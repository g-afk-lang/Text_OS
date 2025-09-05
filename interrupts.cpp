#include "interrupts.h"
#include "terminal_hooks.h"
#include "iostream_wrapper.h"
#include "test.h"
#include "notepad.h"

// IDT and GDT structures
struct idt_entry idt[256];
struct idt_ptr idtp;
struct gdt_entry gdt[3];
struct gdt_ptr gdtp;

// --- KEYBOARD STATE ---
static bool shift_pressed = false;

// --- SCANCODE CONSTANTS ---
#define SCANCODE_L_SHIFT_PRESS 0x2A
#define SCANCODE_R_SHIFT_PRESS 0x36
#define SCANCODE_L_SHIFT_RELEASE 0xAA
#define SCANCODE_R_SHIFT_RELEASE 0xB6
#define SCANCODE_UP 0x48
#define SCANCODE_DOWN 0x50
#define SCANCODE_LEFT 0x4B      
#define SCANCODE_RIGHT 0x4D     
#define SCANCODE_HOME 0x47      
#define SCANCODE_END 0x4F       
#define SCANCODE_F5_PRESS 0x3F
#define SCANCODE_ESC 0x01       
#define KEY_F5 -5

// Keyboard scancode tables
const char scancode_to_ascii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const char scancode_to_ascii_shifted[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const char extended_scancode_table[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\n', 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// COMPLETELY REPLACE keyboard_handler() function
extern "C" void keyboard_handler() {
    uint8_t scancode = inb(0x60);
    
    // Check for extended key code (0xE0)
    if (scancode == 0xE0) {
        extended_key = true;
        outb(0x20, 0x20);
        return;
    }
    
    // Handle ESC key specially for notepad
    if (scancode == SCANCODE_ESC) {
        if (is_notepad_running()) {
            notepad_handle_special_key(scancode);
        }
        extended_key = false;
        outb(0x20, 0x20);
        return;
    }
    
    // Handle F5 key press to start Pong
    if (scancode == SCANCODE_F5_PRESS) {
        if (!is_notepad_running()) { // Don't start Pong if notepad is running
            start_pong_game();
        }
        outb(0x20, 0x20);
        return;
    }
    
    // Handle Shift key press and release
    if (scancode == SCANCODE_L_SHIFT_PRESS || scancode == SCANCODE_R_SHIFT_PRESS) {
        shift_pressed = true;
        outb(0x20, 0x20);
        return;
    }
    
    if (scancode == SCANCODE_L_SHIFT_RELEASE || scancode == SCANCODE_R_SHIFT_RELEASE) {
        shift_pressed = false;
        outb(0x20, 0x20);
        return;
    }
    
    // Handle key release (bit 7 set) for non-shift keys
    if (scancode & 0x80) {
        extended_key = false;
        outb(0x20, 0x20);
        return;
    }
    
    // Handle extended keys (arrow keys, etc.)
    if (extended_key) {
        if (is_notepad_running()) {
            notepad_handle_special_key(scancode);
        } else if (is_pong_running()) {
            switch (scancode) {
                case SCANCODE_UP:
                    pong_handle_input('w'); // Map up arrow to W
                    break;
                case SCANCODE_DOWN:
                    pong_handle_input('s'); // Map down arrow to S
                    break;
            }
        } else {
            switch (scancode) {
                case SCANCODE_UP:
                    // cin.navigateHistory(true);
                    break;
                case SCANCODE_DOWN:
                    // cin.navigateHistory(false);
                    break;
            }
        }
        extended_key = false;
        outb(0x20, 0x20);
        return;
    }
    
    // Normal input handling
    const char* current_scancode_table = shift_pressed ? scancode_to_ascii_shifted : scancode_to_ascii;
    char key = current_scancode_table[scancode];
    
    if (key != 0) {
        if (is_notepad_running()) {
            notepad_handle_input(key);
        } else if (is_pong_running()) {
            pong_handle_input(key);
        } else {
            // Normal terminal input handling
            if (key == '\n') {
                terminal_putchar(key);
                input_buffer[input_length] = '\0';
                cin.setInputReady(input_buffer);
                input_length = 0;
            } else if (key == '\b') {
                if (input_length > 0) {
                    terminal_putchar(key);
                    input_length--;
                }
            } else if (input_length < MAX_COMMAND_LENGTH - 1) {
                input_buffer[input_length++] = key;
                terminal_putchar(key);
            }
        }
    }
    
    outb(0x20, 0x20);
}

// REPLACE timer_handler() to prevent blink glitch
extern "C" void timer_handler() {
    // Update Pong game if it's running
    if (is_pong_running()) {
        pong_update();
    } else if (!is_pong_running() && !is_notepad_running()) {
        // Only blink cursor when not in game AND not in notepad
        update_cursor_state();
    }
    // Don't update cursor when notepad or pong is running to prevent blink glitch
    
    // Send EOI to PIC
    outb(0x20, 0x20);
}

/* Set up a GDT entry */
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access = access;
}

/* Initialize GDT */
void init_gdt() {
    gdtp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gdtp.base = reinterpret_cast<uint32_t>(&gdt);
    /* NULL descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);
    /* Code segment: base = 0, limit = 4GB, 32-bit, code, ring 0 */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    /* Data segment: base = 0, limit = 4GB, 32-bit, data, ring 0 */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    /* Load GDT */
    asm volatile ("lgdt %0" : : "m" (gdtp));
    /* Update segment registers */
    asm volatile (
        "jmp $0x08, $reload_cs\n"
        "reload_cs:\n"
        "mov $0x10, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %fs\n"
        "mov %ax, %gs\n"
        "mov %ax, %ss\n"
    );
}

/* Set up IDT entry */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = (base & 0xFFFF);
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

/* Load IDT */
void idt_load() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = reinterpret_cast<uint32_t>(&idt);
    asm volatile ("lidt %0" : : "m" (idtp));
}

/* Keyboard interrupt handler wrapper (assembler entry point) */
extern "C" void keyboard_handler_wrapper();
asm(
    ".global keyboard_handler_wrapper\n"
    "keyboard_handler_wrapper:\n"
    " pusha\n" // Save registers
    " call keyboard_handler\n" // Call our C++ handler
    " popa\n" // Restore registers
    " iret\n" // Return from interrupt
);

/* Timer interrupt handler wrapper */
extern "C" void timer_handler_wrapper();
asm(
    ".global timer_handler_wrapper\n"
    "timer_handler_wrapper:\n"
    " pusha\n" // Save registers
    " call timer_handler\n" // Call our C++ handler
    " popa\n" // Restore registers
    " iret\n" // Return from interrupt
);

/* Initialize PIC - Enhanced version for USB compatibility */
void init_pic() {
    // Save current interrupt masks
    uint8_t master_mask = inb(0x21);
    uint8_t slave_mask = inb(0xA1);
    
    /* ICW1: Start initialization sequence */
    outb(0x20, 0x11); /* Master PIC */
    outb(0xA0, 0x11); /* Slave PIC */
    
    /* ICW2: Define PIC vectors */
    outb(0x21, 0x20); /* Master PIC vector offset (IRQ0 = int 0x20) */
    outb(0xA1, 0x28); /* Slave PIC vector offset (IRQ8 = int 0x28) */
    
    /* ICW3: Tell Master PIC that there is a slave PIC at IRQ2 */
    outb(0x21, 0x04);
    /* ICW3: Tell Slave PIC its cascade identity */
    outb(0xA1, 0x02);
    
    /* ICW4: Set x86 mode */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    /* Restore interrupt masks but ensure keyboard (IRQ1) and timer (IRQ0) are enabled */
    /* Also ensure USB interrupts (typically IRQ11, 10, or 9) remain enabled */
    master_mask &= ~0x03; // Enable IRQ0 (timer) and IRQ1 (keyboard)
    slave_mask &= ~0x0E;  // Enable IRQ9, IRQ10, IRQ11 for USB devices
    
    outb(0x21, master_mask);
    outb(0xA1, slave_mask);
}

/* Initialize PIT (Programmable Interval Timer) for cursor blinking */
void init_pit() {
    uint32_t divisor = 1193180 / 100; // 100 Hz timer frequency
    // Set command byte: channel 0, access mode lobyte/hibyte, mode 3 (square wave)
    outb(0x43, 0x36);
    // Send divisor (low byte first, then high byte)
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

/* Re-initialize keyboard after USB setup */
void reinit_keyboard_after_usb() {
    // Clear keyboard buffer
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    
    // Send keyboard enable command
    while (inb(0x64) & 0x02); // Wait for input buffer to be clear
    outb(0x60, 0xF4); // Enable keyboard command
    
    // Wait for ACK
    while (!(inb(0x64) & 0x01));
    uint8_t response = inb(0x60);
    if (response != 0xFA) {
        // Keyboard didn't acknowledge, try reset
        while (inb(0x64) & 0x02);
        outb(0x60, 0xFF); // Reset keyboard
        while (!(inb(0x64) & 0x01));
        inb(0x60); // Read response
    }
    
    // Re-enable keyboard interrupt
    init_pic();
}

/* Initialize keyboard - Enhanced version */
void init_keyboard() {
    /* First, set up GDT */
    init_gdt();
    
    /* Initialize IDT */
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }
    
    /* Set timer interrupt gate (IRQ0) */
    idt_set_gate(0x20, reinterpret_cast<uint32_t>(timer_handler_wrapper), 0x08, 0x8E);
    
    /* Set keyboard interrupt gate (IRQ1) */
    idt_set_gate(0x21, reinterpret_cast<uint32_t>(keyboard_handler_wrapper), 0x08, 0x8E);
    
    /* Load IDT */
    idt_load();
    
    /* Initialize PIC */
    init_pic();
    
    /* Initialize PIT */
    init_pit();
    
    /* Clear any pending keyboard data */
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    
    /* Enable interrupts */
    asm volatile ("sti");
}