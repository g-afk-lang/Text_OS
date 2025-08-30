#include "notepad.h"
#include "terminal_hooks.h"
#include "terminal_io.h"
#include "iostream_wrapper.h"
#include "interrupts.h"

// --- NOTEPAD CONSTANTS ---
#define MAX_LINES 10000
#define MAX_VISIBLE_LINES 20
#define MAX_LINE_LENGTH 79
#define NOTEPAD_START_ROW 3
#define NOTEPAD_END_ROW (NOTEPAD_START_ROW + MAX_VISIBLE_LINES - 1)
#define TEXT_AREA_WIDTH 75  // Available width for text (80 - line number area)

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

// Text wrapping state
static bool word_wrap_enabled = true;

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
    while (num > 0) {
        str[i++] = '0' + (num % 10);
        num /= 10;
    }
    str[i] = '\0';
    
    // Reverse the string
    for (int j = 0; j < i / 2; j++) {
        char temp = str[j];
        str[j] = str[i - 1 - j];
        str[i - 1 - j] = temp;
    }
}

// --- TEXT WRAPPING FUNCTIONS ---
static bool is_word_boundary(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\0');
}

static int find_wrap_position(const char* line, int max_width) {
    if (simple_strlen(line) <= max_width) {
        return simple_strlen(line);
    }
    
    // Try to find a word boundary within the line
    for (int i = max_width; i > 0; i--) {
        if (is_word_boundary(line[i])) {
            return i;
        }
    }
    
    // No word boundary found, force wrap at max_width
    return max_width;
}

static void wrap_line_if_needed() {
    if (!word_wrap_enabled) return;
    
    char* current_line = notepad_buffer[cursor_row];
    int line_len = simple_strlen(current_line);
    
    if (line_len > TEXT_AREA_WIDTH && current_line_count < MAX_LINES) {
        // Find the best wrap position
        int wrap_pos = find_wrap_position(current_line, TEXT_AREA_WIDTH);
        
        // Skip leading spaces on the wrapped part
        int next_start = wrap_pos;
        while (current_line[next_start] == ' ' && next_start < line_len) {
            next_start++;
        }
        
        // Create the wrapped line
        char wrapped_text[MAX_LINE_LENGTH + 1];
        simple_strcpy(wrapped_text, &current_line[next_start]);
        
        // Truncate current line
        current_line[wrap_pos] = '\0';
        
        // Shift all lines down to make room
        for (int i = MAX_LINES - 1; i > cursor_row + 1; i--) {
            simple_strcpy(notepad_buffer[i], notepad_buffer[i - 1]);
        }
        
        // Insert wrapped text as new line
        simple_strcpy(notepad_buffer[cursor_row + 1], wrapped_text);
        current_line_count++;
        
        // Adjust cursor position if it was beyond the wrap point
        if (cursor_col > wrap_pos) {
            cursor_row++;
            cursor_col = cursor_col - next_start;
        }
        
        // Recursively wrap the new line if it's still too long
        if (simple_strlen(wrapped_text) > TEXT_AREA_WIDTH) {
            int old_row = cursor_row;
            int old_col = cursor_col;
            cursor_row++;
            cursor_col = simple_strlen(wrapped_text);
            wrap_line_if_needed();
            cursor_row = old_row;
            cursor_col = old_col;
        }
    }
}

// --- SCROLLING FUNCTIONS ---
void notepad_scroll_up() {
    if (scroll_offset > 0) {
        scroll_offset--;
        notepad_draw_interface();
    }
}

void notepad_scroll_down() {
    int max_scroll = current_line_count - visible_lines;
    if (max_scroll < 0) max_scroll = 0;
    
    if (scroll_offset < max_scroll) {
        scroll_offset++;
        notepad_draw_interface();
    }
}

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

// --- NOTEPAD FUNCTIONS ---
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
    
    // Show scroll position and wrap status
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
        notepad_write_string_at(0, 45, scroll_info, 0x0F);
    }
    
    // Show wrap status
    if (word_wrap_enabled) {
        notepad_write_string_at(0, 70, "[WRAP ON]", 0x0A);
    } else {
        notepad_write_string_at(0, 70, "[WRAP OFF]", 0x0C);
    }
    
    // Draw help line
    notepad_write_string_at(1, 0, "ESC:Save | Arrows:Move | PgUp/Dn:Scroll | F2:Toggle Wrap", 0x07);
    
    // Draw separator
    for (int i = 0; i < 80; i++) {
        notepad_write_char_at(2, i, '-', 0x07);
    }
    
    // Draw line numbers and content based on scroll offset
    for (int i = 0; i < visible_lines; i++) {
        int buffer_line = scroll_offset + i;
        
        if (buffer_line < current_line_count) {
            // Line number (show actual line number, not screen line)
            char line_num[4];
            int_to_string(buffer_line + 1, line_num);
            
            if (buffer_line < 9) {
                notepad_write_char_at(NOTEPAD_START_ROW + i, 0, ' ', 0x08);
                notepad_write_string_at(NOTEPAD_START_ROW + i, 1, line_num, 0x08);
            } else if (buffer_line < 99) {
                notepad_write_string_at(NOTEPAD_START_ROW + i, 0, line_num, 0x08);
            } else {
                // For 3+ digit line numbers, just show the number
                notepad_write_string_at(NOTEPAD_START_ROW + i, 0, line_num, 0x08);
            }
            notepad_write_char_at(NOTEPAD_START_ROW + i, 3, '|', 0x08);
            
            // Content - truncate if word wrap is off and line is too long
            if (!word_wrap_enabled && simple_strlen(notepad_buffer[buffer_line]) > TEXT_AREA_WIDTH) {
                char truncated_line[TEXT_AREA_WIDTH + 1];
                for (int j = 0; j < TEXT_AREA_WIDTH; j++) {
                    truncated_line[j] = notepad_buffer[buffer_line][j];
                }
                truncated_line[TEXT_AREA_WIDTH] = '\0';
                notepad_write_string_at(NOTEPAD_START_ROW + i, 4, truncated_line, 0x07);
                notepad_write_char_at(NOTEPAD_START_ROW + i, 79, '>', 0x0C); // Indicate truncation
            } else {
                notepad_write_string_at(NOTEPAD_START_ROW + i, 4, notepad_buffer[buffer_line], 0x07);
            }
        } else {
            // Empty line beyond content
            notepad_write_string_at(NOTEPAD_START_ROW + i, 0, "   |", 0x08);
        }
    }
}

void notepad_update_cursor() {
    // Ensure cursor is visible
    notepad_ensure_cursor_visible();
    
    // Position cursor at current editing position (relative to screen)
    int screen_row = cursor_row - scroll_offset;
    int display_col = cursor_col;
    
    // If word wrap is off and cursor is beyond visible area, show it at the edge
    if (!word_wrap_enabled && cursor_col >= TEXT_AREA_WIDTH) {
        display_col = TEXT_AREA_WIDTH - 1;
    }
    
    notepad_set_cursor_position(NOTEPAD_START_ROW + screen_row, 4 + display_col);
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
        if (!word_wrap_enabled && simple_strlen(notepad_buffer[cursor_row]) > TEXT_AREA_WIDTH) {
            char truncated_line[TEXT_AREA_WIDTH + 1];
            for (int j = 0; j < TEXT_AREA_WIDTH; j++) {
                truncated_line[j] = notepad_buffer[cursor_row][j];
            }
            truncated_line[TEXT_AREA_WIDTH] = '\0';
            notepad_write_string_at(NOTEPAD_START_ROW + screen_row, 4, truncated_line, 0x07);
            notepad_write_char_at(NOTEPAD_START_ROW + screen_row, 79, '>', 0x0C);
        } else {
            notepad_write_string_at(NOTEPAD_START_ROW + screen_row, 4, notepad_buffer[cursor_row], 0x07);
        }
    }
}

void notepad_insert_char(char c) {
    if (cursor_col >= MAX_LINE_LENGTH - 1) return; // Line full
    
    char* line = notepad_buffer[cursor_row];
    int line_len = simple_strlen(line);
    
    // Shift characters to the right
    for (int i = line_len; i > cursor_col; i--) {
        line[i] = line[i - 1];
    }
    
    // Insert new character
    line[cursor_col] = c;
    if (cursor_col >= line_len) {
        line[cursor_col + 1] = '\0';
    }
    
    cursor_col++;
    
    // Check for word wrapping after insertion
    wrap_line_if_needed();
    
    // Redraw screen if wrapping occurred, otherwise just redraw current line
    if (word_wrap_enabled && simple_strlen(line) > TEXT_AREA_WIDTH) {
        notepad_draw_interface();
    } else {
        notepad_redraw_current_line();
    }
}

void notepad_delete_char() {
    if (cursor_col == 0) {
        // At beginning of line - try to merge with previous line
        if (cursor_row > 0) {
            char* prev_line = notepad_buffer[cursor_row - 1];
            char* curr_line = notepad_buffer[cursor_row];
            
            int prev_len = simple_strlen(prev_line);
            int curr_len = simple_strlen(curr_line);
            
            // Check if we can fit current line into previous line
            if (prev_len + curr_len < MAX_LINE_LENGTH) {
                // Concatenate lines
                simple_strcat(prev_line, curr_line);
                
                // Shift all lines up
                for (int i = cursor_row; i < MAX_LINES - 1; i++) {
                    simple_strcpy(notepad_buffer[i], notepad_buffer[i + 1]);
                }
                notepad_buffer[MAX_LINES - 1][0] = '\0';
                
                // Update cursor position
                cursor_row--;
                cursor_col = prev_len;
                
                if (current_line_count > 1) {
                    current_line_count--;
                }
                
                // Check for wrapping on the merged line
                wrap_line_if_needed();
                
                // Redraw screen
                notepad_draw_interface();
            }
        }
        return;
    }
    
    char* line = notepad_buffer[cursor_row];
    int line_len = simple_strlen(line);
    
    if (cursor_col > line_len) {
        cursor_col = line_len;
        return;
    }
    
    // Shift characters to the left
    for (int i = cursor_col - 1; i < line_len; i++) {
        line[i] = line[i + 1];
    }
    
    cursor_col--;
    
    // Redraw current line
    notepad_redraw_current_line();
}

void notepad_new_line() {
    if (current_line_count >= MAX_LINES) return; // Can't add more lines
    
    char* curr_line = notepad_buffer[cursor_row];
    int line_len = simple_strlen(curr_line);
    
    // Split current line if cursor is in the middle
    if (cursor_col < line_len) {
        // Move everything from cursor position to new line
        char temp[MAX_LINE_LENGTH + 1];
        simple_strcpy(temp, &curr_line[cursor_col]);
        curr_line[cursor_col] = '\0';
        
        // Shift all lines down to make room
        for (int i = MAX_LINES - 1; i > cursor_row + 1; i--) {
            simple_strcpy(notepad_buffer[i], notepad_buffer[i - 1]);
        }
        
        // Insert the split part as new line
        simple_strcpy(notepad_buffer[cursor_row + 1], temp);
    } else {
        // Just shift lines down
        for (int i = MAX_LINES - 1; i > cursor_row + 1; i--) {
            simple_strcpy(notepad_buffer[i], notepad_buffer[i - 1]);
        }
        notepad_buffer[cursor_row + 1][0] = '\0';
    }
    
    cursor_row++;
    cursor_col = 0;
    current_line_count++;
    
    // Redraw screen
    notepad_draw_interface();
}

void notepad_move_cursor(int delta_row, int delta_col) {
    int new_row = cursor_row + delta_row;
    int new_col = cursor_col + delta_col;
    
    // Clamp row
    if (new_row < 0) new_row = 0;
    if (new_row >= current_line_count) new_row = current_line_count - 1;
    if (new_row >= MAX_LINES) new_row = MAX_LINES - 1;
    
    // Clamp column based on line length
    int line_len = simple_strlen(notepad_buffer[new_row]);
    if (new_col < 0) new_col = 0;
    if (new_col > line_len) new_col = line_len;
    if (new_col > MAX_LINE_LENGTH - 1) new_col = MAX_LINE_LENGTH - 1;
    
    cursor_row = new_row;
    cursor_col = new_col;
}

void notepad_toggle_word_wrap() {
    word_wrap_enabled = !word_wrap_enabled;
    notepad_draw_interface();  // Redraw to show new wrap status
}

void notepad_save_and_exit(const char* filename_arg) {
    char final_filename[256];
    
    // 1. Prioritize the filename passed to this function (if any).
    if (filename_arg && filename_arg[0] != '\0') {
        simple_strcpy(final_filename, filename_arg);
    } 
    // 2. Otherwise, use the filename from when the file was opened.
    else if (current_filename[0] != '\0') {
        simple_strcpy(final_filename, current_filename);
    } 
    // 3. As a last resort, create a default filename.
    else {
        simple_strcpy(final_filename, "untitled.txt");
    }
    
    // Convert buffer to a single string for saving
    char save_buffer[MAX_LINES * (MAX_LINE_LENGTH + 1) + 1];
    save_buffer[0] = '\0';
    
    for (int i = 0; i < current_line_count; i++) {
        simple_strcat(save_buffer, notepad_buffer[i]);
        if (i < current_line_count - 1) {
            simple_strcat(save_buffer, "\n");
        }
    }
    
    // Save to file using the now-guaranteed-valid filename
    int result = fat32_write_file(ahci_base, 0, final_filename, save_buffer, simple_strlen(save_buffer));
    
    // Show save result
    notepad_write_string_at(24, 0, "                                                                             ", 0x07);
    if (result == 0) {
        notepad_write_string_at(24, 0, "File saved successfully! Press any key to continue...", 0x0A);
    } else {
        notepad_write_string_at(24, 0, "Error saving file! Press any key to continue...", 0x0C);
    }
    
    notepad_running = false;
}

void notepad_load_file(const char* filename) {
    char load_buffer[MAX_LINES * (MAX_LINE_LENGTH + 1) + 1];
    
    int result = fat32_read_file_to_buffer(ahci_base, 0, filename, load_buffer, sizeof(load_buffer) - 1);
    
    if (result >= 0) {
        // Parse loaded content into lines
        notepad_clear_buffer();
        simple_strcpy(current_filename, filename);
        
        int line_idx = 0;
        int char_idx = 0;
        
        for (int i = 0; i < result && line_idx < MAX_LINES; i++) {
            if (load_buffer[i] == '\n' || load_buffer[i] == '\r') {
                notepad_buffer[line_idx][char_idx] = '\0';
                
                // Apply word wrapping to loaded line if enabled
                if (word_wrap_enabled && char_idx > TEXT_AREA_WIDTH) {
                    int old_cursor_row = cursor_row;
                    int old_cursor_col = cursor_col;
                    cursor_row = line_idx;
                    cursor_col = char_idx;
                    wrap_line_if_needed();
                    cursor_row = old_cursor_row;
                    cursor_col = old_cursor_col;
                }
                
                line_idx++;
                char_idx = 0;
            } else if (char_idx < MAX_LINE_LENGTH - 1) {
                notepad_buffer[line_idx][char_idx] = load_buffer[i];
                char_idx++;
            }
        }
        
        if (char_idx > 0) {
            notepad_buffer[line_idx][char_idx] = '\0';
            
            // Apply word wrapping to last line if enabled
            if (word_wrap_enabled && char_idx > TEXT_AREA_WIDTH) {
                int old_cursor_row = cursor_row;
                int old_cursor_col = cursor_col;
                cursor_row = line_idx;
                cursor_col = char_idx;
                wrap_line_if_needed();
                cursor_row = old_cursor_row;
                cursor_col = old_cursor_col;
            }
            
            line_idx++;
        }
        
        current_line_count = line_idx > 0 ? line_idx : 1;
        
        // Start at position 0,0
        cursor_row = 0;
        cursor_col = 0;
        scroll_offset = 0;
    }
}

void notepad_handle_input(char key) {
    if (!notepad_running) return;
    
    switch (key) {
        case '\n': // Enter
            notepad_new_line();
            break;
            
        case '\b': // Backspace
            notepad_delete_char();
            break;
            
        default:
            // Regular character input
            if (key >= 32 && key <= 126) { // Printable characters
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
            
        // Scroll keys
        case 0x49: // Page Up
            notepad_scroll_up();
            break;
            
        case 0x51: // Page Down
            notepad_scroll_down();
            break;
            
        // Toggle word wrap
        case 0x3C: // F2
            notepad_toggle_word_wrap();
            break;
            
        case 0x01: // ESC
            notepad_save_and_exit(notepad_filename);
            return;
    }
    
    notepad_update_cursor();
}

void start_notepad(const char* filename) {
    notepad_running = true;
    notepad_clear_buffer();
    
    if (filename && filename[0] != '\0') {
        notepad_load_file(filename);
    } else {
        current_filename[0] = '\0';
        // Start at position 0,0 for new files too
        cursor_row = 0;
        cursor_col = 0;
        scroll_offset = 0;
    }
    
    notepad_draw_interface();
    notepad_update_cursor();
}

void cmd_notepad(const char* filename) {
    simple_strcpy(notepad_filename, filename);
    start_notepad(filename);
}

