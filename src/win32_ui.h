#ifndef WIN32_UI_H
#define WIN32_UI_H

#include "common.h"
#include "runtime.h"

// Starts the interactive Win32 Console SCADA & Log Hub UI
// Runs the web server in the background and provides an interactive terminal UI
int win32_ui_run(int port, const char *web_root);

#endif // WIN32_UI_H
