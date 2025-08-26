#include "terminal_hooks.h"
#include "stdlib_hooks.h"

// Terminal state variables
size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer;
bool cursor_visible = true;
uint32_t cursor_blink_counter = 0;

// Input state variables
char input_buffer[MAX_COMMAND_LENGTH];
int input_length = 0;
bool input_ready = false;
bool extended_key = false;

uint8_t make_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

uint16_t make_vgaentry(char c, uint8_t color) {
    uint16_t c16 = static_cast<uint16_t>(c);
    uint16_t color16 = static_cast<uint16_t>(color);
    return c16 | color16 << 8;
}

void update_hardware_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;

    // CRT Controller registers: cursor position (low and high bytes)
    outb(0x3D4, 0x0F);  // Low byte index
    outb(0x3D5, static_cast<uint8_t>(pos & 0xFF));  // Low byte data
    outb(0x3D4, 0x0E);  // High byte index
    outb(0x3D5, static_cast<uint8_t>((pos >> 8) & 0xFF));  // High byte data
}

void enable_hardware_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    // CRT Controller registers: cursor shape
    outb(0x3D4, 0x0A);  // Cursor start register
    outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);  // Set start line (bits 0-4)

    outb(0x3D4, 0x0B);  // Cursor end register
    outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);  // Set end line (bits 0-4)
}

void disable_hardware_cursor() {
    outb(0x3D4, 0x0A);  // Cursor start register
    outb(0x3D5, 0x20);  // Bit 5 disables the cursor
}

void clear_screen() {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = make_vgaentry(' ', terminal_color);
        }
    }
    terminal_row = 0;
    terminal_column = 0;
    update_hardware_cursor(terminal_column, terminal_row);
}

void terminal_initialize() {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = make_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    terminal_buffer = reinterpret_cast<uint16_t*>(0xB8000);
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = make_vgaentry(' ', terminal_color);
        }
    }

    // Initialize hardware cursor (start line 14, end line 15 - typical underline cursor)
    enable_hardware_cursor(14, 15);
    update_hardware_cursor(0, 0);
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = make_vgaentry(c, color);
}

void scroll_screen() {
    // Move all rows up one line (except the first row which will be overwritten)
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

void terminal_putchar(char c) {
    if (c == '\n') {
        // Handle newline character
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            // We've reached the bottom of the screen, need to scroll
            scroll_screen();
            terminal_row = VGA_HEIGHT - 1; // Stay at the last row
        }
    } else if (c == '\b') {
        // Handle backspace character
        if (terminal_column > 0) {
            // Move cursor back one position
            terminal_column--;
            // Clear the character at the cursor position
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        } else if (terminal_row > 0) {
            // If at the beginning of a line and not the first line,
            // move to the end of the previous line
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
            // Clear the character at the cursor position
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        }
    } else {
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
        if (++terminal_column == VGA_WIDTH) {
            terminal_column = 0;
            if (++terminal_row == VGA_HEIGHT) {
                // We've reached the bottom of the screen, need to scroll
                scroll_screen();
                terminal_row = VGA_HEIGHT - 1; // Stay at the last row
            }
        }
    }

    // Update the hardware cursor position
    update_hardware_cursor(terminal_column, terminal_row);
}

void terminal_writestring(const char* data) {
    size_t datalen = strlen(data);
    for (size_t i = 0; i < datalen; i++)
        terminal_putchar(data[i]);
}

void update_cursor_state() {
    cursor_blink_counter++;
    if (cursor_blink_counter >= 25) {  // Adjust this value to control blink speed
        cursor_blink_counter = 0;
        cursor_visible = !cursor_visible;

        if (cursor_visible) {
            enable_hardware_cursor(14, 15);  // Show cursor (underline style)
        } else {
            disable_hardware_cursor();  // Hide cursor
        }

        update_hardware_cursor(terminal_column, terminal_row);
    }
}
