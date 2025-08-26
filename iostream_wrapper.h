#ifndef IOSTREAM_WRAPPER_H
#define IOSTREAM_WRAPPER_H
#include <stddef.h>
#include <stdint.h>
#include "terminal_hooks.h"
// Constants for scrollback buffer
#define SCROLLBACK_BUFFER_HEIGHT 1000
#define SCREEN_BACKUP_SIZE (VGA_WIDTH * VGA_HEIGHT)
// Maximum command history
#define HISTORY_SIZE 20
#define MAX_COMMAND_LENGTH 256

// Forward declaration
class TerminalOutput;

// Define manipulator functions in std namespace
namespace std {
    TerminalOutput& hex(TerminalOutput& out);
    TerminalOutput& dec(TerminalOutput& out);
}

// TerminalOutput class for output operations
class TerminalOutput {
private:
    // Scrollback buffer
    uint16_t scrollback_buffer[SCROLLBACK_BUFFER_HEIGHT * VGA_WIDTH];
    size_t scrollback_lines = 0;
   
    // Flag for hex mode
    bool use_hex_format;
   
    // Internal methods
    void scroll_screen_internal();
    void put_entry_at(char c, uint8_t color, size_t x, size_t y);
    void put_char(char c);
public:
    TerminalOutput();
   
    // Format modifiers
    TerminalOutput& hex();
    TerminalOutput& dec();
   
    // Scrollback operations
    bool show_scrollback_page(int page);
    void restore_screen();
    int get_scrollback_pages();
   
    // Output operators
    TerminalOutput& operator<<(const char* str);
    TerminalOutput& operator<<(char c);
    TerminalOutput& operator<<(int num);
    TerminalOutput& operator<<(unsigned int num);
    TerminalOutput& operator<<(void* ptr);
    
    // Add support for manipulators
    TerminalOutput& operator<<(TerminalOutput& (*manip)(TerminalOutput&));
};

// TerminalInput class for input operations
class TerminalInput {
private:
    char input_buffer[MAX_COMMAND_LENGTH];
    bool input_ready;
    size_t input_length = 0;
   
    // Command history
    char command_history[HISTORY_SIZE][MAX_COMMAND_LENGTH];
    int history_count = 0;
    int history_index = -1;
public:
    TerminalInput();
   
    // Input handling
    void setInputReady(const char* buffer);
    void navigateHistory(bool up);
    void clearInputLine();
   
    // Input operator
    TerminalInput& operator>>(char* str);
};
// Global instances
extern TerminalOutput cout;
extern TerminalInput cin;
// Initialization function
void init_terminal_io();
#endif // IOSTREAM_WRAPPER_H