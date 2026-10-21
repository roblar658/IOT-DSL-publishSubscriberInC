#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "common.h"
#include "runtime.h"

int web_server_start(int port, const char *web_root);
bool web_server_start_background(int port, const char *web_root);
void web_server_stop(void);

Runtime *web_server_get_runtime(void);
void web_server_lock_runtime(void);
void web_server_unlock_runtime(void);
void web_server_run_dsl(const char *source);
void web_server_reset_runtime(void);
int web_server_get_port(void);
bool web_server_is_running(void);

#endif // WEB_SERVER_H
