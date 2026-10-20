#include "web_server.h"
#include "logger.h"
#include "parser.h"
#include "runtime.h"

#define MAX_SSE_CLIENTS 32
#define HTTP_REQ_BUF_SIZE 65536
#define HTTP_RESP_BUF_SIZE 131072

static Runtime g_runtime;
static bool g_runtime_inited = false;
static volatile bool g_server_running = false;
static socket_t g_listen_socket = INVALID_SOCKET_VAL;
static char g_web_root[256] = "web";

static socket_t s_sse_clients[MAX_SSE_CLIENTS];
static int s_sse_count = 0;

#if defined(PLATFORM_WINDOWS)
static CRITICAL_SECTION s_sse_lock;
static CRITICAL_SECTION s_rt_lock;
static bool s_locks_inited = false;
#else
static pthread_mutex_t s_sse_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_rt_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static void lock_sse(void) {
#if defined(PLATFORM_WINDOWS)
    if (!s_locks_inited) {
        InitializeCriticalSection(&s_sse_lock);
        InitializeCriticalSection(&s_rt_lock);
        s_locks_inited = true;
    }
    EnterCriticalSection(&s_sse_lock);
#else
    pthread_mutex_lock(&s_sse_lock);
#endif
}

static void unlock_sse(void) {
#if defined(PLATFORM_WINDOWS)
    LeaveCriticalSection(&s_sse_lock);
#else
    pthread_mutex_unlock(&s_sse_lock);
#endif
}

static void lock_rt(void) {
#if defined(PLATFORM_WINDOWS)
    if (!s_locks_inited) {
        InitializeCriticalSection(&s_sse_lock);
        InitializeCriticalSection(&s_rt_lock);
        s_locks_inited = true;
    }
    EnterCriticalSection(&s_rt_lock);
#else
    pthread_mutex_lock(&s_rt_lock);
#endif
}

static void unlock_rt(void) {
#if defined(PLATFORM_WINDOWS)
    LeaveCriticalSection(&s_rt_lock);
#else
    pthread_mutex_unlock(&s_rt_lock);
#endif
}

// Broadcast an SSE event string to all active SSE subscribers
static void broadcast_sse_event(const char *json_data) {
    if (!json_data) return;

    char sse_msg[8192];
    int len = snprintf(sse_msg, sizeof(sse_msg), "data: %s\n\n", json_data);
    if (len <= 0) return;

    lock_sse();
    for (int i = 0; i < s_sse_count; i++) {
        socket_t sock = s_sse_clients[i];
        if (sock != INVALID_SOCKET_VAL) {
            int sent = send(sock, sse_msg, len, 0);
            if (sent <= 0) {
                // Client disconnected
                close_socket(sock);
                s_sse_clients[i] = s_sse_clients[s_sse_count - 1];
                s_sse_count--;
                i--;
            }
        }
    }
    unlock_sse();
}

static void escape_json_str(const char *in, char *out, size_t max_len) {
    if (!in || !out || max_len == 0) return;
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < max_len; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = c;
        } else if (c == '\n') {
            out[o++] = '\\';
            out[o++] = 'n';
        } else if (c == '\r') {
            // omit
        } else if (c == '\t') {
            out[o++] = ' ';
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

// -----------------------------------------------------------------------------
// RUNTIME & LOGGER HOOKS (Emitting events from C directly into SSE stream)
// -----------------------------------------------------------------------------

static void on_logger_hook(LogLevel level, const char *component, const char *message, const char *timestamp, void *userdata) {
    (void)userdata;
    const char *lvl_str = "INFO";
    switch (level) {
        case LOG_LEVEL_DEBUG:   lvl_str = "DEBUG"; break;
        case LOG_LEVEL_INFO:    lvl_str = "INFO"; break;
        case LOG_LEVEL_ROUTING: lvl_str = "ROUTING"; break;
        case LOG_LEVEL_NETWORK: lvl_str = "NETWORK"; break;
        case LOG_LEVEL_WARN:    lvl_str = "WARN"; break;
        case LOG_LEVEL_ERROR:   lvl_str = "ERROR"; break;
        default:                lvl_str = "INFO"; break;
    }

    char esc_msg[2048];
    escape_json_str(message, esc_msg, sizeof(esc_msg));

    char json[4096];
    snprintf(json, sizeof(json),
             "{\"type\":\"LOG\",\"level\":\"%s\",\"category\":\"%s\",\"message\":\"%s\",\"timestamp\":\"%s\"}",
             lvl_str, component ? component : "CORE", esc_msg, timestamp ? timestamp : "");
    broadcast_sse_event(json);
}

static void on_packet_hook(uint32_t seq, const char *packet_type, const char *from_tag, const char *to_tag,
                           const char *from_ep, const char *to_ep, const char *payload, size_t bytes, void *userdata) {
    (void)userdata;
    char esc_payload[2048];
    escape_json_str(payload, esc_payload, sizeof(esc_payload));

    char json[4096];
    snprintf(json, sizeof(json),
             "{\"type\":\"PACKET\",\"seq\":%u,\"packet_type\":\"%s\",\"from_tag\":\"%s\",\"to_tag\":\"%s\","
             "\"from_ep\":\"%s\",\"to_ep\":\"%s\",\"payload\":\"%s\",\"byte_size\":%zu}",
             seq, packet_type, from_tag, to_tag, from_ep, to_ep, esc_payload, bytes);
    broadcast_sse_event(json);
}

static void on_routing_hook(const char *action, const char *registry, const char *tag, const char *endpoint, void *userdata) {
    (void)userdata;
    char json[1024];
    snprintf(json, sizeof(json),
             "{\"type\":\"ROUTING_UPDATE\",\"action\":\"%s\",\"registry\":\"%s\",\"tag\":\"%s\",\"endpoint\":\"%s\"}",
             action, registry, tag, endpoint);
    broadcast_sse_event(json);
}

static void on_step_hook(size_t step_idx, size_t total_steps, int line, const char *stmt_type, void *userdata) {
    (void)userdata;
    char json[512];
    snprintf(json, sizeof(json),
             "{\"type\":\"STEP\",\"index\":%zu,\"total\":%zu,\"line\":%d,\"stmt_type\":\"%s\"}",
             step_idx, total_steps, line, stmt_type);
    broadcast_sse_event(json);
}

// -----------------------------------------------------------------------------
// SCRIPT RUNNER BACKGROUND THREAD
// -----------------------------------------------------------------------------

typedef struct {
    char *dsl_source;
} ScriptRunnerArgs;

#if defined(PLATFORM_WINDOWS)
static unsigned __stdcall script_runner_thread(void *arg) {
#else
static void *script_runner_thread(void *arg) {
#endif
    ScriptRunnerArgs *args = (ScriptRunnerArgs *)arg;
    char *source = args->dsl_source;
    free(args);

    broadcast_sse_event("{\"type\":\"STATUS\",\"state\":\"RUNNING\"}");

    Parser parser;
    parser_init(&parser, source);
    Program *prog = parser_parse_program(&parser);
    free(source);

    if (!prog) {
        LOG_ERROR("EXEC", "Failed to parse DSL program in C parser");
        broadcast_sse_event("{\"type\":\"STATUS\",\"state\":\"ERROR\",\"message\":\"Parser failed\"}");
#if defined(PLATFORM_WINDOWS)
        return 0;
#else
        return NULL;
#endif
    }

    lock_rt();
    g_runtime.packet_hook = on_packet_hook;
    g_runtime.routing_hook = on_routing_hook;
    g_runtime.step_hook = on_step_hook;
    g_runtime.step_delay_ms = 400; // default visible delay for 3D animation
    g_runtime.abort_execution = false;
    g_runtime.is_paused = false;
    unlock_rt();

    runtime_execute_program(&g_runtime, prog);
    program_free(prog);

    broadcast_sse_event("{\"type\":\"STATUS\",\"state\":\"FINISHED\"}");

#if defined(PLATFORM_WINDOWS)
    return 0;
#else
    return NULL;
#endif
}

static void start_script_execution(const char *source_code) {
    ScriptRunnerArgs *args = (ScriptRunnerArgs *)malloc(sizeof(ScriptRunnerArgs));
    args->dsl_source = safe_strdup(source_code);

#if defined(PLATFORM_WINDOWS)
    uintptr_t handle = _beginthreadex(NULL, 0, script_runner_thread, args, 0, NULL);
    if (handle) {
        CloseHandle((HANDLE)handle);
    }
#else
    pthread_t tid;
    pthread_create(&tid, NULL, script_runner_thread, args);
    pthread_detach(tid);
#endif
}

// -----------------------------------------------------------------------------
// HTTP REQUEST HANDLING
// -----------------------------------------------------------------------------

static const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcasecmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(ext, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcasecmp(ext, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcasecmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(ext, ".png") == 0)  return "image/png";
    if (strcasecmp(ext, ".jpg") == 0)  return "image/jpeg";
    if (strcasecmp(ext, ".svg") == 0)  return "image/svg+xml";
    if (strcasecmp(ext, ".webp") == 0) return "image/webp";
    if (strcasecmp(ext, ".dsl") == 0)  return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static void send_http_response(socket_t sock, int status_code, const char *status_text,
                               const char *content_type, const char *body, size_t body_len) {
    char header[512];
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                        "Access-Control-Allow-Headers: Content-Type, Accept\r\n"
                        "Connection: close\r\n\r\n",
                        status_code, status_text, content_type, body_len);
    send(sock, header, hlen, 0);
    if (body && body_len > 0) {
        send(sock, body, (int)body_len, 0);
    }
}

static void serve_static_file(socket_t sock, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        const char *not_found = "404 Not Found";
        send_http_response(sock, 404, "Not Found", "text/plain", not_found, strlen(not_found));
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(fsize);
    if (!buf) {
        fclose(f);
        const char *err = "500 Internal Error";
        send_http_response(sock, 500, "Internal Server Error", "text/plain", err, strlen(err));
        return;
    }

    size_t read_bytes = fread(buf, 1, fsize, f);
    fclose(f);

    send_http_response(sock, 200, "OK", get_mime_type(filepath), buf, read_bytes);
    free(buf);
}

static void handle_http_connection(socket_t client_sock) {
    char buffer[HTTP_REQ_BUF_SIZE];
    int received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        close_socket(client_sock);
        return;
    }
    buffer[received] = '\0';

    char method[16], url[256], proto[16];
    if (sscanf(buffer, "%15s %255s %15s", method, url, proto) < 2) {
        close_socket(client_sock);
        return;
    }

    // Split query string if any
    char *query = strchr(url, '?');
    if (query) {
        *query = '\0';
        query++;
    }

    // 0. CORS PREFLIGHT: OPTIONS
    if (strcmp(method, "OPTIONS") == 0) {
        const char *opt_resp =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Accept\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n";
        send(client_sock, opt_resp, strlen(opt_resp), 0);
        close_socket(client_sock);
        return;
    }

    // Ensure full body is read if Content-Length is present
    char *cl = strstr(buffer, "Content-Length:");
    if (!cl) cl = strstr(buffer, "content-length:");
    if (cl) {
        int content_len = atoi(cl + 15);
        char *body_marker = strstr(buffer, "\r\n\r\n");
        if (body_marker && content_len > 0) {
            char *body_start = body_marker + 4;
            int header_len = (int)(body_start - buffer);
            int current_body_len = received - header_len;
            while (current_body_len < content_len && received < (int)sizeof(buffer) - 1) {
                int more = recv(client_sock, buffer + received, sizeof(buffer) - 1 - received, 0);
                if (more <= 0) break;
                received += more;
                current_body_len += more;
            }
            buffer[received] = '\0';
        }
    }

    // 1. SSE STREAM: GET /api/events
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/events") == 0) {
        const char *sse_header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n"
            "data: {\"type\":\"CONNECTED\",\"backend\":\"C\"}\n\n";
        send(client_sock, sse_header, strlen(sse_header), 0);

        lock_sse();
        if (s_sse_count < MAX_SSE_CLIENTS) {
            s_sse_clients[s_sse_count++] = client_sock;
        } else {
            close_socket(client_sock);
        }
        unlock_sse();
        return; // keep connection open
    }

    // 2. RUN DSL SCRIPT: POST /api/run
    if (strcmp(method, "POST") == 0 && strcmp(url, "/api/run") == 0) {
        char *body = strstr(buffer, "\r\n\r\n");
        if (body) {
            body += 4;
        }
        if (body && strlen(body) > 0) {
            start_script_execution(body);
        } else {
            // Default load scripts/fire_alarm_complex.dsl
            FILE *f = fopen("scripts/fire_alarm_complex.dsl", "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                char *filebuf = (char *)malloc(sz + 1);
                fread(filebuf, 1, sz, f);
                filebuf[sz] = '\0';
                fclose(f);
                start_script_execution(filebuf);
                free(filebuf);
            }
        }
        const char *resp = "{\"status\":\"OK\",\"message\":\"Execution started in C engine\"}";
        send_http_response(client_sock, 200, "OK", "application/json", resp, strlen(resp));
        close_socket(client_sock);
        return;
    }

    // 3. PAUSE / RESUME / STOP
    if (strcmp(method, "POST") == 0 && strcmp(url, "/api/pause") == 0) {
        lock_rt();
        g_runtime.is_paused = true;
        unlock_rt();
        broadcast_sse_event("{\"type\":\"STATUS\",\"state\":\"PAUSED\"}");
        const char *resp = "{\"status\":\"OK\"}";
        send_http_response(client_sock, 200, "OK", "application/json", resp, strlen(resp));
        close_socket(client_sock);
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(url, "/api/resume") == 0) {
        lock_rt();
        g_runtime.is_paused = false;
        unlock_rt();
        broadcast_sse_event("{\"type\":\"STATUS\",\"state\":\"RUNNING\"}");
        const char *resp = "{\"status\":\"OK\"}";
        send_http_response(client_sock, 200, "OK", "application/json", resp, strlen(resp));
        close_socket(client_sock);
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(url, "/api/stop") == 0) {
        lock_rt();
        g_runtime.abort_execution = true;
        g_runtime.is_paused = false;
        unlock_rt();
        broadcast_sse_event("{\"type\":\"STATUS\",\"state\":\"STOPPED\"}");
        const char *resp = "{\"status\":\"OK\"}";
        send_http_response(client_sock, 200, "OK", "application/json", resp, strlen(resp));
        close_socket(client_sock);
        return;
    }

    // 4. RESET RUNTIME
    if (strcmp(method, "POST") == 0 && strcmp(url, "/api/reset") == 0) {
        lock_rt();
        g_runtime.abort_execution = true;
        g_runtime.is_paused = false;
        runtime_free(&g_runtime);
        runtime_init(&g_runtime);
        unlock_rt();
        broadcast_sse_event("{\"type\":\"RESET\"}");
        const char *resp = "{\"status\":\"OK\"}";
        send_http_response(client_sock, 200, "OK", "application/json", resp, strlen(resp));
        close_socket(client_sock);
        return;
    }

    // 5. TRIGGER DEVICE EVENT DIRECTLY IN C RUNTIME
    if (strcmp(method, "POST") == 0 && strcmp(url, "/api/trigger") == 0) {
        char *body = strstr(buffer, "\r\n\r\n");
        if (body) body += 4;

        char from_tag[64] = "MANUELL_MELDER_E1";
        char to_tag[64] = "SENTRAL_KONTROLL";
        char msg[256] = "MANUAL_TRIGGER_FROM_UI";

        if (body) {
            char *f = strstr(body, "\"from_tag\":");
            if (f) sscanf(f + 11, " \"%63[^\"]\"", from_tag);
            char *t = strstr(body, "\"to_tag\":");
            if (t) sscanf(t + 9, " \"%63[^\"]\"", to_tag);
            char *m = strstr(body, "\"message\":");
            if (m) sscanf(m + 10, " \"%255[^\"]\"", msg);
        }

        lock_rt();
        runtime_send_direct(&g_runtime, from_tag, to_tag, msg);
        unlock_rt();

        const char *resp = "{\"status\":\"OK\"}";
        send_http_response(client_sock, 200, "OK", "application/json", resp, strlen(resp));
        close_socket(client_sock);
        return;
    }

    // 6. ROUTING DUMP IN JSON: GET /api/routing
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/routing") == 0) {
        char json[16384];
        lock_rt();
        runtime_to_json(&g_runtime, json, sizeof(json));
        unlock_rt();
        send_http_response(client_sock, 200, "OK", "application/json", json, strlen(json));
        close_socket(client_sock);
        return;
    }

    // 7. SCENARIO SOURCE: GET /api/scenario?file=...
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/scenario") == 0) {
        char script_path[256] = "scripts/fire_alarm_complex.dsl";
        if (query && strncmp(query, "file=", 5) == 0) {
            snprintf(script_path, sizeof(script_path), "scripts/%s", query + 5);
        }
        serve_static_file(client_sock, script_path);
        close_socket(client_sock);
        return;
    }

    // 8. STATIC FILES FROM web/
    if (strcmp(method, "GET") == 0) {
        char filepath[512];
        const char *rel_path = url;
        if (strcmp(rel_path, "/") == 0) {
            rel_path = "/index.html";
        }
        snprintf(filepath, sizeof(filepath), "%s%s", g_web_root, rel_path);
        serve_static_file(client_sock, filepath);
        close_socket(client_sock);
        return;
    }

    // Fallback
    const char *bad = "400 Bad Request";
    send_http_response(client_sock, 400, "Bad Request", "text/plain", bad, strlen(bad));
    close_socket(client_sock);
}

#if defined(PLATFORM_WINDOWS)
static unsigned __stdcall client_worker_thread(void *arg) {
    socket_t client = (socket_t)(intptr_t)arg;
    handle_http_connection(client);
    return 0;
}
#else
static void *client_worker_thread(void *arg) {
    socket_t client = (socket_t)(intptr_t)arg;
    handle_http_connection(client);
    return NULL;
}
#endif

// -----------------------------------------------------------------------------
// SERVER ENTRY POINT
// -----------------------------------------------------------------------------

int web_server_start(int port, const char *web_root) {
    if (web_root) {
        strncpy(g_web_root, web_root, sizeof(g_web_root) - 1);
    }

    net_init();
    runtime_init(&g_runtime);
    g_runtime_inited = true;

    // Register logger hook to stream C logs via SSE
    logger_set_hook(on_logger_hook, NULL);

    g_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_socket == INVALID_SOCKET_VAL) {
        LOG_ERROR("WEB", "Failed to create TCP listen socket");
        return 1;
    }

    int opt = 1;
    setsockopt(g_listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(g_listen_socket, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR_VAL) {
        LOG_ERROR("WEB", "Failed to bind TCP port %d", port);
        close_socket(g_listen_socket);
        return 1;
    }

    if (listen(g_listen_socket, 32) == SOCKET_ERROR_VAL) {
        LOG_ERROR("WEB", "Failed to listen on TCP port %d", port);
        close_socket(g_listen_socket);
        return 1;
    }

    g_server_running = true;

    printf("=================================================================\n");
    printf("  PubSub DSL C Web Backend & Three.js SCADA Host\n");
    printf("  Kjører i ren C på port %d\n", port);
    printf("  Åpne i nettleseren: http://localhost:%d\n", port);
    printf("=================================================================\n\n");
    LOG_INFO("WEB", "C Web Backend listening on port %d (root: '%s')", port, g_web_root);

    while (g_server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        socket_t client = accept(g_listen_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client == INVALID_SOCKET_VAL) {
            if (!g_server_running) break;
            continue;
        }

#if defined(PLATFORM_WINDOWS)
        uintptr_t th = _beginthreadex(NULL, 0, client_worker_thread, (void *)(intptr_t)client, 0, NULL);
        if (th) {
            CloseHandle((HANDLE)th);
        } else {
            handle_http_connection(client);
        }
#else
        pthread_t th;
        if (pthread_create(&th, NULL, client_worker_thread, (void *)(intptr_t)client) == 0) {
            pthread_detach(th);
        } else {
            handle_http_connection(client);
        }
#endif
    }

    web_server_stop();
    return 0;
}

void web_server_stop(void) {
    g_server_running = false;
    if (g_listen_socket != INVALID_SOCKET_VAL) {
        close_socket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET_VAL;
    }

    lock_sse();
    for (int i = 0; i < s_sse_count; i++) {
        if (s_sse_clients[i] != INVALID_SOCKET_VAL) {
            close_socket(s_sse_clients[i]);
            s_sse_clients[i] = INVALID_SOCKET_VAL;
        }
    }
    s_sse_count = 0;
    unlock_sse();

    if (g_runtime_inited) {
        runtime_free(&g_runtime);
        g_runtime_inited = false;
    }
}
