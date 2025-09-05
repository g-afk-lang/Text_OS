#include "interrupts.h"
#include "terminal_hooks.h"
#include "iostream_wrapper.h"
#include "test.h"
#include "notepad.h"
#include "pci.h"
#include "xhci.h"

// External declaration for the global DMA manager instance
extern DMAManager dma_manager;



// USB keyboard state
bool usb_keyboard_active = false;
bool ps2_keyboard_disabled = false;

// External function declarations
extern bool is_notepad_running();
extern bool is_pong_running();
extern void notepad_handle_input(char key);
extern void pong_handle_input(char key);
extern void pong_update();
extern void start_pong_game();
extern void update_cursor_state();
extern void terminal_putchar(char c);
extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t val);

// USB keyboard hardware state
static usb_hid_keyboard_report_t last_usb_report = {0};
static usb_hid_keyboard_report_t current_usb_report = {0};

// Simple memory functions
static void* simple_memcpy(void* dst, const void* src, size_t n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

static void* simple_memset(void* s, int c, size_t n) {
    char* p = (char*)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (char)c;
    }
    return s;
}

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

// USB keyboard functions
void setup_usb_keyboard_hardware() {
    cout << "Setting up USB keyboard hardware override...\n";
    usb_keyboard_active = true;
    cout << "USB keyboard hardware setup completed\n";
}

char usb_hid_to_ascii(uint8_t hid_code, bool shift) {
    // USB HID to ASCII conversion table (simplified)
    static const char hid_to_ascii_table[] = {
        0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
        'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\n', '\b', '\t', ' ',
        '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/'
    };
    
    static const char hid_to_ascii_shifted[] = {
        0, 0, 0, 0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
        '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '\n', '\b', '\t', ' ',
        '_', '+', '{', '}', '|', 0, ':', '"', '~', '<', '>', '?'
    };
    
    if (hid_code >= sizeof(hid_to_ascii_table)) return 0;
    
    return shift ? hid_to_ascii_shifted[hid_code] : hid_to_ascii_table[hid_code];
}

void handle_keyboard_input(char key) {
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

void process_usb_keyboard_interrupt() {
    // Simulate USB keyboard report reading
    // In a real implementation, this would read from USB controller
    
    // Check for changes in key state
    for (int i = 0; i < 6; i++) {
        uint8_t key = current_usb_report.keycodes[i];
        if (key != 0) {
            // Check if this is a new key press
            bool was_pressed = false;
            for (int j = 0; j < 6; j++) {
                if (last_usb_report.keycodes[j] == key) {
                    was_pressed = true;
                    break;
                }
            }
            
            if (!was_pressed) {
                // New key press detected
                bool shift_active = (current_usb_report.modifier_keys & 0x22) != 0; // Left or right shift
                char ascii_char = usb_hid_to_ascii(key, shift_active);
                
                if (ascii_char != 0) {
                    handle_keyboard_input(ascii_char);
                }
            }
        }
    }
    
    // Save current report for next comparison
    simple_memcpy(&last_usb_report, &current_usb_report, sizeof(usb_hid_keyboard_report_t));
}

void disable_ps2_keyboard() {
    cout << "Disabling PS/2 keyboard interrupts...\n";
    // Mask PS/2 keyboard interrupt (IRQ1)
    uint8_t mask = inb(0x21);
    mask |= 0x02; // Set bit 1 to mask IRQ1
    outb(0x21, mask);
    ps2_keyboard_disabled = true;
    cout << "PS/2 keyboard disabled\n";
}

void enable_usb_keyboard_override() {
    cout << "Enabling USB keyboard interrupt override...\n";
    
    // First disable PS/2 keyboard
    disable_ps2_keyboard();
    
    // Set up USB keyboard hardware
    setup_usb_keyboard_hardware();
    
    // Enable USB keyboard interrupt (using IRQ11 as example)
    uint8_t mask = inb(0xA1); // Slave PIC mask
    mask &= ~0x08; // Clear bit 3 to unmask IRQ11
    outb(0xA1, mask);
    
    cout << "USB keyboard override enabled\n";
}

extern "C" void usb_keyboard_interrupt_handler() {
    // Process USB keyboard interrupt
    if (usb_keyboard_active) {
        process_usb_keyboard_interrupt();
    }
    
    // Send EOI to both PICs (since IRQ11 is on slave PIC)
    outb(0xA0, 0x20); // EOI to slave PIC
    outb(0x20, 0x20); // EOI to master PIC
}

// Override the PS/2 keyboard handler to redirect to USB when active
extern "C" void keyboard_handler() {
    if (usb_keyboard_active && !ps2_keyboard_disabled) {
        // Redirect to USB keyboard processing
        usb_keyboard_interrupt_handler();
        return;
    }
    
    // Original PS/2 keyboard handling (only if USB keyboard not active)
    uint8_t scancode = inb(0x60);
    
    // Check for extended key code (0xE0)
    if (scancode == 0xE0) {
        extended_key = true;
        outb(0x20, 0x20);
        return;
    }


    // Handle F5 key press to start Pong
    if (scancode == SCANCODE_F5_PRESS) {
        if (!is_notepad_running()) {
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
                    pong_handle_input('w');
                    break;
                case SCANCODE_DOWN:
                    pong_handle_input('s');
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
        handle_keyboard_input(key);
    }
    outb(0x20, 0x20);
}

extern "C" void timer_handler() {
    if (is_pong_running()) {
        pong_update();
    } else if (!is_pong_running() && !is_notepad_running()) {
        update_cursor_state();
    }
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

    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    asm volatile ("lgdt %0" : : "m" (gdtp));

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

/* Assembly wrappers */
extern "C" void keyboard_handler_wrapper();
asm(
    ".global keyboard_handler_wrapper\n"
    "keyboard_handler_wrapper:\n"
    " pusha\n"
    " call keyboard_handler\n"
    " popa\n"
    " iret\n"
);

extern "C" void timer_handler_wrapper();
asm(
    ".global timer_handler_wrapper\n"
    "timer_handler_wrapper:\n"
    " pusha\n"
    " call timer_handler\n"
    " popa\n"
    " iret\n"
);

extern "C" void usb_keyboard_interrupt_wrapper();
asm(
    ".global usb_keyboard_interrupt_wrapper\n"
    "usb_keyboard_interrupt_wrapper:\n"
    " pusha\n"
    " call usb_keyboard_interrupt_handler\n"
    " popa\n"
    " iret\n"
);

/* Initialize PIC */
void init_pic() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    // Initially allow both PS/2 keyboard and timer, disable others
    outb(0x21, 0xFC); // 1111 1100 = all but IRQ0 and IRQ1 masked
    outb(0xA1, 0xF7); // 1111 0111 = all but IRQ11 masked (for USB keyboard)
}

/* Initialize PIT */
void init_pit() {
    uint32_t divisor = 1193180 / 100;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

/* Initialize keyboard */
void init_keyboard() {
    init_gdt();

    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // Set up interrupt handlers
    idt_set_gate(0x20, reinterpret_cast<uint32_t>(timer_handler_wrapper), 0x08, 0x8E);
    idt_set_gate(0x21, reinterpret_cast<uint32_t>(keyboard_handler_wrapper), 0x08, 0x8E);
    idt_set_gate(0x2B, reinterpret_cast<uint32_t>(usb_keyboard_interrupt_wrapper), 0x08, 0x8E); // IRQ11

    idt_load();
    init_pic();
    init_pit();
    
    asm volatile ("sti");
    
    cout << "Interrupt system initialized\n";
}
