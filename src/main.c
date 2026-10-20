#include "common.h"
#include "logger.h"
#include "net.h"
#include "parser.h"
#include "runtime.h"
#include "node_listener.h"

static void print_usage(const char *prog_name) {
    printf("=================================================================\n");
    printf("  PubSub DSL Engine & Node Daemon in C\n");
    printf("=================================================================\n");
    printf("Usage:\n");
    printf("  %s run <script.dsl> [options]         Execute a DSL script\n", prog_name);
    printf("  %s listen <port | host:port> [options] Start a subscriber/publisher node\n", prog_name);
    printf("  %s test                                Run built-in self tests\n", prog_name);
    printf("  %s --help                              Show this help message\n\n", prog_name);
    printf("Options for 'run':\n");
    printf("  --log <file>       Specify log file output (default: pubsub_dsl.log)\n");
    printf("  --quiet            Suppress console debug logs\n");
    printf("\nOptions for 'listen':\n");
    printf("  --tag <name>       Tag name for this node (e.g. LAMPE, STUE)\n");
    printf("  --count <N>        Exit automatically after receiving N packets\n");
    printf("  --log <file>       Log file output (default: node_<tag>.log)\n");
    printf("\nDSL Grammar Highlights:\n");
    printf("  publisher_tags:{ STUE::ip:port SOVEROM::ip:port SPISE::ip:port }\n");
    printf("  subscriber_tags:{ LAMPE::ip:port VARME::ip:port }\n");
    printf("  subscribers[tag] = [ ... ]\n");
    printf("  change(publisher_tags, tag):: from_ip:port -> to_ip:port\n");
    printf("  change(publisher_tags, tag)::EMPTY\n");
    printf("  delete(subscriber_tags, tag)\n");
    printf("  add(subscriber_tags, tag):: from_ip:port -> to_ip:port\n");
    printf("  add(subscriber_tags, tag)                  # sets tag::EMPTY\n");
    printf("  subscribe(subscriber_tags, tag):: from_ip:port -> to_ip:port\n");
    printf("  broadcast(pub_tag, subscriber_tags)        # sends to all active subscribers\n");
    printf("  multicast(pub_tag, [tag1, tag2, ...])      # multicasts to listed tags\n");
    printf("  send(pub_tag, sub_tag)                     # direct unicast\n");
    printf("  sleep(ms)                                  # pause execution\n");
    printf("  log(\"message\")                             # output script log\n");
    printf("=================================================================\n");
}

static char *read_file_to_string(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        LOG_ERROR("MAIN", "Could not open file: %s", filepath);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

static int cmd_run(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: missing script file. Usage: pubsub_dsl run <script.dsl>\n");
        return 1;
    }

    const char *script_file = argv[2];
    const char *log_file = "pubsub_dsl.log";
    LogLevel console_lvl = LOG_LEVEL_DEBUG;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        } else if (strcmp(argv[i], "--quiet") == 0) {
            console_lvl = LOG_LEVEL_INFO;
        }
    }

    logger_init(log_file, console_lvl, LOG_LEVEL_DEBUG);
    LOG_INFO("MAIN", "Executing DSL script '%s' (logging to '%s')", script_file, log_file);

    char *source = read_file_to_string(script_file);
    if (!source) {
        logger_close();
        return 1;
    }

    Parser parser;
    parser_init(&parser, source);
    Program *prog = parser_parse_program(&parser);
    free(source);

    if (!prog) {
        LOG_ERROR("MAIN", "Script compilation failed.");
        logger_close();
        return 1;
    }

    Runtime rt;
    runtime_init(&rt);

    bool ok = runtime_execute_program(&rt, prog);

    runtime_free(&rt);
    program_free(prog);
    logger_close();

    return ok ? 0 : 1;
}

static int cmd_listen(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: missing port or endpoint. Usage: pubsub_dsl listen <port> --tag <TAG>\n");
        return 1;
    }

    const char *ep_str = argv[2];
    NodeListenerConfig config;
    memset(&config, 0, sizeof(config));
    strcpy(config.tag_name, "NODE");
    config.max_packets = 0;

    Endpoint ep;
    if (!net_parse_endpoint(ep_str, &ep)) {
        fprintf(stderr, "Error: invalid endpoint format '%s'\n", ep_str);
        return 1;
    }

    strncpy(config.bind_host, ep.host, sizeof(config.bind_host) - 1);
    config.bind_host[sizeof(config.bind_host) - 1] = '\0';
    config.port = ep.port;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--tag") == 0 && i + 1 < argc) {
            strncpy(config.tag_name, argv[++i], sizeof(config.tag_name) - 1);
            config.tag_name[sizeof(config.tag_name) - 1] = '\0';
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            config.max_packets = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            config.logfile = argv[++i];
        }
    }

    return run_node_listener(&config);
}

// Built-in self-test suite
static int cmd_test(void) {
    logger_init("self_test.log", LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG);
    LOG_INFO("TEST", "=================================================");
    LOG_INFO("TEST", "       RUNNING PUBSUB DSL SELF-TEST SUITE        ");
    LOG_INFO("TEST", "=================================================");

    const char *test_script =
        "# Test DSL Script\n"
        "publisher_tags:{\n"
        "  STUE::127.0.0.1:18001\n"
        "  SOVEROM::127.0.0.1:18002\n"
        "  SPISE::127.0.0.1:18003\n"
        "}\n\n"
        "subscriber_tags:{\n"
        "  LAMPE::127.0.0.1:19001\n"
        "  VARME::127.0.0.1:19002\n"
        "}\n\n"
        "subscribers[LAMPE] = [\n"
        "  subscribe(subscriber_tags, LAMPE):: 127.0.0.1:18001 -> 127.0.0.1:19001\n"
        "  change(publisher_tags, SPISE)::EMPTY\n"
        "  add(subscriber_tags, RADIO)\n"
        "  add(subscriber_tags, VIFTE):: 127.0.0.1:18001 -> 127.0.0.1:19003\n"
        "  delete(subscriber_tags, VARME)\n"
        "  change(publisher_tags, SOVEROM):: 127.0.0.1:18002 -> 127.0.0.1:18099\n"
        "]\n\n"
        "log(\"Testing direct messaging...\")\n"
        "send(STUE, LAMPE):: \"Test Unicast Message\"\n"
        "multicast(STUE, [LAMPE, VIFTE]):: \"Test Multicast Message\"\n"
        "broadcast(STUE, subscriber_tags):: \"Test Broadcast Message\"\n";

    LOG_INFO("TEST", "1. Parsing DSL test script...");
    Parser parser;
    parser_init(&parser, test_script);
    Program *prog = parser_parse_program(&parser);

    if (!prog) {
        LOG_ERROR("TEST", "TEST FAILED: Parser returned NULL");
        logger_close();
        return 1;
    }
    LOG_INFO("TEST", "Parser OK. Created program with %zu statements.", prog->count);

    LOG_INFO("TEST", "2. Executing DSL program on Runtime...");
    Runtime rt;
    runtime_init(&rt);
    bool exec_ok = runtime_execute_program(&rt, prog);
    if (!exec_ok) {
        LOG_ERROR("TEST", "TEST FAILED: Execution returned error");
        program_free(prog);
        runtime_free(&rt);
        logger_close();
        return 1;
    }

    LOG_INFO("TEST", "3. Verifying Routing Table Mutations...");
    // Verify STUE exists and is active
    TagEntry *stue = runtime_find_tag(&rt, REGISTRY_PUBLISHER, "STUE");
    if (!stue || stue->endpoint.port != 18001 || stue->is_empty) {
        LOG_ERROR("TEST", "TEST FAILED: STUE should be active at port 18001!");
        return 1;
    }
    LOG_INFO("TEST", "  [PASS] 'STUE' active at port 18001");

    // Verify SPISE is EMPTY
    TagEntry *spise = runtime_find_tag(&rt, REGISTRY_PUBLISHER, "SPISE");
    if (!spise || !spise->is_empty) {
        LOG_ERROR("TEST", "TEST FAILED: SPISE should be marked EMPTY!");
        return 1;
    }
    LOG_INFO("TEST", "  [PASS] 'SPISE' correctly marked as EMPTY");

    // Verify RADIO was added as EMPTY (from `add(subscriber_tags, RADIO)`)
    TagEntry *radio = runtime_find_tag(&rt, REGISTRY_SUBSCRIBER, "RADIO");
    if (!radio || !radio->is_empty) {
        LOG_ERROR("TEST", "TEST FAILED: RADIO should be marked EMPTY!");
        return 1;
    }
    LOG_INFO("TEST", "  [PASS] 'RADIO' correctly added with tag::EMPTY");

    // Verify VIFTE was added with port 19003
    TagEntry *vifte = runtime_find_tag(&rt, REGISTRY_SUBSCRIBER, "VIFTE");
    if (!vifte || vifte->endpoint.port != 19003 || vifte->is_empty) {
        LOG_ERROR("TEST", "TEST FAILED: VIFTE endpoint incorrect!");
        return 1;
    }
    LOG_INFO("TEST", "  [PASS] 'VIFTE' correctly added with endpoint 127.0.0.1:19003");

    // Verify VARME was deleted
    TagEntry *varme = runtime_find_tag(&rt, REGISTRY_SUBSCRIBER, "VARME");
    if (varme != NULL) {
        LOG_ERROR("TEST", "TEST FAILED: VARME should have been deleted!");
        return 1;
    }
    LOG_INFO("TEST", "  [PASS] 'VARME' correctly deleted from subscriber_tags");

    // Verify SOVEROM endpoint was changed to 18099
    TagEntry *soverom = runtime_find_tag(&rt, REGISTRY_PUBLISHER, "SOVEROM");
    if (!soverom || soverom->endpoint.port != 18099) {
        LOG_ERROR("TEST", "TEST FAILED: SOVEROM port should be 18099!");
        return 1;
    }
    LOG_INFO("TEST", "  [PASS] 'SOVEROM' correctly changed to port 18099");

    LOG_INFO("TEST", "=================================================");
    LOG_INFO("TEST", "        ALL UNIT TESTS PASSED SUCCESSFULLY!      ");
    LOG_INFO("TEST", "=================================================");

    runtime_free(&rt);
    program_free(prog);
    logger_close();
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "run") == 0) {
        return cmd_run(argc, argv);
    } else if (strcmp(cmd, "listen") == 0) {
        return cmd_listen(argc, argv);
    } else if (strcmp(cmd, "test") == 0) {
        return cmd_test();
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
        print_usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Unknown command '%s'. Run '%s --help' for usage.\n", cmd, argv[0]);
        return 1;
    }
}
