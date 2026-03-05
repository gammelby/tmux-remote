#ifndef TMUXREMOTE_SERVICE_H_
#define TMUXREMOTE_SERVICE_H_

#include <stdbool.h>

bool tmuxremote_do_install_service(const char* homeDir);
bool tmuxremote_do_uninstall_service(void);
bool tmuxremote_do_service_status(void);

#endif
