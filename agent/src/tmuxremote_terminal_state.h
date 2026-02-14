#ifndef TMUXREMOTE_TERMINAL_STATE_H_
#define TMUXREMOTE_TERMINAL_STATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t sequence;
    int rows;
    int cols;
    int cursor_row;
    int cursor_col;
    bool alt_screen;
    char** lines;
} tmuxremote_terminal_snapshot;

typedef struct {
    uint64_t sequence;
    int rows;
    int cols;
    int cursor_row;
    int cursor_col;
    int saved_row;
    int saved_col;
    bool alt_screen;

    char* cells;

    bool in_escape;
    bool in_escape_charset;
    bool in_csi;
    char csi_buf[128];
    size_t csi_len;
} tmuxremote_terminal_state;

void tmuxremote_terminal_state_init(tmuxremote_terminal_state* state,
                                    int rows,
                                    int cols);

void tmuxremote_terminal_state_free(tmuxremote_terminal_state* state);

void tmuxremote_terminal_state_resize(tmuxremote_terminal_state* state,
                                      int rows,
                                      int cols);

void tmuxremote_terminal_state_feed(tmuxremote_terminal_state* state,
                                    const uint8_t* data,
                                    size_t len);

bool tmuxremote_terminal_state_snapshot(const tmuxremote_terminal_state* state,
                                        tmuxremote_terminal_snapshot* snapshot);

void tmuxremote_terminal_snapshot_free(tmuxremote_terminal_snapshot* snapshot);

#endif
