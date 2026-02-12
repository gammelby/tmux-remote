#include "nabtoshell.h"

#include <string.h>
#include <stdlib.h>

void nabtoshell_init(struct nabtoshell* app)
{
    memset(app, 0, sizeof(struct nabtoshell));
    nabtoshell_session_map_init(&app->sessionMap);
}

void nabtoshell_deinit(struct nabtoshell* app)
{
    if (app->patternConfig) {
        nabtoshell_pattern_config_free(app->patternConfig);
        app->patternConfig = NULL;
    }
    free(app->homeDir);
    free(app->deviceConfigFile);
    free(app->deviceKeyFile);
    free(app->iamStateFile);
    app->homeDir = NULL;
    app->deviceConfigFile = NULL;
    app->deviceKeyFile = NULL;
    app->iamStateFile = NULL;
}
