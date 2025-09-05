#include "xhci.h"
#include "pci.h"
#include "terminal_io.h"
#include "iostream_wrapper.h"
#include "dma_memory.h"
#include "stdlib_hooks.h"

// --- Global Pointers to xHCI resources (Definitions) ---
volatile xhci_cap_regs_t* xhci_cap_regs;
volatile xhci_op_regs_t* xhci_op_regs;
volatile uint32_t* xhci_db_regs;
volatile xhci_port_regs_t* xhci_port_regs;

// Memory structures for xHCI (Definitions)
uint64_t* dcbaa;
xhci_trb_t* cmd_ring;

// External declaration for the global DMA manager instance
extern DMAManager dma_manager;

// External declaration for memset implementation from kernel.cpp

// --- Helper functions ---
static void* align_pointer(void* ptr, size_t alignment) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t offset = (alignment - (addr % alignment)) % alignment;
    return (void*)(addr + offset);
}


// Simple memset implementation (in case stdlib_hooks.h doesn't work)
static void* simple_memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

// --- Initialization Function ---
bool xhci_init() {
    cout << "Initializing xHCI USB 3.0 driver...\n";

    // 1. Find the xHCI controller on the PCI bus
    pci_device_info pci_device = scan_for_xhci();
    if (!pci_device.found) {
        cout << "Error: xHCI controller not found.\n";
        return false;
    }
    cout << "xHCI controller found at Bus " << (int)pci_device.bus << ", Dev " << (int)pci_device.device << "\n";
    
    // 2. Map MMIO registers with validation
    uint32_t base_addr = pci_device.bar0;
    cout << "Using base address: 0x" << base_addr << "\n";
    
    xhci_cap_regs = (xhci_cap_regs_t*)base_addr;
    
    // Validate that we can read from the capability registers
    uint16_t hci_version = xhci_cap_regs->hci_version;
    if (hci_version == 0x0000 || hci_version == 0xFFFF) {
        cout << "Error: Invalid xHCI version (0x" << hci_version << "). Bad base address?\n";
        return false;
    }
    cout << "xHCI Version: 0x" << hci_version << "\n";
    
    uint8_t cap_length = xhci_cap_regs->cap_length;
    if (cap_length == 0) {
        cout << "Error: Invalid capability length (" << (int)cap_length << ")\n";
        return false;
    }
    
    xhci_op_regs = (xhci_op_regs_t*)(base_addr + cap_length);
    xhci_db_regs = (uint32_t*)(base_addr + xhci_cap_regs->dboff);

    // 3. Check if controller is already halted
    cout << "Checking controller state...";
    uint32_t usb_sts = xhci_op_regs->usb_sts;
    cout << " USB Status: 0x" << usb_sts << "\n";
    
    if (!(usb_sts & 0x1)) {  // If not halted (HCHalted bit is not set)
        cout << "Halting controller...";
        xhci_op_regs->usb_cmd &= ~0x1; // Clear Run/Stop bit
        
        // Wait for halted with timeout
        int timeout = 1000000;
        while (!(xhci_op_regs->usb_sts & 0x1) && timeout > 0) {
            timeout--;
        }
        
        if (timeout == 0) {
            cout << " TIMEOUT! Controller failed to halt.\n";
            cout << "Final USB Status: 0x" << xhci_op_regs->usb_sts << "\n";
            return false;
        }
        cout << " OK\n";
    } else {
        cout << "Controller is already halted.\n";
    }

    // 4. Reset the controller with timeout and validation
    cout << "Resetting controller...";
    xhci_op_regs->usb_cmd |= 0x2; // Set Host Controller Reset bit
    
    // Wait for reset completion with timeout
    int reset_timeout = 10000000;
    while (reset_timeout > 0) {
        if (!(xhci_op_regs->usb_cmd & 0x2)) {  // Reset bit is cleared by hardware on completion
            break;
        }
        reset_timeout--;
        if (reset_timeout % 1000000 == 0) {
            cout << ".";
        }
    }
    
    if (reset_timeout == 0) {
        cout << " TIMEOUT!\n";
        cout << "Reset bit never cleared. Final CMD: 0x" << xhci_op_regs->usb_cmd << "\n";
        cout << "This usually means:\n";
        cout << "1. Wrong base address or MMIO not enabled\n";
        cout << "2. Controller not powered/enabled in BIOS\n";
        cout << "3. Hardware fault or virtual machine issue\n";
        return false;
    }
    cout << " OK\n";
    
    // 5. Set up Device Context Base Address Array Pointer (DCBAAP)
    uint32_t max_slots = xhci_cap_regs->hcs_params1 & 0xFF;
    cout << "Max device slots: " << max_slots << "\n";

    // Allocate and align the DCBAA
    size_t dcbaa_size = sizeof(uint64_t) * (max_slots + 1);
    void* dcbaa_raw = dma_manager.allocate_dma_buffer(dcbaa_size + 64);
    if (!dcbaa_raw) {
        cout << "Error: Failed to allocate raw buffer for DCBAA.\n";
        return false;
    }
    dcbaa = (uint64_t*)align_pointer(dcbaa_raw, 64);
    
    simple_memset(dcbaa, 0, dcbaa_size);
    xhci_op_regs->dcbaap = (uint64_t)dcbaa; // NOTE: This must be a PHYSICAL address!

    // 6. Set up Command Ring
    size_t cmd_ring_size = sizeof(xhci_trb_t) * 256; // 256 entries
    void* cmd_ring_raw = dma_manager.allocate_dma_buffer(cmd_ring_size + 64);
    if (!cmd_ring_raw) {
        cout << "Error: Failed to allocate raw buffer for Command Ring.\n";
        return false;
    }
    cmd_ring = (xhci_trb_t*)align_pointer(cmd_ring_raw, 64);
    simple_memset(cmd_ring, 0, cmd_ring_size);
    xhci_op_regs->crcr = (uint64_t)cmd_ring | 0x1; // Set Ring Cycle State and pointer (PHYSICAL address!)

    // 7. Set number of device slots enabled and start controller
    xhci_op_regs->config = max_slots;
    cout << "Starting controller...";
    xhci_op_regs->usb_cmd |= 0x1; // Set Run/Stop bit
    while (xhci_op_regs->usb_sts & 0x1); // Wait for HCHalted bit to be 0
    cout << " OK\n";
    
    // 8. Ring the command doorbell (for the host controller, which is slot 0)
    xhci_db_regs[0] = 0; 
    
    cout << "xHCI driver initialized successfully.\n";

    
    // 9. Enumerate ports
    uint8_t num_ports = (xhci_cap_regs->hcs_params1 >> 24) & 0xFF;
    cout << "Number of USB ports: " << (int)num_ports << "\n";
    xhci_port_regs = (xhci_port_regs_t*)((uintptr_t)xhci_op_regs + 0x400);
    
    bool has_connected_devices = false;
    for (uint8_t i = 0; i < num_ports; ++i) {
        volatile xhci_port_regs_t* port = &xhci_port_regs[i];
        if (port->portsc & 0x1) { // Check CCS (Current Connect Status) bit
            cout << "Device connected on Port " << (int)(i + 1) << "\n";
            has_connected_devices = true;
        }
    }
    
    // If we have connected USB devices, activate USB keyboard override
    if (has_connected_devices) {
        //enable_usb_keyboard_override();
    }

    return true;
}