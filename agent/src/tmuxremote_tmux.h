#ifndef TMUXREMOTE_TMUX_H_
#define TMUXREMOTE_TMUX_H_

#include <stdbool.h>
#include <stdint.h>

#define TMUXREMOTE_TMUX_MAX_SESSIONS 32

struct tmuxremote_tmux_session {
    char name[64];
    uint16_t cols;
    uint16_t rows;
    int attached;
};

struct tmuxremote_tmux_list {
    struct tmuxremote_tmux_session sessions[TMUXREMOTE_TMUX_MAX_SESSIONS];
    int count;
};

bool tmuxremote_tmux_list_sessions(struct tmuxremote_tmux_list* list);
bool tmuxremote_tmux_session_exists(const char* name);
bool tmuxremote_tmux_create_session(const char* name, uint16_t cols,
                                    uint16_t rows, const char* command);
bool tmuxremote_tmux_validate_session_name(const char* name);

#endif
