#ifndef TERMINAL_HOOKS_H
#define TERMINAL_HOOKS_H

#include "types.h"

// Terminal state 
extern size_t terminal_row;
extern size_t terminal_column;
extern uint8_t terminal_color;
extern uint16_t* terminal_buffer;
extern bool cursor_visible;
extern uint32_t cursor_blink_counter;

// Input buffer state
extern char input_buffer[MAX_COMMAND_LENGTH];
extern int input_length;
extern bool input_ready;
extern bool extended_key;

// Terminal functions
uint8_t make_color(enum vga_color fg, enum vga_color bg);
uint16_t make_vgaentry(char c, uint8_t color);
void update_hardware_cursor(int x, int y);
void enable_hardware_cursor(uint8_t cursor_start, uint8_t cursor_end);
void disable_hardware_cursor();
void clear_screen();
void terminal_initialize();
void terminal_setcolor(uint8_t color);
void terminal_putentryat(char c, uint8_t color, size_t x, size_t y);
void terminal_putchar(char c);
void terminal_writestring(const char* data);
void update_cursor_state();
void scroll_screen();

// IO port functions
static inline uint8_t inb(uint16_t port);
static inline void outb(uint16_t port, uint8_t val);

// Inline implementations for port I/O
inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

#endif // TERMINAL_HOOKS_H
