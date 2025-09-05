#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "types.h"
#include "dma_memory.h"

// External declarations for IDT and GDT
extern struct idt_entry idt[256];
extern struct idt_ptr idtp;
extern struct gdt_entry gdt[3];
extern struct gdt_ptr gdtp;

// External declaration for the global DMA manager instance
extern DMAManager dma_manager;

// Keyboard scancode tables
extern const char scancode_to_ascii[128];
extern const char extended_scancode_table[128];
extern bool extended_key;

// USB keyboard state
extern bool usb_keyboard_active;
extern bool ps2_keyboard_disabled;

// USB HID keyboard report structure
typedef struct {
    uint8_t modifier_keys;    // Ctrl, Shift, Alt, GUI keys
    uint8_t reserved;         // Always 0
    uint8_t keycodes[6];      // Up to 6 simultaneous key presses
} __attribute__((packed)) usb_hid_keyboard_report_t;

// Initialize interrupt-related components
void init_pic();
void init_pit();
void init_keyboard();
void init_gdt();
void idt_load();
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

// USB keyboard functions
void setup_usb_keyboard_hardware();
void enable_usb_keyboard_override();
void disable_ps2_keyboard();
char usb_hid_to_ascii(uint8_t hid_code, bool shift);
void handle_keyboard_input(char key);
void process_usb_keyboard_interrupt();

// Interrupt handler declarations
extern "C" {
    void keyboard_handler_wrapper();
    void timer_handler_wrapper();
    void usb_keyboard_interrupt_wrapper();
    void keyboard_handler();
    void timer_handler();
    void usb_keyboard_interrupt_handler();
}

#endif // INTERRUPTS_H
