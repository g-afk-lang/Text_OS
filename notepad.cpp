#include "notepad.h"
#include "terminal_hooks.h"
#include "terminal_io.h"
#include "iostream_wrapper.h"
#include "interrupts.h"

// --- NOTEPAD CONSTANTS ---
#define MAX_LINES 100
#define MAX_VISIBLE_LINES 20
#define MAX_LINE_LENGTH 79
#define NOTEPAD_START_ROW 3
#define NOTEPAD_END_ROW (NOTEPAD_START_ROW + MAX_VISIBLE_LINES - 1)

// --- NOTEPAD STATE ---
static bool notepad_running = false;
static char notepad_filename[256];
static char notepad_buffer[MAX_LINES][MAX_LINE_LENGTH + 1];
static int cursor_row = 0;
static int cursor_col = 0;
static int current_line_count = 1;
static char current_filename[32] = "";

// Scrolling state
static int scroll_offset = 0;
static int visible_lines = MAX_VISIBLE_LINES;

// --- MISSING FORWARD DECLARATIONS ---
extern bool extended_key;
extern int input_length;
extern bool is_pong_running();
extern uint64_t ahci_base;
extern bool fat32_init(uint64_t ahci_base, int port);
extern int fat32_write_file(uint64_t ahci_base, int port, const char* filename, const void* data, uint32_t size);
extern int fat32_read_file_to_buffer(uint64_t ahci_base, int port, const char* filename, void* data_buffer, uint32_t buffer_size);

// VGA text mode cursor functions (inline implementations)
static void notepad_set_cursor_position(int row, int col) {
    uint16_t pos = row * 80 + col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void notepad_show_cursor() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | 14);
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);
}

static void notepad_hide_cursor() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

// VGA text buffer direct access
static volatile uint16_t* vga_buffer = (volatile uint16_t*)0xB8000;

static void notepad_write_char_at(int row, int col, char c, uint8_t color) {
    if (row >= 0 && row < 25 && col >= 0 && col < 80) {
        vga_buffer[row * 80 + col] = (uint16_t)c | ((uint16_t)color << 8);
    }
}

static void notepad_write_string_at(int row, int col, const char* str, uint8_t color) {
    int i = 0;
    while (str[i] != '\0' && col + i < 80) {
        notepad_write_char_at(row, col + i, str[i], color);
        i++;
    }
}

static void notepad_clear_line(int row, uint8_t color) {
    for (int i = 0; i < 80; i++) {
        notepad_write_char_at(row, i, ' ', color);
    }
}

// --- UTILITY FUNCTIONS ---
static inline int simple_strlen(const char* str) {
    int len = 0;
    while (str[len] != '\0') len++;
    return len;
}

static inline void simple_strcpy(char* dest, const char* src) {
    int i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline void simple_strcat(char* dest, const char* src) {
    char* ptr = dest + simple_strlen(dest);
    while (*src != '\0') *ptr++ = *src++;
    *ptr = '\0';
}

static void int_to_string(int num, char* str) {
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    int i = 0;
    bool is_neg = num < 0;
    if (is_neg) num = -num;

    while (num > 0) {
        str[i++] = '0' + (num % 10);
        num /= 10;
    }
    if (is_neg) str[i++] = '-';
    str[i] = '\0';
    // Reverse the string
    for (int j = 0; j < i / 2; j++) {
        char temp = str[j];
        str[j] = str[i - 1 - j];
        str[i - 1 - j] = temp;
    }
}


// --- SCROLLING AND DRAWING ---
void notepad_draw_interface(); // Forward declaration

void notepad_ensure_cursor_visible() {
    // Scroll up if cursor is above visible area
    if (cursor_row < scroll_offset) {
        scroll_offset = cursor_row;
        notepad_draw_interface();
    }
    // Scroll down if cursor is below visible area
    else if (cursor_row >= scroll_offset + visible_lines) {
        scroll_offset = cursor_row - visible_lines + 1;
        notepad_draw_interface();
    }
}

void notepad_draw_interface() {
    // Clear screen with direct VGA access
    for (int row = 0; row < 25; row++) {
        notepad_clear_line(row, 0x07);
    }

    // Draw title bar
    notepad_write_string_at(0, 0, "=== NOTEPAD === ", 0x0F);
    if (current_filename[0] != '\0') {
        notepad_write_string_at(0, 16, "File: ", 0x0F);
        notepad_write_string_at(0, 22, current_filename, 0x0F);
    } else {
        notepad_write_string_at(0, 16, "New File", 0x0F);
    }

    // Show scroll position indicator
    if (current_line_count > visible_lines) {
        char scroll_info[32];
        simple_strcpy(scroll_info, " Lines: ");
        char line_num[8];
        int_to_string(scroll_offset + 1, line_num);
        simple_strcat(scroll_info, line_num);
        simple_strcat(scroll_info, "-");
        int_to_string(scroll_offset + visible_lines, line_num);
        simple_strcat(scroll_info, line_num);
        simple_strcat(scroll_info, "/");
        int_to_string(current_line_count, line_num);
        simple_strcat(scroll_info, line_num);
        notepad_write_string_at(0, 50, scroll_info, 0x0F);
    }

    // Draw help line
    notepad_write_string_at(1, 0, "ESC: Save & Exit | Arrows: Move | PgUp/PgDn: Scroll", 0x07);
    // Draw separator
    for (int i = 0; i < 80; i++) {
        notepad_write_char_at(2, i, '-', 0x07);
    }

    // Draw line numbers and content based on scroll offset
    for (int i = 0; i < visible_lines; i++) {
        int buffer_line = scroll_offset + i;
        if (buffer_line < current_line_count) {
            char line_num_str[5];
            int_to_string(buffer_line + 1, line_num_str);
            notepad_write_string_at(NOTEPAD_START_ROW + i, 0, "   ", 0x08); // Clear area
            notepad_write_string_at(NOTEPAD_START_ROW + i, 3 - simple_strlen(line_num_str), line_num_str, 0x08);
            notepad_write_char_at(NOTEPAD_START_ROW + i, 3, '|', 0x08);
            notepad_write_string_at(NOTEPAD_START_ROW + i, 4, notepad_buffer[buffer_line], 0x07);
        } else {
            // Empty line beyond content
            notepad_write_string_at(NOTEPAD_START_ROW + i, 0, " ~ |", 0x08);
        }
    }
}


void notepad_update_cursor() {
    // Ensure cursor is visible
    notepad_ensure_cursor_visible();
    // Position cursor at current editing position (relative to screen)
    int screen_row = cursor_row - scroll_offset;
    notepad_set_cursor_position(NOTEPAD_START_ROW + screen_row, 4 + cursor_col);
    notepad_show_cursor();
}

void notepad_redraw_current_line() {
    int screen_row = cursor_row - scroll_offset;
    // Only redraw if the current line is visible
    if (screen_row >= 0 && screen_row < visible_lines) {
        // Clear the content area of the current line
        for (int i = 4; i < 80; i++) {
            notepad_write_char_at(NOTEPAD_START_ROW + screen_row, i, ' ', 0x07);
        }
        // Redraw the line content
        notepad_write_string_at(NOTEPAD_START_ROW + screen_row, 4, notepad_buffer[cursor_row], 0x07);
    }
}

// --- NOTEPAD CORE LOGIC ---
bool is_notepad_running() {
    return notepad_running;
}

void notepad_clear_buffer() {
    for (int i = 0; i < MAX_LINES; i++) {
        notepad_buffer[i][0] = '\0';
    }
    cursor_row = 0;
    cursor_col = 0;
    current_line_count = 1;
    scroll_offset = 0;
}

void notepad_insert_char(char c) {
    if (cursor_col >= MAX_LINE_LENGTH) return;
    char* line = notepad_buffer[cursor_row];
    int line_len = simple_strlen(line);

    if (line_len < MAX_LINE_LENGTH) {
        // Shift characters to the right
        for (int i = line_len; i >= cursor_col; i--) {
            line[i + 1] = line[i];
        }
        // Insert new character
        line[cursor_col] = c;
        cursor_col++;
        notepad_redraw_current_line();
    }
}

void notepad_delete_char() { // Backspace
    if (cursor_col > 0) {
        // Normal backspace within a line
        char* line = notepad_buffer[cursor_row];
        int line_len = simple_strlen(line);
        cursor_col--;
        for (int i = cursor_col; i < line_len; i++) {
            line[i] = line[i + 1];
        }
        notepad_redraw_current_line();
    } else if (cursor_row > 0) {
        // At beginning of line, merge with previous line
        char* prev_line = notepad_buffer[cursor_row - 1];
        char* curr_line = notepad_buffer[cursor_row];
        int prev_len = simple_strlen(prev_line);
        int curr_len = simple_strlen(curr_line);

        if (prev_len + curr_len < MAX_LINE_LENGTH) {
            simple_strcat(prev_line, curr_line);
            // Shift all subsequent lines up
            for (int i = cursor_row; i < current_line_count - 1; i++) {
                simple_strcpy(notepad_buffer[i], notepad_buffer[i + 1]);
            }
            notepad_buffer[current_line_count - 1][0] = '\0';
            current_line_count--;
            cursor_row--;
            cursor_col = prev_len;
            notepad_draw_interface();
        }
    }
}

void notepad_new_line() { // Enter
    if (current_line_count >= MAX_LINES) return;

    char* curr_line = notepad_buffer[cursor_row];
    char temp[MAX_LINE_LENGTH + 1];
    simple_strcpy(temp, &curr_line[cursor_col]);
    curr_line[cursor_col] = '\0';

    // Shift lines down to make room for the new line
    for (int i = current_line_count; i > cursor_row + 1; i--) {
        simple_strcpy(notepad_buffer[i], notepad_buffer[i - 1]);
    }
    simple_strcpy(notepad_buffer[cursor_row + 1], temp);

    current_line_count++;
    cursor_row++;
    cursor_col = 0;
    notepad_draw_interface();
}

void notepad_move_cursor(int delta_row, int delta_col) {
    cursor_row += delta_row;
    cursor_col += delta_col;

    // Clamp row
    if (cursor_row < 0) cursor_row = 0;
    if (cursor_row >= current_line_count) cursor_row = current_line_count - 1;

    // Clamp column
    int line_len = simple_strlen(notepad_buffer[cursor_row]);
    if (cursor_col > line_len) cursor_col = line_len;
    if (cursor_col < 0) cursor_col = 0;
    if (cursor_col > MAX_LINE_LENGTH) cursor_col = MAX_LINE_LENGTH;
}

void notepad_save_and_exit(const char* filename_arg) {
    char final_filename[256];
    if (filename_arg && filename_arg[0] != '\0') {
        simple_strcpy(final_filename, filename_arg);
    } else if (current_filename[0] != '\0') {
        simple_strcpy(final_filename, current_filename);
    } else {
        simple_strcpy(final_filename, "untitled.txt");
    }

    char save_buffer[MAX_LINES * (MAX_LINE_LENGTH + 1)];
    save_buffer[0] = '\0';
    for (int i = 0; i < current_line_count; i++) {
        simple_strcat(save_buffer, notepad_buffer[i]);
        if (i < current_line_count - 1) {
            simple_strcat(save_buffer, "\n");
        }
    }

    int result = fat32_write_file(ahci_base, 0, final_filename, save_buffer, simple_strlen(save_buffer));
    notepad_write_string_at(24, 0, "                                                  ", 0x07);
    if (result == 0) {
        notepad_write_string_at(24, 0, "File saved. Press any key.", 0x0A);
    } else {
        notepad_write_string_at(24, 0, "Error saving! Press any key.", 0x0C);
    }

    // Wait for a key press before exiting
    while(inb(0x64) & 0x01) inb(0x60); // Clear keyboard buffer
    while(!(inb(0x64) & 0x01));      // Wait for new key press
    inb(0x60); // Discard the key

    notepad_running = false;
    terminal_clear_screen();
    terminal_draw_header();
    // This is where you would restore the terminal's state if needed
}

void notepad_load_file(const char* filename) {
    notepad_clear_buffer();
    char load_buffer[MAX_LINES * (MAX_LINE_LENGTH + 1)];
    int bytes_read = fat32_read_file_to_buffer(ahci_base, 0, filename, load_buffer, sizeof(load_buffer) - 1);

    if (bytes_read >= 0) {
        load_buffer[bytes_read] = '\0';
        simple_strcpy(current_filename, filename);
        int line = 0;
        int col = 0;
        for (int i = 0; i < bytes_read && line < MAX_LINES; i++) {
            char c = load_buffer[i];
            if (c == '\n') {
                notepad_buffer[line][col] = '\0';
                line++;
                col = 0;
            } else if (c != '\r' && col < MAX_LINE_LENGTH) {
                notepad_buffer[line][col++] = c;
            }
        }
        notepad_buffer[line][col] = '\0';
        current_line_count = line + 1;
    } else {
        // File not found or error, start with a blank slate but keep the name
        simple_strcpy(current_filename, filename);
    }
}


void notepad_handle_input(char key) {
    if (!notepad_running) return;
    switch (key) {
        case '\n': notepad_new_line(); break;
        case '\b': notepad_delete_char(); break;
        default:
            if (key >= 32 && key <= 126) {
                notepad_insert_char(key);
            }
            break;
    }
    notepad_update_cursor();
}

void notepad_handle_special_key(int scancode) {
    if (!notepad_running) return;
    switch (scancode) {
        case 0x48: // Up arrow
            notepad_move_cursor(-1, 0);
            break;
        case 0x50: // Down arrow
            notepad_move_cursor(1, 0);
            break;
        case 0x4B: // Left arrow
            notepad_move_cursor(0, -1);
            break;
        case 0x4D: // Right arrow
            notepad_move_cursor(0, 1);
            break;
        case 0x47: // Home
            cursor_col = 0;
            break;
        case 0x4F: // End
            cursor_col = simple_strlen(notepad_buffer[cursor_row]);
            break;
        
        // --- CORRECTED SCROLL LOGIC ---
        case 0x49: // Page Up
            cursor_row -= visible_lines;
            if (cursor_row < 0) cursor_row = 0;
            // No need to call redraw, notepad_update_cursor will handle it
            break;
        case 0x51: // Page Down
            cursor_row += visible_lines;
            if (cursor_row >= current_line_count) cursor_row = current_line_count - 1;
            // No need to call redraw, notepad_update_cursor will handle it
            break;

        case 0x01: // ESC
            notepad_save_and_exit(notepad_filename);
            return; // Exit immediately, don't update cursor
    }
    // Adjust column if new line is shorter
    int line_len = simple_strlen(notepad_buffer[cursor_row]);
    if (cursor_col > line_len) {
        cursor_col = line_len;
    }

    notepad_update_cursor();
}


void start_notepad(const char* filename) {
    notepad_running = true;
    notepad_hide_cursor(); // Hide terminal cursor
    if (filename && filename[0] != '\0') {
        notepad_load_file(filename);
    } else {
        notepad_clear_buffer();
        current_filename[0] = '\0';
    }
    notepad_draw_interface();
    notepad_update_cursor();
}

void cmd_notepad(const char* filename) {
    simple_strcpy(notepad_filename, filename);
    start_notepad(filename);
}
