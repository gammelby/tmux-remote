#include "nabtoshell_terminal_state.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TERMINAL_DEFAULT_ROWS 48
#define TERMINAL_DEFAULT_COLS 160

static void clear_screen(nabtoshell_terminal_state* state);
static void clear_line_range(nabtoshell_terminal_state* state,
                             int row,
                             int col_start,
                             int col_end);
static void scroll_up(nabtoshell_terminal_state* state);
static int clamp_int(int value, int minimum, int maximum);
static void put_char(nabtoshell_terminal_state* state, unsigned char ch);
static void advance_line(nabtoshell_terminal_state* state);
static void process_csi(nabtoshell_terminal_state* state,
                        const char* params,
                        char final);
static int get_param(const char* params, int index, int default_value);
static bool has_private_mode(const char* params, int mode);

static void init_cells(nabtoshell_terminal_state* state)
{
    size_t count = (size_t)state->rows * (size_t)state->cols;
    state->cells = malloc(count);
    if (state->cells != NULL) {
        memset(state->cells, ' ', count);
    }
}

void nabtoshell_terminal_state_init(nabtoshell_terminal_state* state,
                                    int rows,
                                    int cols)
{
    memset(state, 0, sizeof(*state));

    state->rows = rows > 0 ? rows : TERMINAL_DEFAULT_ROWS;
    state->cols = cols > 0 ? cols : TERMINAL_DEFAULT_COLS;
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->saved_row = 0;
    state->saved_col = 0;
    state->sequence = 0;

    init_cells(state);
}

void nabtoshell_terminal_state_free(nabtoshell_terminal_state* state)
{
    if (state == NULL) {
        return;
    }
    free(state->cells);
    state->cells = NULL;
}

static char* cell_ptr(const nabtoshell_terminal_state* state, int row, int col)
{
    return state->cells + ((size_t)row * (size_t)state->cols + (size_t)col);
}

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

void nabtoshell_terminal_state_resize(nabtoshell_terminal_state* state,
                                      int rows,
                                      int cols)
{
    if (state == NULL) {
        return;
    }

    int new_rows = rows > 0 ? rows : TERMINAL_DEFAULT_ROWS;
    int new_cols = cols > 0 ? cols : TERMINAL_DEFAULT_COLS;

    if (new_rows == state->rows && new_cols == state->cols) {
        return;
    }

    size_t new_count = (size_t)new_rows * (size_t)new_cols;
    char* new_cells = malloc(new_count);
    if (new_cells == NULL) {
        return;
    }
    memset(new_cells, ' ', new_count);

    if (state->cells != NULL) {
        int copy_rows = state->rows < new_rows ? state->rows : new_rows;
        int copy_cols = state->cols < new_cols ? state->cols : new_cols;
        for (int row = 0; row < copy_rows; row++) {
            memcpy(new_cells + (size_t)row * (size_t)new_cols,
                   state->cells + (size_t)row * (size_t)state->cols,
                   (size_t)copy_cols);
        }
        free(state->cells);
    }

    state->cells = new_cells;
    state->rows = new_rows;
    state->cols = new_cols;
    state->cursor_row = clamp_int(state->cursor_row, 0, state->rows - 1);
    state->cursor_col = clamp_int(state->cursor_col, 0, state->cols - 1);
    state->saved_row = clamp_int(state->saved_row, 0, state->rows - 1);
    state->saved_col = clamp_int(state->saved_col, 0, state->cols - 1);
    state->sequence++;
}

static void clear_screen(nabtoshell_terminal_state* state)
{
    if (state->cells == NULL) {
        return;
    }
    memset(state->cells, ' ', (size_t)state->rows * (size_t)state->cols);
}

static void clear_line_range(nabtoshell_terminal_state* state,
                             int row,
                             int col_start,
                             int col_end)
{
    if (row < 0 || row >= state->rows || state->cells == NULL) {
        return;
    }

    col_start = clamp_int(col_start, 0, state->cols - 1);
    col_end = clamp_int(col_end, 0, state->cols - 1);
    if (col_end < col_start) {
        return;
    }

    memset(cell_ptr(state, row, col_start),
           ' ',
           (size_t)(col_end - col_start + 1));
}

static void scroll_up(nabtoshell_terminal_state* state)
{
    if (state->cells == NULL || state->rows <= 1) {
        return;
    }

    size_t row_bytes = (size_t)state->cols;
    memmove(state->cells,
            state->cells + row_bytes,
            row_bytes * (size_t)(state->rows - 1));
    memset(state->cells + row_bytes * (size_t)(state->rows - 1),
           ' ',
           row_bytes);
}

static void advance_line(nabtoshell_terminal_state* state)
{
    state->cursor_col = 0;
    state->cursor_row++;
    if (state->cursor_row >= state->rows) {
        scroll_up(state);
        state->cursor_row = state->rows - 1;
    }
}

static void put_char(nabtoshell_terminal_state* state, unsigned char ch)
{
    if (state->cells == NULL) {
        return;
    }

    state->cursor_row = clamp_int(state->cursor_row, 0, state->rows - 1);
    state->cursor_col = clamp_int(state->cursor_col, 0, state->cols - 1);

    *cell_ptr(state, state->cursor_row, state->cursor_col) = (char)ch;

    state->cursor_col++;
    if (state->cursor_col >= state->cols) {
        advance_line(state);
    }
}

static int parse_int_token(const char* token, int default_value)
{
    if (token == NULL || token[0] == '\0') {
        return default_value;
    }

    char* end = NULL;
    long value = strtol(token, &end, 10);
    if (end == token) {
        return default_value;
    }

    if (value < 0) {
        return default_value;
    }

    if (value > 100000) {
        return 100000;
    }

    return (int)value;
}

static int get_param(const char* params, int index, int default_value)
{
    if (params == NULL || params[0] == '\0') {
        return default_value;
    }

    const char* start = params;
    if (*start == '?') {
        start++;
    }

    int current = 0;
    const char* p = start;
    while (1) {
        const char* sep = strchr(p, ';');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);

        if (current == index) {
            char token[32];
            if (len >= sizeof(token)) {
                len = sizeof(token) - 1;
            }
            memcpy(token, p, len);
            token[len] = '\0';
            return parse_int_token(token, default_value);
        }

        if (sep == NULL) {
            break;
        }
        p = sep + 1;
        current++;
    }

    return default_value;
}

static bool has_private_mode(const char* params, int mode)
{
    if (params == NULL || params[0] != '?') {
        return false;
    }

    const char* p = params + 1;
    while (*p != '\0') {
        const char* sep = strchr(p, ';');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        char token[32];
        if (len >= sizeof(token)) {
            len = sizeof(token) - 1;
        }
        memcpy(token, p, len);
        token[len] = '\0';

        if (parse_int_token(token, -1) == mode) {
            return true;
        }

        if (sep == NULL) {
            break;
        }
        p = sep + 1;
    }

    return false;
}

static void process_csi(nabtoshell_terminal_state* state,
                        const char* params,
                        char final)
{
    switch (final) {
        case 'A': {
            int n = get_param(params, 0, 1);
            state->cursor_row = clamp_int(state->cursor_row - n, 0, state->rows - 1);
            break;
        }
        case 'B': {
            int n = get_param(params, 0, 1);
            state->cursor_row = clamp_int(state->cursor_row + n, 0, state->rows - 1);
            break;
        }
        case 'C': {
            int n = get_param(params, 0, 1);
            state->cursor_col = clamp_int(state->cursor_col + n, 0, state->cols - 1);
            break;
        }
        case 'D': {
            int n = get_param(params, 0, 1);
            state->cursor_col = clamp_int(state->cursor_col - n, 0, state->cols - 1);
            break;
        }
        case 'G': {
            int col = get_param(params, 0, 1);
            state->cursor_col = clamp_int(col - 1, 0, state->cols - 1);
            break;
        }
        case 'd': {
            int row = get_param(params, 0, 1);
            state->cursor_row = clamp_int(row - 1, 0, state->rows - 1);
            break;
        }
        case 'H':
        case 'f': {
            int row = get_param(params, 0, 1);
            int col = get_param(params, 1, 1);
            state->cursor_row = clamp_int(row - 1, 0, state->rows - 1);
            state->cursor_col = clamp_int(col - 1, 0, state->cols - 1);
            break;
        }
        case 'J': {
            int mode = get_param(params, 0, 0);
            if (mode == 2 || mode == 3) {
                clear_screen(state);
                state->cursor_row = 0;
                state->cursor_col = 0;
            } else if (mode == 0) {
                clear_line_range(state, state->cursor_row, state->cursor_col, state->cols - 1);
                for (int row = state->cursor_row + 1; row < state->rows; row++) {
                    clear_line_range(state, row, 0, state->cols - 1);
                }
            }
            break;
        }
        case 'K': {
            int mode = get_param(params, 0, 0);
            if (mode == 0) {
                clear_line_range(state, state->cursor_row, state->cursor_col, state->cols - 1);
            } else if (mode == 1) {
                clear_line_range(state, state->cursor_row, 0, state->cursor_col);
            } else if (mode == 2) {
                clear_line_range(state, state->cursor_row, 0, state->cols - 1);
            }
            break;
        }
        case 'X': {
            int n = get_param(params, 0, 1);
            if (n <= 0) {
                n = 1;
            }
            int end_col = state->cursor_col + n - 1;
            clear_line_range(state,
                             state->cursor_row,
                             state->cursor_col,
                             clamp_int(end_col, 0, state->cols - 1));
            break;
        }
        case 's': {
            state->saved_row = state->cursor_row;
            state->saved_col = state->cursor_col;
            break;
        }
        case 'u': {
            state->cursor_row = clamp_int(state->saved_row, 0, state->rows - 1);
            state->cursor_col = clamp_int(state->saved_col, 0, state->cols - 1);
            break;
        }
        case 'h': {
            if (has_private_mode(params, 1049) ||
                has_private_mode(params, 1047) ||
                has_private_mode(params, 47)) {
                state->alt_screen = true;
                clear_screen(state);
                state->cursor_row = 0;
                state->cursor_col = 0;
            }
            break;
        }
        case 'l': {
            if (has_private_mode(params, 1049) ||
                has_private_mode(params, 1047) ||
                has_private_mode(params, 47)) {
                state->alt_screen = false;
                clear_screen(state);
                state->cursor_row = 0;
                state->cursor_col = 0;
            }
            break;
        }
        case 'm':
        default:
            break;
    }
}

void nabtoshell_terminal_state_feed(nabtoshell_terminal_state* state,
                                    const uint8_t* data,
                                    size_t len)
{
    if (state == NULL || data == NULL || len == 0 || state->cells == NULL) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = data[i];

        if (state->in_escape_charset) {
            state->in_escape_charset = false;
            continue;
        }

        if (state->in_csi) {
            if (ch >= 0x40 && ch <= 0x7E) {
                state->csi_buf[state->csi_len] = '\0';
                process_csi(state, state->csi_buf, (char)ch);
                state->in_csi = false;
                state->csi_len = 0;
                continue;
            }

            if (state->csi_len + 1 < sizeof(state->csi_buf)) {
                state->csi_buf[state->csi_len++] = (char)ch;
            }
            continue;
        }

        if (state->in_escape) {
            state->in_escape = false;
            if (ch == '[') {
                state->in_csi = true;
                state->csi_len = 0;
                continue;
            }
            if (ch == '(' || ch == ')' || ch == '*' || ch == '+' ||
                ch == '-' || ch == '.' || ch == '/') {
                /* Charset designation (e.g. ESC ( B) consumes one more byte. */
                state->in_escape_charset = true;
                continue;
            }
            continue;
        }

        switch (ch) {
            case 0x1B:
                state->in_escape = true;
                break;
            case '\r':
                state->cursor_col = 0;
                break;
            case '\n':
                advance_line(state);
                break;
            case '\b':
                state->cursor_col = clamp_int(state->cursor_col - 1, 0, state->cols - 1);
                break;
            case '\t': {
                int next_tab = ((state->cursor_col / 8) + 1) * 8;
                if (next_tab >= state->cols) {
                    advance_line(state);
                } else {
                    state->cursor_col = next_tab;
                }
                break;
            }
            default:
                if (isprint(ch) || ch >= 0x80) {
                    put_char(state, ch);
                }
                break;
        }
    }

    state->sequence++;
}

static char* trim_row_copy(const nabtoshell_terminal_state* state, int row)
{
    int last_non_space = -1;
    for (int col = 0; col < state->cols; col++) {
        char ch = *cell_ptr(state, row, col);
        if (ch != ' ' && ch != '\0') {
            last_non_space = col;
        }
    }

    size_t len = (size_t)(last_non_space + 1);
    char* line = malloc(len + 1);
    if (line == NULL) {
        return NULL;
    }

    if (len > 0) {
        memcpy(line, cell_ptr(state, row, 0), len);
    }
    line[len] = '\0';

    return line;
}

bool nabtoshell_terminal_state_snapshot(const nabtoshell_terminal_state* state,
                                        nabtoshell_terminal_snapshot* snapshot)
{
    if (state == NULL || snapshot == NULL || state->cells == NULL) {
        return false;
    }

    memset(snapshot, 0, sizeof(*snapshot));

    snapshot->sequence = state->sequence;
    snapshot->rows = state->rows;
    snapshot->cols = state->cols;
    snapshot->cursor_row = state->cursor_row;
    snapshot->cursor_col = state->cursor_col;
    snapshot->alt_screen = state->alt_screen;

    snapshot->lines = calloc((size_t)snapshot->rows, sizeof(char*));
    if (snapshot->lines == NULL) {
        return false;
    }

    for (int row = 0; row < snapshot->rows; row++) {
        snapshot->lines[row] = trim_row_copy(state, row);
        if (snapshot->lines[row] == NULL) {
            nabtoshell_terminal_snapshot_free(snapshot);
            return false;
        }
    }

    return true;
}

void nabtoshell_terminal_snapshot_free(nabtoshell_terminal_snapshot* snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    if (snapshot->lines != NULL) {
        for (int i = 0; i < snapshot->rows; i++) {
            free(snapshot->lines[i]);
        }
        free(snapshot->lines);
    }

    memset(snapshot, 0, sizeof(*snapshot));
}
