#include "hardware_specs.h"
#include "interrupts.h"
#include "iostream_wrapper.h"
#include "kernel.h"
#include "stdlib_hooks.h"
#include "terminal_hooks.h"
#include "terminal_io.h"
#include "types.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCI_COMMAND_REGISTER 0x04
#define PCI_VENDOR_ID 0x00

// Direct implementation of outl/inl functions
void direct_outl(uint16_t port, uint32_t value) {
    asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

uint32_t direct_inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

struct pci_device {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;
    uint32_t bar[6];
};

uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    
    direct_outl(PCI_CONFIG_ADDRESS, address);
    return direct_inl(PCI_CONFIG_DATA);
}

void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    
    direct_outl(PCI_CONFIG_ADDRESS, address);
    direct_outl(PCI_CONFIG_DATA, value);
}

uint16_t get_pci_command(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t temp = pci_read_config_dword(bus, slot, func, PCI_COMMAND_REGISTER);
    return (uint16_t)(temp & 0xFFFF);
}

void read_pci_bars(uint8_t bus, uint8_t slot, uint8_t func, struct pci_device* dev) {
    for (int i = 0; i < 6; i++) {
        uint32_t bar_value = pci_read_config_dword(bus, slot, func, 0x10 + i * 4);
        dev->bar[i] = bar_value;
    }
}

int check_device(uint8_t bus, uint8_t device) {
    uint16_t vendor;
    uint32_t vendor_device = pci_read_config_dword(bus, device, 0, PCI_VENDOR_ID);
    vendor = (uint16_t)(vendor_device & 0xFFFF);
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
                        if (dev.bar[i] & 1) {
                            cout << "  Type: I/O Space\n";
                            cout << "  I/O Port Base Address: " << std::hex << (dev.bar[i] & 0xFFFFFFFC) << std::dec << "\n";
                        } else {
                            cout << "  Type: Memory Space\n";
                            uint8_t mem_type = (dev.bar[i] >> 1) & 3;
                            cout << "  Memory Type: " << int(mem_type) << "\n";
                            cout << "  Memory Base Address: " << std::hex << (dev.bar[i] & 0xFFFFFFF0) << std::dec << "\n";
                        }
                    }
                }
                cout << "\nPress enter to continue\n\n";
                char input[1];
                cin >> input;
                cout << "\n";
            }
        }
    }
}

