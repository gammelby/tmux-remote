#ifndef PE_TERMINAL_STATE_H_
#define PE_TERMINAL_STATE_H_

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
} pe_terminal_snapshot;

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
} pe_terminal_state;

void pe_terminal_state_init(pe_terminal_state* state,
                            int rows,
                            int cols);

void pe_terminal_state_free(pe_terminal_state* state);

void pe_terminal_state_resize(pe_terminal_state* state,
                              int rows,
                              int cols);

void pe_terminal_state_feed(pe_terminal_state* state,
                            const uint8_t* data,
                            size_t len);

bool pe_terminal_state_snapshot(const pe_terminal_state* state,
                                pe_terminal_snapshot* snapshot);

void pe_terminal_snapshot_free(pe_terminal_snapshot* snapshot);

#endif
