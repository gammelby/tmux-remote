#include "tmuxremote.h"

#include <string.h>
#include <stdlib.h>

void tmuxremote_init(struct tmuxremote* app)
{
    memset(app, 0, sizeof(struct tmuxremote));
    tmuxremote_session_map_init(&app->sessionMap);
}

void tmuxremote_deinit(struct tmuxremote* app)
{
    tmuxremote_session_map_deinit(&app->sessionMap);
    if (app->patternConfig) {
        tmuxremote_pattern_config_free(app->patternConfig);
        app->patternConfig = NULL;
    }
    free(app->homeDir);
    free(app->deviceConfigFile);
    free(app->deviceKeyFile);
    free(app->iamStateFile);
    free(app->recordPtyFile);
    app->homeDir = NULL;
    app->deviceConfigFile = NULL;
    app->deviceKeyFile = NULL;
    app->iamStateFile = NULL;
    app->recordPtyFile = NULL;
}
