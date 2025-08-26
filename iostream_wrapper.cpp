#include "iostream_wrapper.h"
#include "stdlib_hooks.h"

// Global instances
TerminalOutput cout;
TerminalInput cin;

// Add this implementation for the new operator
TerminalOutput& TerminalOutput::operator<<(TerminalOutput& (*manip)(TerminalOutput&)) {
    return manip(*this);
}

// Implementation of TerminalOutput methods
TerminalOutput::TerminalOutput() {
    // Initialize scrollback buffer
    memset(scrollback_buffer, 0, sizeof(scrollback_buffer));
    // Initialize to decimal format by default
    use_hex_format = false;
}

// Format modifiers implementation
TerminalOutput& TerminalOutput::hex() {
    use_hex_format = true;
    return *this;
}

TerminalOutput& TerminalOutput::dec() {
    use_hex_format = false;
    return *this;
}

void TerminalOutput::scroll_screen_internal() {
    // First, save the top line that's about to be scrolled off to scrollback buffer
    if (scrollback_lines < SCROLLBACK_BUFFER_HEIGHT) {
        // We have room in scrollback buffer, so save the first line
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            scrollback_buffer[scrollback_lines * VGA_WIDTH + x] = terminal_buffer[x];
        }
        scrollback_lines++;
    } else {
        // Scrollback buffer is full, so shift everything up one line
        for (size_t y = 0; y < SCROLLBACK_BUFFER_HEIGHT - 1; y++) {
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                scrollback_buffer[y * VGA_WIDTH + x] = scrollback_buffer[(y + 1) * VGA_WIDTH + x];
            }
        }
        
        // Now save the current top line to the last scrollback line
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            scrollback_buffer[(SCROLLBACK_BUFFER_HEIGHT - 1) * VGA_WIDTH + x] = terminal_buffer[x];
        }
    }
    
    // Move all rows up one line
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t dest_index = y * VGA_WIDTH + x;
            const size_t src_index = (y + 1) * VGA_WIDTH + x;
            terminal_buffer[dest_index] = terminal_buffer[src_index];
        }
    }
    
    // Clear the last row
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        terminal_buffer[index] = make_vgaentry(' ', terminal_color);
    }
}

void TerminalOutput::put_entry_at(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = make_vgaentry(c, color);
}

void TerminalOutput::put_char(char c) {
    if (c == '\n') {
        // Handle newline character
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            // We've reached the bottom of the screen, need to scroll
            scroll_screen_internal();
            terminal_row = VGA_HEIGHT - 1; // Stay at the last row
        }
    } else if (c == '\b') {
        // Handle backspace character
        if (terminal_column > 0) {
            // Move cursor back one position
            terminal_column--;
            // Clear the character at the cursor position
            put_entry_at(' ', terminal_color, terminal_column, terminal_row);
        } else if (terminal_row > 0) {
            // If at the beginning of a line and not the first line,
            // move to the end of the previous line
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
            // Clear the character at the cursor position
            put_entry_at(' ', terminal_color, terminal_column, terminal_row);
        }
    } else if (c == '\r') {
        // Handle carriage return (move to beginning of line)
        terminal_column = 0;
    } else if (c == '\t') {
        // Handle tab (move to next 8-character boundary)
        size_t tab_size = 8;
        terminal_column = (terminal_column + tab_size) & ~(tab_size - 1);
        if (terminal_column >= VGA_WIDTH) {
            terminal_column = 0;
            if (++terminal_row == VGA_HEIGHT) {
                scroll_screen_internal();
                terminal_row = VGA_HEIGHT - 1;
            }
        }
    } else {
        put_entry_at(c, terminal_color, terminal_column, terminal_row);
        if (++terminal_column == VGA_WIDTH) {
            terminal_column = 0;
            if (++terminal_row == VGA_HEIGHT) {
                scroll_screen_internal();
                terminal_row = VGA_HEIGHT - 1;
            }
        }
    }

    // Update the hardware cursor position
    update_hardware_cursor(terminal_column, terminal_row);
}

bool TerminalOutput::show_scrollback_page(int page) {
    if (page < 0 || page >= scrollback_lines / VGA_HEIGHT) {
        return false;  // Invalid page number
    }
    
    // Calculate starting line in scrollback buffer
    int start_line = scrollback_lines - (page + 1) * VGA_HEIGHT;
    if (start_line < 0) {
        start_line = 0;
    }
    
    // Backup current screen if this is the first scrollback operation
    static bool first_scrollback = true;
    static uint16_t screen_backup[SCREEN_BACKUP_SIZE];
    if (first_scrollback) {
        for (size_t y = 0; y < VGA_HEIGHT; y++) {
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                screen_backup[y * VGA_WIDTH + x] = terminal_buffer[y * VGA_WIDTH + x];
            }
        }
        first_scrollback = false;
    }
    
    // Copy scrollback buffer to screen
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            if (start_line + y < scrollback_lines) {
                terminal_buffer[y * VGA_WIDTH + x] = scrollback_buffer[(start_line + y) * VGA_WIDTH + x];
            } else {
                // If we run out of scrollback lines, use the backup screen
                int backup_line = y - (scrollback_lines - start_line);
                if (backup_line >= 0 && backup_line < VGA_HEIGHT) {
                    terminal_buffer[y * VGA_WIDTH + x] = screen_backup[backup_line * VGA_WIDTH + x];
                } else {
                    // Fill with blanks if we somehow run out of data
                    terminal_buffer[y * VGA_WIDTH + x] = make_vgaentry(' ', terminal_color);
                }
            }
        }
    }
    
    // Update hardware cursor (place at bottom left during scrollback)
    update_hardware_cursor(0, VGA_HEIGHT - 1);
    return true;
}

void TerminalOutput::restore_screen() {
    // Redraw the current screen (the system should continue from where it left off)
    // This is essentially just a refresh, since the actual terminal_buffer hasn't changed
    update_hardware_cursor(terminal_column, terminal_row);
}

int TerminalOutput::get_scrollback_pages() {
    return (scrollback_lines + VGA_HEIGHT - 1) / VGA_HEIGHT;  // Ceiling division
}

TerminalOutput& TerminalOutput::operator<<(const char* str) {
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        put_char(str[i]);
    }
    return *this;
}

TerminalOutput& TerminalOutput::operator<<(char c) {
    put_char(c);
    return *this;
}

TerminalOutput& TerminalOutput::operator<<(int num) {
    char buffer[20];
    if (use_hex_format) {
        sprintf(buffer, "0x%x", num);
    } else {
        sprintf(buffer, "%d", num);
    }
    *this << buffer;
    return *this;
}

TerminalOutput& TerminalOutput::operator<<(unsigned int num) {
    char buffer[20];
    if (use_hex_format) {
        sprintf(buffer, "0x%x", num);
    } else {
        sprintf(buffer, "%u", num);
    }
    *this << buffer;
    return *this;
}

TerminalOutput& TerminalOutput::operator<<(void* ptr) {
    char buffer[20];
    sprintf(buffer, "0x%x", reinterpret_cast<unsigned int>(ptr));
    *this << buffer;
    return *this;
}

// Implementation of TerminalInput methods
TerminalInput::TerminalInput() : input_ready(false) {
    // Initialize history
    for (int i = 0; i < HISTORY_SIZE; i++) {
        memset(command_history[i], 0, MAX_COMMAND_LENGTH);
    }
}

void TerminalInput::setInputReady(const char* buffer) {
    strcpy(input_buffer, buffer);
    
    // Add command to history if not empty
    if (strlen(buffer) > 0) {
        // Shift history entries down
        for (int i = HISTORY_SIZE - 1; i > 0; i--) {
            strcpy(command_history[i], command_history[i-1]);
        }
        
        // Add new command at the top
        strcpy(command_history[0], buffer);
        
        // Update history count if needed
        if (history_count < HISTORY_SIZE) {
            history_count++;
        }
    }
    
    history_index = -1;  // Reset history navigation
    input_ready = true;
}

void TerminalInput::navigateHistory(bool up) {
    if (up) {  // Up key
        if (history_index < history_count - 1) {
            history_index++;
            // Update command buffer with history item
            strcpy(input_buffer, command_history[history_index]);
            input_length = strlen(input_buffer);
            
            // Clear current input line and display the history item
            clearInputLine();
            terminal_writestring(input_buffer);
        }
    } else {  // Down key
        if (history_index > -1) {
            history_index--;
            
            // Clear current input line
            clearInputLine();
            
            if (history_index == -1) {
                // Return to empty input
                input_buffer[0] = '\0';
                input_length = 0;
            } else {
                // Show previous history item
                strcpy(input_buffer, command_history[history_index]);
                input_length = strlen(input_buffer);
                terminal_writestring(input_buffer);
            }
        }
    }
}

void TerminalInput::clearInputLine() {
    size_t current_col = terminal_column;
    while (current_col > 0) {
        terminal_putchar('\b');
        current_col--;
    }
    
    // Clear the entire line
    for (size_t i = 0; i < VGA_WIDTH; i++) {
        terminal_putentryat(' ', terminal_color, i, terminal_row);
    }
    
    // Reset cursor to beginning of line
    terminal_column = 0;
    update_hardware_cursor(terminal_column, terminal_row);
}

TerminalInput& TerminalInput::operator>>(char* str) {
    input_ready = false;
    memset(input_buffer, 0, sizeof(input_buffer));
    
    // Reset input buffer
    input_length = 0;
    
    // Wait for input to be ready (set by keyboard interrupt)
    while (!input_ready) {
        asm volatile ("hlt"); // Wait for input
    }
    
    // Copy input to provided string
    strcpy(str, input_buffer);
    return *this;
}

// Implementation of std namespace manipulators
namespace std {
    TerminalOutput& hex(TerminalOutput& out) {
        return out.hex();
    }
    
    TerminalOutput& dec(TerminalOutput& out) {
        return out.dec();
    }
}

void init_terminal_io() {
    // Initialize the terminal I/O objects
    cout = TerminalOutput();
    cin = TerminalInput();
}