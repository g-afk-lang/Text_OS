#include "pci.h"
#include "hardware_specs.h"
#include "interrupts.h"
#include "iostream_wrapper.h"
#include "kernel.h"
#include "stdlib_hooks.h"
#include "terminal_hooks.h"
#include "terminal_io.h"
#include "types.h"

// --- Low-level I/O Functions ---

static void outl(uint16_t port, uint32_t value) {
    asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// --- PCI Configuration Space Access ---

uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    // Create the configuration address
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | 0x80000000);

    // Write the address to the address port
    outl(PCI_CONFIG_ADDRESS, address);
    // Read the data from the data port
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | 0x80000000);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// --- xHCI Controller Scan ---

pci_device_info scan_for_xhci() {
    pci_device_info result = {false, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    cout << "Scanning PCI bus for xHCI controller...\n";

    for (int bus = 0; bus < 256; bus++) {
        for (int device = 0; device < 32; device++) {
            // Check if a device exists at this location
            uint32_t vendor_device_id = pci_read_config_dword(bus, device, 0, 0);
            if ((vendor_device_id & 0xFFFF) == 0xFFFF) {
                continue; // No device
            }

            uint32_t class_info = pci_read_config_dword(bus, device, 0, 8);
            uint8_t class_code = (class_info >> 24) & 0xFF;
            uint8_t subclass = (class_info >> 16) & 0xFF;
            uint8_t interface = (class_info >> 8) & 0xFF;

            // Check if this is an xHCI controller (USB 3.0)
            // Class 0x0C = Serial Bus Controller
            // Subclass 0x03 = USB Controller
            // Interface 0x30 = xHCI
            if (class_code == 0x0C && subclass == 0x03 && interface == 0x30) {
                uint16_t vendor_id = vendor_device_id & 0xFFFF;
                uint16_t device_id = (vendor_device_id >> 16) & 0xFFFF;

                cout << "Found xHCI controller!\n";
                cout << "  Vendor: 0x" << vendor_id << ", Device: 0x" << device_id << "\n";
                cout << "  Location: Bus " << bus << ", Device " << device << "\n";

                // Read BAR0 (Base Address Register 0)
                uint32_t bar0 = pci_read_config_dword(bus, device, 0, 0x10);

                result.found = true;
                result.bus = bus;
                result.device = device;
                result.function = 0;
                result.vendor_id = vendor_id;
                result.device_id = device_id;
                result.class_code = class_code;
                result.subclass = subclass;
                result.interface = interface;
                result.bar0 = bar0 & 0xFFFFFFF0; // Mask off lower 4 bits for memory-mapped BARs

                return result; // Found it, no need to search further
            }
        }
    }

    cout << "No xHCI controller found.\n";
    return result;
}


// --- Generic PCI Device Scanning and Info ---

uint16_t get_pci_command(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t temp = pci_read_config_dword(bus, slot, func, PCI_COMMAND_REGISTER);
    return (uint16_t)(temp & 0xFFFF);
}

void read_pci_bars(uint8_t bus, uint8_t slot, uint8_t func, struct pci_device* dev) {
    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_read_config_dword(bus, slot, func, 0x10 + i * 4);
    }
}

int check_device(uint8_t bus, uint8_t device) {
    uint32_t vendor_device = pci_read_config_dword(bus, device, 0, PCI_VENDOR_ID);
    uint16_t vendor = (uint16_t)(vendor_device & 0xFFFF);
    return (vendor != 0xFFFF);
}

void scan_pci() {
    struct pci_device dev;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            if (check_device(bus, device)) {
                uint32_t vendor_device = pci_read_config_dword(bus, device, 0, PCI_VENDOR_ID);
                dev.vendor_id = (uint16_t)(vendor_device & 0xFFFF);
                dev.device_id = (uint16_t)(vendor_device >> 16);

                dev.command = get_pci_command(bus, device, 0);

                read_pci_bars(bus, device, 0, &dev);

                cout << "Device found at Bus " << bus << ", Device " << device << "\n";
                cout << "Vendor ID: " << std::hex << dev.vendor_id << ", Device ID: " << dev.device_id << std::dec << "\n";
                cout << "Command Register: " << std::hex << dev.command << std::dec << "\n";

                for (int i = 0; i < 6; i++) {
                    cout << "BAR" << i << ": " << std::hex << dev.bar[i] << std::dec << "\n";

                    if (dev.bar[i] != 0) {
                        if (dev.bar[i] & 1) { // I/O Space
                            cout << "  Type: I/O Space\n";
                            cout << "  I/O Port Base Address: " << std::hex << (dev.bar[i] & 0xFFFFFFFC) << std::dec << "\n";
                        } else { // Memory Space
                            cout << "  Type: Memory Space\n";
                            uint8_t mem_type = (dev.bar[i] >> 1) & 3;
                            cout << "  Memory Type: " << (int)mem_type << "\n";
                            cout << "  Memory Base Address: " << std::hex << (dev.bar[i] & 0xFFFFFFF0) << std::dec << "\n";
                        }
                    }
                }
                cout << "\nPress enter to continue\n\n";
                char input[2];
                cin >> input;
                cout << "\n";
            }
        }
    }
}