#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "types.h"

// External declarations for IDT and GDT
extern struct idt_entry idt[256];
extern struct idt_ptr idtp;
extern struct gdt_entry gdt[3];
extern struct gdt_ptr gdtp;

// Keyboard scancode tables
extern const char scancode_to_ascii[128];
extern const char extended_scancode_table[128];

// Initialize interrupt-related components
void init_pic();
void init_pit();
void init_keyboard();
void init_gdt();
void idt_load();
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

// Interrupt handler declarations
extern "C" {
    void keyboard_handler_wrapper();
    void timer_handler_wrapper();
    void keyboard_handler();
    void timer_handler();
}

#endif // INTERRUPTS_H