#ifndef PCI_BAR_READER_H
#define PCI_BAR_READER_H

#include "types.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCI_COMMAND_REGISTER 0x04
#define PCI_VENDOR_ID 0x00

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

uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
uint16_t get_pci_command(uint8_t bus, uint8_t slot, uint8_t func);
void read_pci_bars(uint8_t bus, uint8_t slot, uint8_t func, struct pci_device* dev);
int check_device(uint8_t bus, uint8_t device);
void scan_pci();

#endif