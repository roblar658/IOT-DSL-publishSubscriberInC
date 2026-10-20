#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "common.h"
#include "runtime.h"

int web_server_start(int port, const char *web_root);
void web_server_stop(void);

#endif // WEB_SERVER_H
