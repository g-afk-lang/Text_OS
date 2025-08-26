/*
 * SATA Identify Command Implementation
 * For use with the SATA Controller Debug Utility
 */

#include "kernel.h"
#include "iostream_wrapper.h"
#include "pci.h"
#include "stdlib_hooks.h"
#include "sata.h"

 // Make sure we have access to all the AHCI register definitions
 // These should match the definitions in your main code
#define AHCI_CAP        0x00  // Host Capabilities
#define AHCI_GHC        0x04  // Global Host Control
#define AHCI_IS         0x08  // Interrupt Status
#define AHCI_PI         0x0C  // Ports Implemented
#define AHCI_VS         0x10  // Version
#define AHCI_PORT_BASE  0x100 // Port registers base
#define AHCI_PORT_SIZE  0x80  // Size of port register space

// Port registers offsets (add to PORT_BASE + port_num * PORT_SIZE)
#define PORT_CLB        0x00  // Command List Base Address
#define PORT_CLBU       0x04  // Command List Base Address Upper 32 bits
#define PORT_FB         0x08  // FIS Base Address
#define PORT_FBU        0x0C  // FIS Base Address Upper 32 bits
#define PORT_IS         0x10  // Interrupt Status
#define PORT_IE         0x14  // Interrupt Enable
#define PORT_CMD        0x18  // Command and Status
#define PORT_TFD        0x20  // Task File Data
#define PORT_SIG        0x24  // Signature
#define PORT_SSTS       0x28  // SATA Status
#define PORT_SCTL       0x2C  // SATA Control
#define PORT_SERR       0x30  // SATA Error
#define PORT_SACT       0x34  // SATA Active
#define PORT_CI         0x38  // Command Issue

// FIS types
#define FIS_TYPE_REG_H2D    0x27    // Register FIS - Host to Device
#define FIS_TYPE_REG_D2H    0x34    // Register FIS - Device to Host
#define FIS_TYPE_DMA_ACT    0x39    // DMA Activate FIS
#define FIS_TYPE_DMA_SETUP  0x41    // DMA Setup FIS
#define FIS_TYPE_DATA       0x46    // Data FIS
#define FIS_TYPE_BIST       0x58    // BIST Activate FIS
#define FIS_TYPE_PIO_SETUP  0x5F    // PIO Setup FIS
#define FIS_TYPE_DEV_BITS   0xA1    // Set Device Bits FIS

// ATA commands
#define ATA_CMD_IDENTIFY    0xEC

// Structure for H2D Register FIS
struct FIS_REG_H2D {
    uint8_t  fis_type;      // FIS_TYPE_REG_H2D
    uint8_t  pmport : 4;      // Port multiplier port
    uint8_t  rsv0 : 3;        // Reserved
    uint8_t  c : 1;           // 1: Command, 0: Control
    uint8_t  command;       // Command register
    uint8_t  featurel;      // Feature register, 7:0

    uint8_t  lba0;          // LBA low register, 7:0
    uint8_t  lba1;          // LBA mid register, 15:8
    uint8_t  lba2;          // LBA high register, 23:16
    uint8_t  device;        // Device register

    uint8_t  lba3;          // LBA register, 31:24
    uint8_t  lba4;          // LBA register, 39:32
    uint8_t  lba5;          // LBA register, 47:40
    uint8_t  featureh;      // Feature register, 15:8

    uint8_t  countl;        // Count register, 7:0
    uint8_t  counth;        // Count register, 15:8
    uint8_t  icc;           // Isochronous command completion
    uint8_t  control;       // Control register

    uint8_t  rsv1[4];       // Reserved
} __attribute__((packed));

// Command List Structure (1K aligned)
struct HBA_CMD_HEADER {
    uint8_t  cfl : 5;         // Command FIS length in DWORDS, 2 ~ 16
    uint8_t  a : 1;           // ATAPI
    uint8_t  w : 1;           // Write, 1: H2D, 0: D2H
    uint8_t  p : 1;           // Prefetchable

    uint8_t  r : 1;           // Reset
    uint8_t  b : 1;           // BIST
    uint8_t  c : 1;           // Clear busy upon R_OK
    uint8_t  rsv0 : 1;        // Reserved
    uint8_t  pmp : 4;         // Port multiplier port

    uint16_t prdtl;         // Physical region descriptor table length in entries

    volatile uint32_t prdbc; // Physical region descriptor byte count transferred

    uint32_t ctba;          // Command table descriptor base address
    uint32_t ctbau;         // Command table descriptor base address upper 32 bits

    uint32_t rsv1[4];       // Reserved
} __attribute__((packed));

// Command Table Structure
struct HBA_CMD_TBL {
    uint8_t  cfis[64];      // Command FIS
    uint8_t  acmd[16];      // ATAPI command, 12 or 16 bytes
    uint8_t  rsv[48];       // Reserved

    // PRDT entries follow
} __attribute__((packed));

// Physical Region Descriptor Table Entry
struct HBA_PRDT_ENTRY {
    uint32_t dba;           // Data base address
    uint32_t dbau;          // Data base address upper 32 bits
    uint32_t rsv0;          // Reserved

    uint32_t dbc : 22;        // Byte count, 4M max
    uint32_t rsv1 : 9;        // Reserved
    uint32_t i : 1;           // Interrupt on completion
} __attribute__((packed));

// Use the read_mem32 function from the original code
// If needed, we can implement it locally
inline uint32_t read_mem32(uint64_t addr) {
    return *((volatile uint32_t*)addr);
}


// Custom memory allocation for aligned memory
void* aligned_alloc_custom(size_t alignment, size_t size) {
    // Simple implementation - should be replaced with a proper allocator
    void* ptr = malloc(size + alignment);
    if (!ptr) return nullptr;

    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return (void*)aligned;
}

// Function to check if a port is idle and ready to accept commands
bool is_port_ready(uint64_t port_addr) {
    uint32_t cmd = read_mem32(port_addr + PORT_CMD);
    uint32_t tfd = read_mem32(port_addr + PORT_TFD);

    // Check if command engine is running
    if ((cmd & (1 << 15)) || (cmd & (1 << 14))) {
        return false;
    }

    // Check if device is busy
    if (tfd & 0x80) {
        return false;
    }

    return true;
}

// Function to start command engine
bool start_cmd_engine(uint64_t port_addr) {
    uint32_t cmd = read_mem32(port_addr + PORT_CMD);

    // Set ST bit if not already set
    if (!(cmd & 1)) {
        cmd |= 1;
        *((volatile uint32_t*)(port_addr + PORT_CMD)) = cmd;
    }

    // Wait for command engine to start
    for (int i = 0; i < 1000; i++) {
        if (read_mem32(port_addr + PORT_CMD) & 1) {
            return true;
        }
        // Small delay
        for (volatile int j = 0; j < 1000000; j++);
    }

    return false;
}

// Function to stop command engine
bool stop_cmd_engine(uint64_t port_addr) {
    uint32_t cmd = read_mem32(port_addr + PORT_CMD);

    // Clear ST bit if set
    if (cmd & 1) {
        cmd &= ~1;
        *((volatile uint32_t*)(port_addr + PORT_CMD)) = cmd;
    }

    // Wait for command engine to stop
    for (int i = 0; i < 1000; i++) {
        if (!(read_mem32(port_addr + PORT_CMD) & (1 << 15))) {
            return true;
        }
        // Small delay
        for (volatile int j = 0; j < 1000000; j++);
    }

    return false;
}
