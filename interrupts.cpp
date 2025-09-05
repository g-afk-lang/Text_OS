#include "interrupts.h"
#include "terminal_hooks.h"
#include "iostream_wrapper.h"
#include "test.h"

#include "notepad.h"
#include "xhci.h"  // This is now included via interrupts.h
#include "pci.h"

// --- UTILITY FUNCTION IMPLEMENTATIONS ---
void* simple_memcpy(void* dst, const void* src, size_t n) { char* d = (char*)dst; const char* s = (const char*)src; for (size_t i = 0; i < n; i++) d[i] = s[i]; return dst; }
void* simple_memset(void* s, int c, size_t n) { char* p = (char*)s; for (size_t i = 0; i < n; i++) p[i] = (char)c; return s; }
int simple_memcmp(const void* s1, const void* s2, size_t n) { const unsigned char* p1 = (const unsigned char*)s1; const unsigned char* p2 = (const unsigned char*)s2; for (size_t i = 0; i < n; i++) { if (p1[i] != p2[i]) return p1[i] - p2[i]; } return 0; }

// IDT and GDT structures
struct idt_entry idt[256];
struct idt_ptr idtp;
struct gdt_entry gdt[3];
struct gdt_ptr gdtp;

// Keyboard state
static bool shift_pressed = false;

// USB keyboard state
bool usb_keyboard_active = false;
bool ps2_keyboard_disabled = false;

// USB keyboard hardware state
static usb_hid_keyboard_report_t last_usb_report = {0};
static uint8_t usb_keyboard_irq = 11;  // Will be read from PCI config
static volatile bool usb_interrupt_received = false;

// Real hardware: xHCI Event Ring for processing USB events
static xhci_trb_t* xhci_event_ring = nullptr;  // Now using type from xhci.h
static uint32_t event_ring_cycle = 1;
static uint32_t event_ring_dequeue = 0;

// Real hardware: USB keyboard transfer ring
static xhci_trb_t* usb_keyboard_ring = nullptr;  // Now using type from xhci.h
static uint32_t keyboard_ring_cycle = 1;
static uint32_t keyboard_ring_enqueue = 0;
static uint32_t keyboard_ring_dequeue = 0;

// USB keyboard device information
static uint8_t keyboard_slot_id = 0;
static uint8_t keyboard_endpoint = 1;  // Usually endpoint 1 for keyboard interrupt IN



// Scancode constants
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

// --- REAL HARDWARE IMPLEMENTATION ---

// Get xHCI IRQ line from PCI configuration space
uint8_t get_xhci_irq_line() {
    pci_device_info xhci_device = scan_for_xhci();
    if (!xhci_device.found) {
        cout << "Error: xHCI controller not found for IRQ detection\n";
        return 11; // Default fallback
    }
    
    // Read interrupt line from PCI config space (offset 0x3C)
    uint32_t interrupt_config = pci_read_config_dword(
        xhci_device.bus, 
        xhci_device.device, 
        xhci_device.function, 
        0x3C
    );
    
    uint8_t irq_line = interrupt_config & 0xFF;
    cout << "Real hardware xHCI IRQ line: " << (int)irq_line << "\n";
    return irq_line;
}

// Enable PCI bus master for DMA operations
void enable_pci_bus_master() {
    pci_device_info xhci_device = scan_for_xhci();
    if (!xhci_device.found) {
        cout << "Error: Cannot enable bus master - xHCI not found\n";
        return;
    }
    
    // Read current command register
    uint32_t command = pci_read_config_dword(
        xhci_device.bus, 
        xhci_device.device, 
        xhci_device.function, 
        PCI_COMMAND_REGISTER
    );
    
    // Enable bus mastering and memory space access
    command |= 0x06;  // Enable Bus Master (bit 2) and Memory Space (bit 1)
    
    pci_write_config_dword(
        xhci_device.bus, 
        xhci_device.device, 
        xhci_device.function, 
        PCI_COMMAND_REGISTER, 
        command
    );
    
    cout << "PCI bus master enabled for xHCI DMA operations\n";
}

// Configure PCI interrupts for real hardware
void configure_pci_interrupts() {
    pci_device_info xhci_device = scan_for_xhci();
    if (!xhci_device.found) {
        cout << "Error: Cannot configure PCI interrupts - xHCI not found\n";
        return;
    }
    
    enable_pci_bus_master();
    
    // Read current status register to clear any pending interrupts
    uint32_t status = pci_read_config_dword(
        xhci_device.bus, 
        xhci_device.device, 
        xhci_device.function, 
        0x06  // Status register offset
    );
    
    cout << "PCI Status: 0x" << status << "\n";
    cout << "Real hardware PCI interrupts configured\n";
}

// Setup real USB keyboard hardware transfer rings
void setup_usb_keyboard_hardware() {
    if (!xhci_op_regs) {
        cout << "Error: xHCI not initialized - cannot setup USB keyboard hardware\n";
        return;
    }
    
    extern DMAManager dma_manager;
    
    // Allocate event ring for xHCI events
    void* event_ring_memory = dma_manager.allocate_dma_buffer(
        sizeof(xhci_trb_t) * USB_KEYBOARD_RING_SIZE + 64
    );
    
    if (!event_ring_memory) {
        cout << "Error: Failed to allocate xHCI event ring\n";
        return;
    }
    
    // Align to 64-byte boundary (xHCI requirement)
    uintptr_t addr = (uintptr_t)event_ring_memory;
    uintptr_t offset = (64 - (addr % 64)) % 64;
    xhci_event_ring = (xhci_trb_t*)(addr + offset);
    
    // Initialize event ring
    simple_memset(xhci_event_ring, 0, sizeof(xhci_trb_t) * USB_KEYBOARD_RING_SIZE);
    
    // Allocate transfer ring for USB keyboard
    void* transfer_ring_memory = dma_manager.allocate_dma_buffer(
        sizeof(xhci_trb_t) * USB_KEYBOARD_RING_SIZE + 64
    );
    
    if (!transfer_ring_memory) {
        cout << "Error: Failed to allocate USB keyboard transfer ring\n";
        return;
    }
    
    // Align transfer ring
    addr = (uintptr_t)transfer_ring_memory;
    offset = (64 - (addr % 64)) % 64;
    usb_keyboard_ring = (xhci_trb_t*)(addr + offset);
    
    // Initialize transfer ring
    simple_memset(usb_keyboard_ring, 0, sizeof(xhci_trb_t) * USB_KEYBOARD_RING_SIZE);
    
    // Setup Link TRB at the end for circular ring
    xhci_trb_t* link_trb = &usb_keyboard_ring[USB_KEYBOARD_RING_SIZE - 1];
    link_trb->parameter = (uint64_t)usb_keyboard_ring;  // Point back to start
    link_trb->control = (TRB_TYPE_LINK << 10) | (1 << 1);  // Link TRB, Toggle Cycle
    
    cout << "Real hardware USB keyboard transfer rings allocated and configured\n";
}

// Setup real USB keyboard device context and endpoints
bool setup_usb_keyboard_device(uint8_t slot_id) {
    if (!xhci_op_regs || !usb_keyboard_ring) {
        cout << "Error: xHCI not ready for device setup\n";
        return false;
    }
    
    keyboard_slot_id = slot_id;
    
    cout << "Setting up real USB keyboard device in slot " << (int)slot_id << "\n";
    
    extern DMAManager dma_manager;
    
    // Allocate device context (slot context + endpoint contexts)
    void* device_context_memory = dma_manager.allocate_dma_buffer(1024 + 64);
    if (!device_context_memory) {
        cout << "Error: Failed to allocate device context\n";
        return false;
    }
    
    // Align device context to 64-byte boundary
    uintptr_t addr = (uintptr_t)device_context_memory;
    uintptr_t offset = (64 - (addr % 64)) % 64;
    usb_slot_context_t* slot_context = (usb_slot_context_t*)(addr + offset);
    usb_endpoint_context_t* ep0_context = (usb_endpoint_context_t*)(addr + offset + 32);
    usb_endpoint_context_t* ep1_context = (usb_endpoint_context_t*)(addr + offset + 64);
    
    // Configure slot context for keyboard
    slot_context->dev_info = (1 << 27) | (1 << 0);  // Context entries = 1, slot state = default
    slot_context->port_info = 0;  // Will be set based on actual port
    slot_context->tt_info = 0;
    slot_context->dev_state = 0;
    
    // Configure control endpoint (EP0)
    ep0_context->ep_info = (4 << 3) | (1 << 0);  // EP type = control, EP state = running
    ep0_context->ep_info2 = (64 << 16) | (0 << 8);  // Max packet size = 64, error count = 0
    ep0_context->dequeue_ptr = (uint64_t)usb_keyboard_ring | 1;  // DCS = 1
    ep0_context->transfer_info = 8;  // Average TRB length
    
    // Configure interrupt IN endpoint (EP1) for keyboard
    ep1_context->ep_info = (7 << 3) | (1 << 0);  // EP type = interrupt IN, EP state = running
    ep1_context->ep_info2 = (8 << 16) | (0 << 8);   // Max packet size = 8, error count = 0
    ep1_context->dequeue_ptr = (uint64_t)usb_keyboard_ring | 1;  // DCS = 1
    ep1_context->transfer_info = 8;  // Average TRB length = 8 bytes (HID report size)
    
    cout << "USB keyboard device context configured\n";
    return true;
}

// Configure keyboard endpoint for interrupt transfers
void configure_keyboard_endpoint() {
    if (!usb_keyboard_ring || keyboard_slot_id == 0) {
        cout << "Error: Keyboard device not ready for endpoint configuration\n";
        return;
    }
    
    // Set up transfer TRBs for periodic keyboard polling
    xhci_trb_t* transfer_trb = &usb_keyboard_ring[keyboard_ring_enqueue];
    
    extern DMAManager dma_manager;
    
    // Allocate buffer for keyboard HID report
    void* report_buffer = dma_manager.allocate_dma_buffer(8 + 64);
    if (!report_buffer) {
        cout << "Error: Failed to allocate keyboard report buffer\n";
        return;
    }
    
    // Align buffer
    uintptr_t addr = (uintptr_t)report_buffer;
    uintptr_t offset = (64 - (addr % 64)) % 64;
    void* aligned_buffer = (void*)(addr + offset);
    
    // Setup Normal TRB for keyboard interrupt transfer
    transfer_trb->parameter = (uint64_t)aligned_buffer;
    transfer_trb->status = 8;  // Transfer length = 8 bytes
    transfer_trb->control = (TRB_TYPE_NORMAL << 10) | (1 << 5) | keyboard_ring_cycle;  // IOC=1, Cycle bit
    
    keyboard_ring_enqueue++;
    if (keyboard_ring_enqueue >= USB_KEYBOARD_RING_SIZE - 1) {
        keyboard_ring_enqueue = 0;
        keyboard_ring_cycle ^= 1;
    }
    
    cout << "Keyboard endpoint configured for interrupt transfers\n";
}

// Convert USB HID usage codes to ASCII
char usb_hid_to_ascii(uint8_t hid_code, bool shift) {
    // USB HID Usage Table for keyboards (real hardware codes)
    static const char hid_to_ascii_normal[256] = {
        // 0x00-0x03: Reserved, Error, POST Fail, Undefined
        0, 0, 0, 0,
        // 0x04-0x1D: Letters a-z
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
        'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        // 0x1E-0x27: Numbers 1-9, 0
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
        // 0x28-0x2F: Special keys
        '\n', 0, '\b', '\t', ' ', '-', '=', '[', ']', '\\',
        // 0x30-0x38: More special keys
        0, ';', '\'', '`', ',', '.', '/', 0, 0,
        // Fill rest with zeros for now
    };
    
    static const char hid_to_ascii_shift[256] = {
        0, 0, 0, 0,
        // Uppercase letters
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
        // Shifted numbers
        '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
        // Shifted special keys
        '\n', 0, '\b', '\t', ' ', '_', '+', '{', '}', '|',
        0, ':', '"', '~', '<', '>', '?', 0, 0,
    };
    
    if (hid_code < 256) {
        return shift ? hid_to_ascii_shift[hid_code] : hid_to_ascii_normal[hid_code];
    }
    return 0;
}

// Read USB keyboard report from real hardware
bool read_usb_keyboard_report(usb_hid_keyboard_report_t* report) {
    if (!usb_keyboard_ring || !usb_interrupt_received) {
        return false;
    }
    
    // Process completed transfer TRBs from event ring
    xhci_trb_t* event_trb = &xhci_event_ring[event_ring_dequeue];
    
    // Check if this event TRB is valid (cycle bit matches)
    bool cycle_match = ((event_trb->control & 0x1) == event_ring_cycle);
    if (!cycle_match) {
        return false;
    }
    
    // Check if this is a transfer event for our keyboard endpoint
    uint32_t trb_type = (event_trb->control >> 10) & 0x3F;
    if (trb_type == 32) {  // Transfer Event TRB
        // Extract completion code
        uint32_t completion_code = (event_trb->status >> 24) & 0xFF;
        if (completion_code == 1) {  // Success
            // Get transfer length
            uint32_t transfer_length = event_trb->status & 0xFFFFFF;
            
            if (transfer_length >= 8) {
                // Extract data from the TRB parameter (pointer to data buffer)
                uint64_t data_ptr = event_trb->parameter;
                if (data_ptr) {
                    simple_memcpy(report, (void*)data_ptr, sizeof(usb_hid_keyboard_report_t));
                    
                    // Advance event ring dequeue pointer
                    event_ring_dequeue++;
                    if (event_ring_dequeue >= USB_KEYBOARD_RING_SIZE - 1) {
                        event_ring_dequeue = 0;
                        event_ring_cycle ^= 1;
                    }
                    
                    usb_interrupt_received = false;
                    return true;
                }
            }
        }
    }
    
    return false;
}

// Process USB keyboard events from real hardware
void process_usb_keyboard_interrupt() {
    usb_hid_keyboard_report_t current_report;
    
    // Read actual hardware report from xHCI event ring
    if (!read_usb_keyboard_report(&current_report)) {
        return;  // No new keyboard data
    }
    
    // Process modifier key changes (real USB HID modifier byte)
    bool current_shift = (current_report.modifier_keys & 0x22) != 0;  // Left/Right Shift bits
    if (current_shift != shift_pressed) {
        shift_pressed = current_shift;
    }
    
    // Process key changes (compare with last report)
    for (int i = 0; i < 6; i++) {
        uint8_t current_key = current_report.keycodes[i];
        if (current_key == 0) continue;  // Empty keycode slot
        
        // Check if this is a new key press (not in previous report)
        bool is_new_key = true;
        for (int j = 0; j < 6; j++) {
            if (last_usb_report.keycodes[j] == current_key) {
                is_new_key = false;
                break;
            }
        }
        
        if (is_new_key) {
            // Convert USB HID usage code to ASCII
            char key = usb_hid_to_ascii(current_key, shift_pressed);
            if (key != 0) {
                handle_keyboard_input(key);
            }
        }
    }
    
    // Save current report for next comparison
    simple_memcpy(&last_usb_report, &current_report, sizeof(usb_hid_keyboard_report_t));
}

// Process xHCI events from hardware
void process_xhci_events() {
    if (!xhci_event_ring) return;
    
    while (true) {
        xhci_trb_t* event_trb = &xhci_event_ring[event_ring_dequeue];
        
        // Check cycle bit
        if ((event_trb->control & 0x1) != event_ring_cycle) {
            break;  // No more events
        }
        
        // Process based on TRB type
        uint32_t trb_type = (event_trb->control >> 10) & 0x3F;
        switch (trb_type) {
            case 32:  // Transfer Event
                handle_transfer_event(event_trb);
                break;
            case 33:  // Command Completion Event
                handle_command_completion(event_trb);
                break;
            default:
                // Other event types
                break;
        }
        
        // Advance dequeue pointer
        event_ring_dequeue++;
        if (event_ring_dequeue >= USB_KEYBOARD_RING_SIZE - 1) {
            event_ring_dequeue = 0;
            event_ring_cycle ^= 1;
        }
    }
}

// Handle transfer completion events
void handle_transfer_event(xhci_trb_t* event_trb) {
    // Check if this is for our keyboard endpoint
    uint32_t slot_id = (event_trb->control >> 24) & 0xFF;
    uint32_t endpoint_id = (event_trb->control >> 16) & 0x1F;
    
    if (slot_id == keyboard_slot_id && endpoint_id == keyboard_endpoint) {
        usb_interrupt_received = true;
        process_usb_keyboard_interrupt();
    }
}

// Handle command completion events
void handle_command_completion(xhci_trb_t* event_trb) {
    uint32_t completion_code = (event_trb->status >> 24) & 0xFF;
    if (completion_code != 1) {  // Not success
        cout << "xHCI command failed with code: " << completion_code << "\n";
    }
}

// Real hardware USB keyboard interrupt handler
extern "C" void usb_keyboard_interrupt_handler() {
    // Read xHCI interrupt status register
    if (xhci_op_regs) {
        uint32_t usb_sts = xhci_op_regs->usb_sts;
        
        // Check for Event Interrupt (EINT) bit 3
        if (usb_sts & 0x08) {
            // Process all pending events
            process_xhci_events();
            
            // Clear the interrupt by writing 1 to EINT bit
            xhci_op_regs->usb_sts = 0x08;
        }
        
        // Check for Host System Error (HSE) bit 2
        if (usb_sts & 0x04) {
            cout << "xHCI Host System Error detected and cleared\n";
            xhci_op_regs->usb_sts = 0x04;
        }
        
        // Check for Port Change Detect (PCD) bit 4
        if (usb_sts & 0x10) {
            // Handle port changes (device connect/disconnect)
            xhci_op_regs->usb_sts = 0x10;
        }
    }
    
    // Send End of Interrupt to PIC
    outb(0x20, 0x20);  // Master PIC
    if (usb_keyboard_irq >= 8) {
        outb(0xA0, 0x20);  // Slave PIC for IRQ >= 8
    }
}

// Register USB keyboard interrupt handler with real IRQ
void register_usb_keyboard_interrupt() {
    // Get actual IRQ line from hardware
    usb_keyboard_irq = get_xhci_irq_line();
    
    cout << "Registering real hardware USB keyboard interrupt on IRQ " << (int)usb_keyboard_irq << "\n";
    
    // Set up IDT entry for USB interrupt
    idt_set_gate(0x20 + usb_keyboard_irq, 
                 reinterpret_cast<uint32_t>(usb_keyboard_interrupt_wrapper), 
                 0x08, 0x8E);
    
    // Enable IRQ line in PIC hardware
    if (usb_keyboard_irq < 8) {
        // Master PIC (IRQ 0-7)
        uint8_t mask = inb(0x21);
        mask &= ~(1 << usb_keyboard_irq);  // Clear bit to enable
        outb(0x21, mask);
    } else {
        // Slave PIC (IRQ 8-15)
        uint8_t master_mask = inb(0x21);
        master_mask &= ~0x04;  // Enable IRQ2 cascade
        outb(0x21, master_mask);
        
        uint8_t slave_mask = inb(0xA1);
        slave_mask &= ~(1 << (usb_keyboard_irq - 8));
        outb(0xA1, slave_mask);
    }
    
    cout << "Real hardware USB keyboard interrupt registered successfully\n";
}

// Enable complete USB keyboard hardware override
void enable_usb_keyboard_override() {
    cout << "Initializing real hardware USB keyboard override...\n";
    
    // Configure real PCI hardware
    configure_pci_interrupts();
    
    // Setup real hardware transfer rings
    setup_usb_keyboard_hardware();
    
    // Configure keyboard device (assuming slot 1 for first keyboard)
    if (setup_usb_keyboard_device(1)) {
        configure_keyboard_endpoint();
    }
    
    // Register real hardware interrupt
    register_usb_keyboard_interrupt();
    
    // Enable interrupts in xHCI controller hardware
    if (xhci_op_regs) {
        // Enable Event Interrupt Enable (EIE) in USB Command Register
        xhci_op_regs->usb_cmd |= 0x04;
        
        // Enable Host System Error Enable (HSEE)
        xhci_op_regs->usb_cmd |= 0x08;
        
        cout << "xHCI hardware interrupts enabled (EIE + HSEE)\n";
    }
    
    usb_keyboard_active = true;
    cout << "Real hardware USB keyboard override is now active!\n";
}

// Handle keyboard input (unified for USB and PS/2)
void handle_keyboard_input(char key) {
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
}

// USB keyboard interrupt wrapper
extern "C" void usb_keyboard_interrupt_wrapper();
asm(
    ".global usb_keyboard_interrupt_wrapper\n"
    "usb_keyboard_interrupt_wrapper:\n"
    " pusha\n"
    " call usb_keyboard_interrupt_handler\n"
    " popa\n"
    " iret\n"
);

// --- ORIGINAL PS/2 KEYBOARD HANDLER (for compatibility) ---

extern "C" void keyboard_handler() {
    uint8_t scancode = inb(0x60);
    
    // If USB keyboard is active, reduce PS/2 priority but keep functional
    if (usb_keyboard_active && !ps2_keyboard_disabled) {
        // Process PS/2 but let USB take precedence for conflicts
    }
    
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
        if (!is_notepad_running()) {
            start_pong_game();
        }
        outb(0x20, 0x20);
        return;
    }
    
    // Handle Shift key press and release
    if (scancode == SCANCODE_L_SHIFT_PRESS || scancode == SCANCODE_R_SHIFT_PRESS) {
        if (!usb_keyboard_active) {  // Only update if USB not handling
            shift_pressed = true;
        }
        outb(0x20, 0x20);
        return;
    }
    
    if (scancode == SCANCODE_L_SHIFT_RELEASE || scancode == SCANCODE_R_SHIFT_RELEASE) {
        if (!usb_keyboard_active) {
            shift_pressed = false;
        }
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
    
    // Normal input handling (only if USB not active or PS/2 not disabled)
    if (!usb_keyboard_active || !ps2_keyboard_disabled) {
        const char* current_scancode_table = shift_pressed ? scancode_to_ascii_shifted : scancode_to_ascii;
        char key = current_scancode_table[scancode];
        
        if (key != 0) {
            handle_keyboard_input(key);
        }
    }
    
    outb(0x20, 0x20);
}

// --- TIMER HANDLER (unchanged) ---

extern "C" void timer_handler() {
    if (is_pong_running()) {
        pong_update();
    } else if (!is_pong_running() && !is_notepad_running()) {
        update_cursor_state();
    }
    outb(0x20, 0x20);
}

// --- GDT FUNCTIONS ---

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access = access;
}

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

// --- IDT FUNCTIONS ---

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = (base & 0xFFFF);
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void idt_load() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = reinterpret_cast<uint32_t>(&idt);
    asm volatile ("lidt %0" : : "m" (idtp));
}

// --- INTERRUPT WRAPPERS ---

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

// --- PIC FUNCTIONS ---

void init_pic() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFC);  // Enable IRQ0 (timer) and IRQ1 (PS/2 keyboard)
    outb(0xA1, 0xFF);  // Mask all slave interrupts initially
}

void init_pit() {
    uint32_t divisor = 1193180 / 100;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

void init_keyboard() {
    init_gdt();
    
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }
    
    idt_set_gate(0x20, reinterpret_cast<uint32_t>(timer_handler_wrapper), 0x08, 0x8E);
    idt_set_gate(0x21, reinterpret_cast<uint32_t>(keyboard_handler_wrapper), 0x08, 0x8E);
    
    idt_load();
    init_pic();
    init_pit();
    
    asm volatile ("sti");
    
    cout << "Interrupt system initialized with PS/2 keyboard support\n";
}
