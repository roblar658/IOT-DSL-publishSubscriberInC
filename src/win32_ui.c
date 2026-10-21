#include "win32_ui.h"
#include "logger.h"
#include "web_server.h"
#include "parser.h"

#if defined(PLATFORM_WINDOWS)
#include <conio.h>

#define MAX_UI_LOGS 300
#define DEFAULT_CONSOLE_WIDTH 100
#define DEFAULT_CONSOLE_HEIGHT 30

typedef struct {
    LogLevel level;
    char component[16];
    char message[256];
    char timestamp[32];
} UiLogItem;

static UiLogItem s_logs[MAX_UI_LOGS];
static int s_log_start = 0;
static int s_log_count = 0;
static CRITICAL_SECTION s_ui_lock;
static bool s_ui_lock_inited = false;
static volatile bool s_need_redraw = true;
static volatile bool s_ui_running = false;

static char s_input_buf[128] = "";
static size_t s_input_len = 0;
static char s_status_msg[128] = "System klar. Tast 1-7 for hurtighandlinger.";

static void ui_lock(void) {
    if (!s_ui_lock_inited) {
        InitializeCriticalSection(&s_ui_lock);
        s_ui_lock_inited = true;
    }
    EnterCriticalSection(&s_ui_lock);
}

static void ui_unlock(void) {
    LeaveCriticalSection(&s_ui_lock);
}

static void on_ui_logger_hook(LogLevel level, const char *component, const char *message, const char *timestamp, void *userdata) {
    (void)userdata;
    ui_lock();
    int idx;
    if (s_log_count < MAX_UI_LOGS) {
        idx = (s_log_start + s_log_count) % MAX_UI_LOGS;
        s_log_count++;
    } else {
        idx = s_log_start;
        s_log_start = (s_log_start + 1) % MAX_UI_LOGS;
    }

    s_logs[idx].level = level;
    strncpy(s_logs[idx].component, component ? component : "CORE", sizeof(s_logs[idx].component) - 1);
    s_logs[idx].component[sizeof(s_logs[idx].component) - 1] = '\0';

    strncpy(s_logs[idx].message, message ? message : "", sizeof(s_logs[idx].message) - 1);
    s_logs[idx].message[sizeof(s_logs[idx].message) - 1] = '\0';

    strncpy(s_logs[idx].timestamp, timestamp ? timestamp : "", sizeof(s_logs[idx].timestamp) - 1);
    s_logs[idx].timestamp[sizeof(s_logs[idx].timestamp) - 1] = '\0';

    s_need_redraw = true;
    ui_unlock();
}

static int utf8_display_width(const char *str) {
    if (!str) return 0;
    int width = 0;
    bool in_ansi = false;
    for (size_t i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\033') {
            in_ansi = true;
            continue;
        }
        if (in_ansi) {
            if (c == 'm' || c == 'H' || c == 'J' || c == 'K') {
                in_ansi = false;
            }
            continue;
        }
        // Skip UTF-8 continuation bytes
        if ((c & 0xC0) == 0x80) {
            continue;
        }
        width++;
    }
    return width;
}

static void get_console_size(int *width, int *height) {
    *width = DEFAULT_CONSOLE_WIDTH;
    *height = DEFAULT_CONSOLE_HEIGHT;

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
            int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
            if (w >= 70) *width = w;
            if (h >= 20) *height = h;
        }
    }
}

static const char *level_to_color_str(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG:   return "\033[36m";       // Cyan
        case LOG_LEVEL_INFO:    return "\033[32m";       // Green
        case LOG_LEVEL_ROUTING: return "\033[35m";       // Magenta
        case LOG_LEVEL_NETWORK: return "\033[94m";       // Bright Blue
        case LOG_LEVEL_WARN:    return "\033[33m";       // Yellow
        case LOG_LEVEL_ERROR:   return "\033[1;31m";     // Bold Red
        default:                return "\033[0m";
    }
}

static const char *level_to_label(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG:   return "DEBUG";
        case LOG_LEVEL_INFO:    return "INFO";
        case LOG_LEVEL_ROUTING: return "ROUTING";
        case LOG_LEVEL_NETWORK: return "NET_TX";
        case LOG_LEVEL_WARN:    return "WARN";
        case LOG_LEVEL_ERROR:   return "ERROR";
        default:                return "LOG";
    }
}

// Print a box line with exact inner padding
static void print_box_row(int total_w, const char *content_fmt, ...) {
    char raw[1024];
    va_list args;
    va_start(args, content_fmt);
    vsnprintf(raw, sizeof(raw), content_fmt, args);
    va_end(args);

    int inner_w = total_w - 4; // ║ (space) ... (space) ║
    if (inner_w < 10) inner_w = 10;

    int vis_w = utf8_display_width(raw);

    printf("║ ");
    if (vis_w <= inner_w) {
        printf("%s", raw);
        for (int i = 0; i < inner_w - vis_w; i++) {
            putchar(' ');
        }
    } else {
        // Clip content to fit
        int cur_w = 0;
        bool in_ansi = false;
        for (size_t i = 0; raw[i] != '\0'; i++) {
            unsigned char c = (unsigned char)raw[i];
            if (c == '\033') {
                in_ansi = true;
                putchar(c);
                continue;
            }
            if (in_ansi) {
                putchar(c);
                if (c == 'm') in_ansi = false;
                continue;
            }
            if ((c & 0xC0) == 0x80) {
                putchar(c);
                continue;
            }
            if (cur_w + 3 >= inner_w) {
                printf("...");
                cur_w += 3;
                break;
            }
            putchar(c);
            cur_w++;
        }
        printf("\033[0m");
        for (int i = cur_w; i < inner_w; i++) {
            putchar(' ');
        }
    }
    printf(" ║\n");
}

static void print_horizontal_border(int total_w, const char *left, const char *mid, const char *right) {
    printf("%s", left);
    int inner_w = total_w - 2;
    for (int i = 0; i < inner_w; i++) {
        printf("%s", mid);
    }
    printf("%s\n", right);
}

static void ui_render_full(void) {
    int w, h;
    get_console_size(&w, &h);

    Runtime *rt = web_server_get_runtime();
    size_t pub_count = 0, sub_count = 0;
    uint32_t seq_count = 0;
    if (rt) {
        web_server_lock_runtime();
        for (size_t i = 0; i < MAX_TAGS; i++) {
            if (rt->publisher_tags.entries[i].in_use) pub_count++;
            if (rt->subscriber_tags.entries[i].in_use) sub_count++;
        }
        seq_count = rt->message_sequence;
        web_server_unlock_runtime();
    }

    // Hide cursor & move to top-left
    printf("\033[?25l\033[H");

    // 1. TOP HEADER
    print_horizontal_border(w, "╔", "═", "╗");
    print_box_row(w, "\033[1;36m  PUBSUB DSL - WIN32 SCADA KONTROLLPANEL & LOGG (UTF-8)\033[0m       Port: \033[1;33m%-5d\033[0m", web_server_get_port());
    print_box_row(w, "  Web: \033[1;32mhttp://localhost:%-5d\033[0m │ Status: \033[1;32mAKTIV / KJØRER\033[0m │ Pakker: \033[1;37m%-5u\033[0m │ Noder: \033[1;33m%2zu pub / %2zu sub\033[0m",
                  web_server_get_port(), seq_count, pub_count, sub_count);
    print_horizontal_border(w, "╠", "═", "╣");

    // 2. LOG WINDOW HEADER
    int log_area_lines = h - 14;
    if (log_area_lines < 6) log_area_lines = 6;

    print_box_row(w, "\033[1;37m  SANNTIDSLOGG & HENDELSER (Viser siste %d oppføringer)\033[0m", log_area_lines);
    print_horizontal_border(w, "╟", "─", "╢");

    // LOG ENTRIES
    ui_lock();
    int available_logs = s_log_count;
    int start_offset = 0;
    if (available_logs > log_area_lines) {
        start_offset = available_logs - log_area_lines;
    }
    int lines_rendered = 0;
    for (int i = 0; i < log_area_lines; i++) {
        int log_num = start_offset + i;
        if (log_num < available_logs) {
            int entry_idx = (s_log_start + log_num) % MAX_UI_LOGS;
            UiLogItem *item = &s_logs[entry_idx];
            const char *col = level_to_color_str(item->level);
            const char *lbl = level_to_label(item->level);

            // Extract just HH:MM:SS.mmm if timestamp has date
            const char *ts = item->timestamp;
            if (strlen(ts) > 11) {
                ts = item->timestamp + 11; // Skip YYYY-MM-DD
            }

            print_box_row(w, "[%s] %s%-7s\033[0m \033[1;37m[%-7s]\033[0m %s",
                          ts, col, lbl, item->component, item->message);
            lines_rendered++;
        } else {
            print_box_row(w, " ");
            lines_rendered++;
        }
    }
    ui_unlock();

    // 3. ACTIONS MENU
    print_horizontal_border(w, "╠", "═", "╣");
    print_box_row(w, "\033[1;33m HURTIGHANDLINGER / TESTBESKJEDER:\033[0m");
    print_box_row(w, "  \033[1;32m[1]\033[0m Manuell Brannalarm (E1 -> Sentral)      \033[1;32m[2]\033[0m Kringkast Evakuering (Sentral -> Alle)");
    print_box_row(w, "  \033[1;32m[3]\033[0m Røykvarsler Test (Resepsjon -> Sentral) \033[1;32m[4]\033[0m Send Egendefinert Beskjed (Fra/Til/Tekst)");
    print_box_row(w, "  \033[1;32m[5]\033[0m Kjør Fullt DSL Brannalarm-Scenario      \033[1;32m[6]\033[0m Vis Routingtabell & Noder");
    print_box_row(w, "  \033[1;32m[7]\033[0m Nullstill Runtime (Reset)               \033[1;32m[C]\033[0m Tøm Logg          \033[1;31m[Q]\033[0m Avslutt");
    print_horizontal_border(w, "╠", "═", "╣");

    // 4. STATUS & INPUT LINE
    print_box_row(w, " Status: \033[1;36m%s\033[0m", s_status_msg);
    print_box_row(w, " Kommando / hurtigtast [1-7, C, Q, help] > \033[1;37m%s\033[0m", s_input_buf);
    print_horizontal_border(w, "╚", "═", "╝");

    // Show cursor positioned at end of input line
    // Calculate cursor position: row is h - 2, col is prompt length + s_input_len + 3
    int prompt_len = 45;
    int cursor_row = 4 + 2 + log_area_lines + 1 + 6 + 2;
    int cursor_col = prompt_len + (int)s_input_len;
    printf("\033[%d;%dH\033[?25h", cursor_row, cursor_col);
    fflush(stdout);
}

// Prompt for custom message in console
static void handle_custom_message_dialog(void) {
    Runtime *rt = web_server_get_runtime();
    if (!rt) return;

    printf("\033[?25h");
    printf("\n\n--- [SEND EGENDEFINERT BESKJED] ---\n");

    char from_tag[64] = "SENTRAL_KONTROLL";
    char to_tag[64] = "SIRENE_E1";
    char message[256] = "BRANNØVELSE: Testmelding fra Win32 Konsoll";

    printf("Fra node [standard: SENTRAL_KONTROLL]: ");
    fflush(stdout);
    char buf[128];
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_whitespace(buf);
        if (strlen(buf) > 0) {
            strncpy(from_tag, buf, sizeof(from_tag) - 1);
            from_tag[sizeof(from_tag) - 1] = '\0';
        }
    }

    printf("Til node (* for kringkasting/broadcast) [standard: SIRENE_E1]: ");
    fflush(stdout);
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_whitespace(buf);
        if (strlen(buf) > 0) {
            strncpy(to_tag, buf, sizeof(to_tag) - 1);
            to_tag[sizeof(to_tag) - 1] = '\0';
        }
    }

    printf("Meldingstekst [standard: %s]: ", message);
    fflush(stdout);
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_whitespace(buf);
        if (strlen(buf) > 0) {
            strncpy(message, buf, sizeof(message) - 1);
            message[sizeof(message) - 1] = '\0';
        }
    }

    web_server_lock_runtime();
    if (strcmp(to_tag, "*") == 0 || strcasecmp(to_tag, "broadcast") == 0 || strcasecmp(to_tag, "alle") == 0) {
        runtime_send_broadcast(rt, from_tag, message);
        snprintf(s_status_msg, sizeof(s_status_msg), "Kringkasting sendt fra [%s]: \"%s\"", from_tag, message);
    } else {
        runtime_send_direct(rt, from_tag, to_tag, message);
        snprintf(s_status_msg, sizeof(s_status_msg), "Melding sendt fra [%s] til [%s]: \"%s\"", from_tag, to_tag, message);
    }
    web_server_unlock_runtime();

    printf("\nMelding sendt! Trykk en tast for å gå tilbake til konsoll UI...");
    _getch();

    // Clear whole screen for redraw
    printf("\033[2J");
    s_need_redraw = true;
}

static void dump_routing_to_logs(void) {
    Runtime *rt = web_server_get_runtime();
    if (!rt) return;

    web_server_lock_runtime();
    LOG_INFO("ROUTING", "================ AKTIVE PUBLISHER NODER ================");
    for (size_t i = 0; i < MAX_TAGS; i++) {
        if (rt->publisher_tags.entries[i].in_use) {
            TagEntry *e = &rt->publisher_tags.entries[i];
            char ep_str[64];
            net_format_endpoint(&e->endpoint, ep_str, sizeof(ep_str));
            LOG_INFO("ROUTING", "  Pub Tag: %-20s -> %-20s %s", e->name, ep_str, e->is_empty ? "(EMPTY)" : "(AKTIV)");
        }
    }

    LOG_INFO("ROUTING", "=============== AKTIVE SUBSCRIBER NODER ================");
    for (size_t i = 0; i < MAX_TAGS; i++) {
        if (rt->subscriber_tags.entries[i].in_use) {
            TagEntry *e = &rt->subscriber_tags.entries[i];
            char ep_str[64];
            net_format_endpoint(&e->endpoint, ep_str, sizeof(ep_str));
            LOG_INFO("ROUTING", "  Sub Tag: %-20s -> %-20s %s", e->name, ep_str, e->is_empty ? "(EMPTY)" : "(AKTIV)");
        }
    }
    LOG_INFO("ROUTING", "========================================================");
    web_server_unlock_runtime();
    snprintf(s_status_msg, sizeof(s_status_msg), "Routingtabell dumpet til loggvinduet.");
}

static void execute_text_command(const char *cmd_line) {
    char cmd[32] = "";
    char arg1[64] = "";
    char arg2[64] = "";
    char rest[256] = "";

    Runtime *rt = web_server_get_runtime();

    int n = sscanf(cmd_line, "%31s %63s %63s %255[^\n]", cmd, arg1, arg2, rest);
    if (n <= 0) return;

    if (strcasecmp(cmd, "help") == 0 || strcasecmp(cmd, "hjelp") == 0 || strcmp(cmd, "?") == 0) {
        LOG_INFO("HJELP", "Tilgjengelige kommandoer:");
        LOG_INFO("HJELP", "  1-7                    Hurtigvalg (se meny)");
        LOG_INFO("HJELP", "  send <fra> <til> <msg> Send direkte melding");
        LOG_INFO("HJELP", "  broadcast <fra> <msg>  Kringkast til alle");
        LOG_INFO("HJELP", "  run <script.dsl>       Kjør DSL-script");
        LOG_INFO("HJELP", "  nodes                  Vis noder og routing");
        LOG_INFO("HJELP", "  reset                  Nullstill runtime");
        LOG_INFO("HJELP", "  clear                  Tøm loggvinduet");
        LOG_INFO("HJELP", "  quit / exit            Avslutt server");
        snprintf(s_status_msg, sizeof(s_status_msg), "Hjelp vist i loggvinduet.");
    } else if (strcasecmp(cmd, "send") == 0) {
        if (n >= 4 && rt) {
            web_server_lock_runtime();
            runtime_send_direct(rt, arg1, arg2, rest);
            web_server_unlock_runtime();
            snprintf(s_status_msg, sizeof(s_status_msg), "Sendt fra [%s] til [%s]: \"%s\"", arg1, arg2, rest);
        } else {
            snprintf(s_status_msg, sizeof(s_status_msg), "Bruk: send <fra_tag> <til_tag> <beskjed>");
        }
    } else if (strcasecmp(cmd, "broadcast") == 0) {
        if (n >= 3 && rt) {
            char bmsg[256] = "";
            if (n == 3) {
                strncpy(bmsg, arg2, sizeof(bmsg) - 1);
            } else {
                snprintf(bmsg, sizeof(bmsg), "%s %s", arg2, rest);
            }
            web_server_lock_runtime();
            runtime_send_broadcast(rt, arg1, bmsg);
            web_server_unlock_runtime();
            snprintf(s_status_msg, sizeof(s_status_msg), "Kringkasting sendt fra [%s]: \"%s\"", arg1, bmsg);
        } else {
            snprintf(s_status_msg, sizeof(s_status_msg), "Bruk: broadcast <fra_tag> <beskjed>");
        }
    } else if (strcasecmp(cmd, "run") == 0) {
        const char *script = (n >= 2 && strlen(arg1) > 0) ? arg1 : "scripts/fire_alarm_complex.dsl";
        FILE *f = fopen(script, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *buf = (char *)malloc(sz + 1);
            if (buf) {
                fread(buf, 1, sz, f);
                buf[sz] = '\0';
                web_server_run_dsl(buf);
                free(buf);
                snprintf(s_status_msg, sizeof(s_status_msg), "Kjører script '%s'...", script);
            }
            fclose(f);
        } else {
            snprintf(s_status_msg, sizeof(s_status_msg), "Feil: Fant ikke script '%s'", script);
        }
    } else if (strcasecmp(cmd, "nodes") == 0 || strcasecmp(cmd, "routing") == 0) {
        dump_routing_to_logs();
    } else if (strcasecmp(cmd, "reset") == 0) {
        web_server_reset_runtime();
        snprintf(s_status_msg, sizeof(s_status_msg), "Runtime er nullstilt.");
    } else if (strcasecmp(cmd, "clear") == 0 || strcasecmp(cmd, "cls") == 0) {
        ui_lock();
        s_log_count = 0;
        s_log_start = 0;
        ui_unlock();
        snprintf(s_status_msg, sizeof(s_status_msg), "Logg tømt.");
    } else if (strcasecmp(cmd, "quit") == 0 || strcasecmp(cmd, "exit") == 0 || strcasecmp(cmd, "q") == 0) {
        s_ui_running = false;
    } else {
        snprintf(s_status_msg, sizeof(s_status_msg), "Ukjent kommando '%s'. Skriv 'help' for hjelp.", cmd);
    }
}

// Pre-load default tags if runtime is clean
static void ensure_default_tags_loaded(void) {
    Runtime *rt = web_server_get_runtime();
    if (!rt) return;

    web_server_lock_runtime();
    if (rt->publisher_tags.count == 0 && rt->subscriber_tags.count == 0) {
        // Read scripts/fire_alarm_complex.dsl and parse tags
        FILE *f = fopen("scripts/fire_alarm_complex.dsl", "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *buf = (char *)malloc(sz + 1);
            if (buf) {
                fread(buf, 1, sz, f);
                buf[sz] = '\0';
                Parser p;
                parser_init(&p, buf);
                Program *prog = parser_parse_program(&p);
                if (prog) {
                    for (size_t i = 0; i < prog->count; i++) {
                        AstStmt *s = prog->statements[i];
                        if (s->type == STMT_TAGS_BLOCK || s->type == STMT_SUBSCRIBER_BLOCK) {
                            runtime_execute_statement(rt, s);
                        }
                    }
                    program_free(prog);
                }
                free(buf);
            }
            fclose(f);
        }
    }
    web_server_unlock_runtime();
}

int win32_ui_run(int port, const char *web_root) {
    platform_init_utf8();

    // 1. Start web server in background
    if (!web_server_start_background(port, web_root)) {
        fprintf(stderr, "Feil: Kunne ikke starte webserver på port %d\n", port);
        return 1;
    }

    // 2. Pre-populate tags so they are ready for interactive send
    ensure_default_tags_loaded();

    // 3. Register UI log hook and disable direct stdout logging (so log window controls output)
    logger_add_hook(on_ui_logger_hook, NULL);
    logger_enable_console_output(false);

    // Initial log banner
    LOG_INFO("UI", "PubSub DSL Win32 SCADA Kontrollpanel initialisert (UTF-8 aktiv).");
    LOG_INFO("UI", "Webserver lytter på port %d (http://localhost:%d).", port, port);
    LOG_INFO("UI", "Bruk hurtigtaster [1-7] eller skriv kommandoer for å sende beskjeder.");

    s_ui_running = true;

    // Clear console screen once
    printf("\033[2J");

    int last_w = 0, last_h = 0;

    while (s_ui_running && web_server_is_running()) {
        int cur_w, cur_h;
        get_console_size(&cur_w, &cur_h);
        if (cur_w != last_w || cur_h != last_h) {
            last_w = cur_w;
            last_h = cur_h;
            printf("\033[2J");
            s_need_redraw = true;
        }

        // Process keyboard input
        while (_kbhit()) {
            int ch = _getch();

            if (ch == 27) { // ESC key
                s_ui_running = false;
                break;
            } else if (ch == '\r' || ch == '\n') {
                if (s_input_len > 0) {
                    s_input_buf[s_input_len] = '\0';
                    char cmd_copy[128];
                    strncpy(cmd_copy, s_input_buf, sizeof(cmd_copy) - 1);
                    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
                    s_input_len = 0;
                    s_input_buf[0] = '\0';
                    execute_text_command(cmd_copy);
                    s_need_redraw = true;
                }
            } else if (ch == '\b' || ch == 127) { // Backspace
                if (s_input_len > 0) {
                    s_input_len--;
                    s_input_buf[s_input_len] = '\0';
                    s_need_redraw = true;
                }
            } else if (s_input_len == 0 && (ch == '1' || ch == '2' || ch == '3' || ch == '4' || ch == '5' || ch == '6' || ch == '7' || ch == 'q' || ch == 'Q' || ch == 'c' || ch == 'C')) {
                // Immediate single-key hotkey
                Runtime *rt = web_server_get_runtime();
                if (ch == '1') {
                    if (rt) {
                        web_server_lock_runtime();
                        runtime_send_direct(rt, "MANUELL_MELDER_E1", "SENTRAL_KONTROLL", "BRANNALARM: Manuell melder E1 utløst i resepsjon!");
                        web_server_unlock_runtime();
                        snprintf(s_status_msg, sizeof(s_status_msg), "Manuell brannalarm E1 utløst!");
                    }
                } else if (ch == '2') {
                    if (rt) {
                        web_server_lock_runtime();
                        runtime_send_broadcast(rt, "SENTRAL_KONTROLL", "GENERAL_EVACUATION: Talevarsling aktivert - Forlat bygningen!");
                        web_server_unlock_runtime();
                        snprintf(s_status_msg, sizeof(s_status_msg), "Generell evakuering kringkastet til alle noder!");
                    }
                } else if (ch == '3') {
                    if (rt) {
                        web_server_lock_runtime();
                        runtime_send_direct(rt, "ROYK_RESEPSJON", "SENTRAL_KONTROLL", "PRE_ALARM: Røykdetektor utløst i resepsjon (nivå 2)!");
                        web_server_unlock_runtime();
                        snprintf(s_status_msg, sizeof(s_status_msg), "Røykvarsler testet i resepsjon.");
                    }
                } else if (ch == '4') {
                    handle_custom_message_dialog();
                } else if (ch == '5') {
                    web_server_run_dsl(NULL);
                    snprintf(s_status_msg, sizeof(s_status_msg), "Kjører fire_alarm_complex.dsl i C-motor...");
                } else if (ch == '6') {
                    dump_routing_to_logs();
                } else if (ch == '7') {
                    web_server_reset_runtime();
                    snprintf(s_status_msg, sizeof(s_status_msg), "Runtime er nullstilt.");
                } else if (ch == 'c' || ch == 'C') {
                    ui_lock();
                    s_log_count = 0;
                    s_log_start = 0;
                    ui_unlock();
                    snprintf(s_status_msg, sizeof(s_status_msg), "Logg tømt.");
                } else if (ch == 'q' || ch == 'Q') {
                    s_ui_running = false;
                }
                s_need_redraw = true;
            } else if (ch >= 32 && ch <= 255) {
                // Printable character typed into input buffer
                if (s_input_len + 1 < sizeof(s_input_buf)) {
                    s_input_buf[s_input_len++] = (char)ch;
                    s_input_buf[s_input_len] = '\0';
                    s_need_redraw = true;
                }
            }
        }

        if (s_need_redraw) {
            ui_render_full();
            s_need_redraw = false;
        }

        Sleep(25);
    }

    // Cleanup & restore console
    logger_remove_hook(on_ui_logger_hook);
    logger_enable_console_output(true);
    web_server_stop();

    printf("\033[?25h\033[2J\033[H");
    printf("=================================================================\n");
    printf("  PubSub DSL Win32 SCADA Kontrollpanel er avsluttet.\n");
    printf("  Ha en fin dag!\n");
    printf("=================================================================\n");

    return 0;
}

#else
// Non-windows stub fallback
int win32_ui_run(int port, const char *web_root) {
    return web_server_start(port, web_root);
}
#endif
