#ifndef XHCI_H
#define XHCI_H

#include "types.h"
// Add these declarations before the #endif
void activate_usb_keyboard();
void enable_usb_keyboard_override();
void disable_ps2_keyboard();

// --- xHCI Register Structures ---
// Vol 1, Sec 5.3: Capability Registers
typedef struct {
    uint8_t  cap_length;
    uint8_t  reserved;
    uint16_t hci_version;
    uint32_t hcs_params1;
    uint32_t hcs_params2;
    uint32_t hcs_params3;
    uint32_t hcc_params1;
    uint32_t dboff;
    uint32_t rtsoff;
} __attribute__((packed)) xhci_cap_regs_t;

// Vol 1, Sec 5.4: Operational Registers
typedef struct {
    uint32_t usb_cmd;
    uint32_t usb_sts;
    uint32_t page_size;
    uint64_t dnctrl;
    uint64_t crcr;
    uint64_t dcbaap;
    uint32_t config;
} __attribute__((packed)) xhci_op_regs_t;

// Vol 1, Sec 5.5: Port Registers
typedef struct {
    uint32_t portsc;
    uint32_t portpmsc;
    uint32_t portli;
    uint32_t porthlpmc;
} __attribute__((packed)) xhci_port_regs_t;

// --- xHCI Data Structures (Memory) ---
// Vol 1, Sec 6.1: TRB - Transfer Request Block
typedef struct {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) xhci_trb_t;

// Function to initialize the xHCI controller
bool xhci_init();

#endif // XHCI_H
