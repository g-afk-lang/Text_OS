#include "interrupts.h"
#include "terminal_hooks.h"
#include "iostream_wrapper.h"

// IDT and GDT structures
struct idt_entry idt[256];
struct idt_ptr idtp;
struct gdt_entry gdt[3];
struct gdt_ptr gdtp;

// Keyboard scancode tables
const char scancode_to_ascii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
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

/* Keyboard interrupt handler */
extern "C" void keyboard_handler() {
    /* Read scancode from keyboard data port */
    uint8_t scancode = inb(0x60);

    /* Check for extended key code (0xE0) */
    if (scancode == 0xE0) {
        extended_key = true;
        /* Send EOI to PIC */
        outb(0x20, 0x20);
        return;
    }

    /* Handle key release (bit 7 set) */
    if (scancode & 0x80) {
        /* Reset extended key flag if it was set */
        extended_key = false;
        /* Send EOI to PIC */
        outb(0x20, 0x20);
        return;
    }

    // Handle special keys for extended keyboard sequences
    if (extended_key) {
        switch (scancode) {
            case SCANCODE_UP: // Up arrow
                // Access the global cin object and use history navigation
                //cin.navigateHistory(true);
                break;
                
            case SCANCODE_DOWN: // Down arrow
                // Access the global cin object and use history navigation
                //cin.navigateHistory(false);
                break;
        }
        
        // Reset extended key flag
        extended_key = false;
        
        /* Send EOI to PIC */
        outb(0x20, 0x20);
        return;
    }

    /* Normal input handling */
    char key = scancode_to_ascii[scancode];
    if (key != 0) {
        if (key == '\n') {
            // Enter key - process command
            terminal_putchar(key);
            input_buffer[input_length] = '\0';

            // Notify TerminalInput about the command
            cin.setInputReady(input_buffer);

            // Reset for next command
            input_length = 0;
        } else if (key == '\b') {
            // Backspace - delete last character
            if (input_length > 0) {
                terminal_putchar(key);
                input_length--;
            }
        } else if (input_length < MAX_COMMAND_LENGTH - 1) {
            // Regular character - add to buffer and display
            input_buffer[input_length++] = key;
            terminal_putchar(key);
        }
    }

    /* Send EOI to PIC */
    outb(0x20, 0x20);
}

/* Timer interrupt handler */
extern "C" void timer_handler() {
    // Blink cursor
    update_cursor_state();

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
    "    pusha\n"            // Save registers
    "    call keyboard_handler\n" // Call our C++ handler
    "    popa\n"             // Restore registers
    "    iret\n"             // Return from interrupt
);

/* Timer interrupt handler wrapper */
extern "C" void timer_handler_wrapper();
asm(
    ".global timer_handler_wrapper\n"
    "timer_handler_wrapper:\n"
    "    pusha\n"            // Save registers
    "    call timer_handler\n" // Call our C++ handler
    "    popa\n"             // Restore registers
    "    iret\n"             // Return from interrupt
);

/* Initialize PIC */
void init_pic() {
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

    /* Mask all interrupts except keyboard (IRQ1) and timer (IRQ0) */
    outb(0x21, 0xFC); /* 1111 1100 = all but IRQ0 and IRQ1 masked */
    outb(0xA1, 0xFF); /* Mask all slave interrupts */
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

/* Initialize keyboard */
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

    /* Enable interrupts */
    asm volatile ("sti");
}