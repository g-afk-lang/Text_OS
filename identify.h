/*

 * SATA Controller IDENTIFY Command Implementation

 * For bare metal AMD64 environment

 */

#ifndef IDENTIFY_H

#define IDENTIFY_H



#include "kernel.h" // Assumed to provide basic types like uintXX_t

#include "iostream_wrapper.h" // Assumed to provide a 'cout' like object



 // Port registers offsets - duplicate from main file to avoid dependency issues

 // DEBUG: Ensure these offsets match the AHCI specification (Section 3.3.x)

#define PORT_CLB         0x00  // Command List Base Address (Lower 32 bits)

#define PORT_CLBU        0x04  // Command List Base Address Upper 32 bits

#define PORT_FB          0x08  // FIS Base Address (Lower 32 bits)

#define PORT_FBU         0x0C  // FIS Base Address Upper 32 bits

#define PORT_IS          0x10  // Interrupt Status

#define PORT_IE          0x14  // Interrupt Enable

#define PORT_CMD         0x18  // Command and Status

#define PORT_TFD         0x20  // Task File Data

#define PORT_SIG         0x24  // Signature

#define PORT_SSTS        0x28  // SATA Status (SCR0: SStatus)

#define PORT_SCTL        0x2C  // SATA Control (SCR2: SControl)

#define PORT_SERR        0x30  // SATA Error (SCR1: SError)

#define PORT_SACT        0x34  // SATA Active (SCR3: SActive)

#define PORT_CI          0x38  // Command Issue



// Simple memory access - duplicate from main file

// DEBUG: Ensure 'volatile' is used correctly for MMIO. This looks okay.

// DEBUG: Ensure the addresses used are correct physical addresses mapped appropriately.

inline uint32_t read_mem32(uint64_t addr) {

    return *((volatile uint32_t*)addr);

}



inline void write_mem32(uint64_t addr, uint32_t value) {

    *((volatile uint32_t*)addr) = value;

}





// Useful constants for AHCI Port Command and Status Register (PORT_CMD)

// DEBUG: Verify these bit definitions against AHCI spec (Section 3.3.8)

#define HBA_PORT_CMD_ST     0x0001 // Start (bit 0) - Controller processes command list

#define HBA_PORT_CMD_FRE    0x0010 // FIS Receive Enable (bit 4) - HBA can receive FISes

#define HBA_PORT_CMD_FR     0x4000 // FIS Receive Running (bit 14) - Status

#define HBA_PORT_CMD_CR     0x8000 // Command List Running (bit 15) - Status



// FIS types

// DEBUG: Verify these type codes against AHCI spec (Section 4.1)

#define FIS_TYPE_REG_H2D    0x27    // Register FIS - host to device

#define FIS_TYPE_REG_D2H    0x34    // Register FIS - device to host

#define FIS_TYPE_DMA_ACT    0x39    // DMA activate FIS - device to host

#define FIS_TYPE_DMA_SETUP  0x41    // DMA setup FIS - bidirectional

#define FIS_TYPE_DATA       0x46    // Data FIS - bidirectional

#define FIS_TYPE_BIST       0x58    // BIST activate FIS - bidirectional

#define FIS_TYPE_PIO_SETUP  0x5F    // PIO setup FIS - device to host

#define FIS_TYPE_DEV_BITS   0xA1    // Set device bits FIS - device to host



// ATA commands

// DEBUG: Verify command code against ATA/ATAPI Command Set (ACS) spec

#define ATA_CMD_IDENTIFY         0xEC    // IDENTIFY DEVICE
#define ATA_CMD_READ_DMA         0xC8    // READ DMA (LBA28)
#define ATA_CMD_READ_DMA_EXT     0x25    // READ DMA EXT (LBA48)
#define ATA_CMD_WRITE_DMA        0xCA    // WRITE DMA (LBA28)
#define ATA_CMD_WRITE_DMA_EXT    0x35    // WRITE DMA EXT (LBA48)
#define ATA_CMD_FLUSH_CACHE      0xE7    // FLUSH CACHE
#define ATA_CMD_FLUSH_CACHE_EXT  0xEA    // FLUSH CACHE EXT



// AHCI command list structure (Command Header)

// DEBUG: Verify structure layout and size against AHCI spec (Section 5.5)

typedef struct {

    // DW0

    uint8_t  cfl : 5;        // Command FIS Length (in DWORDS, 2-16)

    uint8_t  a : 1;          // ATAPI

    uint8_t  w : 1;          // Write (0: H2D, 1: D2H) <-- Direction of DMA data transfer if PRDT used

    uint8_t  p : 1;          // Prefetchable

    uint8_t  r : 1;          // Reset

    uint8_t  b : 1;          // BIST

    uint8_t  c : 1;          // Clear Busy upon R_OK

    uint8_t  reserved0 : 1; // Reserved

    uint16_t prdtl;          // Physical Region Descriptor Table Length (Entries)



    // DW1

    volatile uint32_t prdbc; // Physical Region Descriptor Byte Count (bytes transferred)



    // DW2, DW3

    uint64_t ctba;           // Command Table Base Address (must be 128-byte aligned)



    // DW4-DW7

    uint32_t reserved1[4]; // Reserved

} __attribute__((packed)) hba_cmd_header_t; // Total size: 32 bytes



// FIS structure for H2D Register

// DEBUG: Verify structure layout and size against AHCI spec (Section 4.2.1)

typedef struct {

    // DW0

    uint8_t  fis_type;    // FIS_TYPE_REG_H2D (0x27)

    uint8_t  pmport : 4;    // Port multiplier port

    uint8_t  reserved0 : 3; // Reserved

    uint8_t  c : 1;          // 1: Command, 0: Control

    uint8_t  command;     // Command register (e.g., ATA_CMD_IDENTIFY)

    uint8_t  featurel;    // Feature register, 7:0



    // DW1

    uint8_t  lba0;        // LBA low register, 7:0

    uint8_t  lba1;        // LBA mid register, 15:8

    uint8_t  lba2;        // LBA high register, 23:16

    uint8_t  device;      // Device register



    // DW2

    uint8_t  lba3;        // LBA register, 31:24

    uint8_t  lba4;        // LBA register, 39:32

    uint8_t  lba5;        // LBA register, 47:40

    uint8_t  featureh;    // Feature register, 15:8



    // DW3

    uint8_t  countl;      // Count register, 7:0

    uint8_t  counth;      // Count register, 15:8

    uint8_t  icc;         // Isochronous command completion

    uint8_t  control;     // Control register



    // DW4

    uint8_t  reserved1[4]; // Reserved

} __attribute__((packed)) fis_reg_h2d_t; // Total size: 20 bytes



// Physical Region Descriptor Table Entry

// DEBUG: Verify structure layout and size against AHCI spec (Section 5.3)

typedef struct {

    // DW0, DW1

    uint64_t dba;           // Data Base Address (must be 2-byte aligned)



    // DW2

    uint32_t reserved0;   // Reserved



    // DW3

    uint32_t dbc : 22;       // Byte Count (0-based, max 4MB-1)

    uint32_t reserved1 : 9; // Reserved

    uint32_t i : 1;          // Interrupt on Completion

} __attribute__((packed)) hba_prdt_entry_t; // Total size: 16 bytes



// Command Table Structure

// DEBUG: Verify structure layout and size against AHCI spec (Section 5.4)

// DEBUG: Base address must be 128-byte aligned.

typedef struct {

    uint8_t  cfis[64];      // Command FIS (should match hba_cmd_header_t.cfl)

    uint8_t  acmd[16];      // ATAPI command (if hba_cmd_header_t.a == 1)

    uint8_t  reserved[48];  // Reserved

    hba_prdt_entry_t prdt[1]; // PRDT entries (1 to 65535) - Variable size!

} __attribute__((packed)) hba_cmd_tbl_t;



// Simple memory allocation for demonstration

// DEBUG: Ensure these buffers are in memory physically accessible by the AHCI controller (DMA).

// DEBUG: Ensure alignment requirements are met.

#define SECTOR_SIZE 512
#define MAX_PRDT_ENTRIES 8 // Allow for potentially fragmented buffers later (though not used yet)
#define MAX_TRANSFER_SECTORS 128 // Limit transfer size for simplicity (128*512 = 64KB)

// Command list (array of Command Headers) must be 1KB aligned. Max 32 slots.
static uint8_t cmd_list_buffer[32 * sizeof(hba_cmd_header_t)] __attribute__((aligned(1024)));

// Received FIS buffer must be 256-byte aligned.
static uint8_t fis_buffer[256] __attribute__((aligned(256)));

// Command Table buffer must be 128-byte aligned.
// Size needs to accommodate CFIS(64) + ACMD(16) + Resvd(48) + N * PRDT(16)
#define CMD_TABLE_STATIC_SIZE (64 + 16 + 48)
#define CMD_TABLE_TOTAL_SIZE (CMD_TABLE_STATIC_SIZE + MAX_PRDT_ENTRIES * sizeof(hba_prdt_entry_t))
static uint8_t cmd_table_buffer[CMD_TABLE_TOTAL_SIZE] __attribute__((aligned(128)));

// Data buffer must be 2-byte aligned (word aligned). Used for IDENTIFY and simple string I/O.
static uint8_t data_buffer[MAX_TRANSFER_SECTORS * SECTOR_SIZE] __attribute__((aligned(2)));


// Global flag indicating LBA48 support - should be set after IDENTIFY
static bool lba48_available = false;


// Wait for a bit to clear in the specified register

// DEBUG: This is a busy-wait loop. In a real system, use a proper timer or sleep mechanism.

//        The inner loop count (100000) is arbitrary and CPU-speed dependent.

//        Timeout value might need adjustment based on hardware.

int wait_for_clear(uint64_t reg_addr, uint32_t mask, int timeout_ms) {

    // DEBUG: Consider adding a check for timeout_ms <= 0

    for (int i = 0; i < timeout_ms * 10; i++) { // Arbitrary multiplier for delay loop

        if ((read_mem32(reg_addr) & mask) == 0) {

            return 0;  // Success

        }

        // Simple delay - replace with platform-specific delay/yield if possible

        for (volatile int j = 0; j < 100000; j++);

    }

    return -1;  // Timeout

}


// Find the first available command slot
// Returns slot number 0-31, or -1 if none available
int find_free_command_slot(uint64_t port_addr) {
    uint32_t ci_val = read_mem32(port_addr + PORT_CI);
    uint32_t sact_val = read_mem32(port_addr + PORT_SACT);
    uint32_t busy_slots = ci_val | sact_val;
    for (int i = 0; i < 32; ++i) { // Assuming HBA supports up to 32 commands (check HBA_CAP.NCS)
        if (!((busy_slots >> i) & 1)) {
            return i;
        }
    }
    return -1; // No free slot found
}


// Generic function to start an AHCI command
// Assumes buffers are set up and port checks passed
// Returns 0 on success, negative on error
int issue_ahci_command(uint64_t port_addr, int slot) {
    // Wait for the port to be idle (not busy - TFD.BSY=0, TFD.DRQ=0)
    // BSY (bit 7), DRQ (bit 3)
    if (wait_for_clear(port_addr + PORT_TFD, (1 << 7) | (1 << 3), 1000) < 0) { // Timeout 1 second
        cout << "ERROR: Port is busy before command issue (TFD=0x" << read_mem32(port_addr + PORT_TFD) << "). Cannot send command.\n";
        // DEBUG: Might need a port reset here.
        return -6;
    }

    // Issue the command by setting the corresponding bit in PORT_CI
    write_mem32(port_addr + PORT_CI, (1 << slot));

    return 0; // Command issued successfully (but not yet completed)
}

// Generic function to wait for command completion and check status
// Returns 0 on success, negative on error
int wait_for_ahci_completion(uint64_t port_addr, int slot, hba_cmd_header_t* cmd_header, uint32_t expected_bytes) {
    // Wait for command completion by polling PORT_CI bit for the slot to clear
    // Timeout needs to be generous (e.g., 5 seconds for read/write/identify)
    if (wait_for_clear(port_addr + PORT_CI, (1 << slot), 5000) < 0) { // Timeout 5 seconds
        cout << "ERROR: Command timed out waiting for CI bit " << slot << " to clear.\n";
        // DEBUG: Check SACT as well. If SACT is also clear, maybe it completed but CI wasn't seen? Unlikely.
        // DEBUG: Consider attempting a port reset or controller reset on timeout.
        return -7;
    }

    // Check for errors in the Task File Data register (PORT_TFD)
    // ERR (bit 0) or DF (bit 5) indicate an error.
    uint32_t tfd = read_mem32(port_addr + PORT_TFD);
    if (tfd & ((1 << 0) | (1 << 5))) { // Check ERR or DF bits
        cout << "ERROR: Command failed. TFD status: ";
        // Print status bits (based on ATA spec)
        if (tfd & 0x80) cout << "BSY "; // Busy
        if (tfd & 0x40) cout << "DRDY "; // Device Ready
        if (tfd & 0x20) cout << "DF ";  // Device Fault
        if (tfd & 0x10) cout << "DSC "; // Device Seek Complete (Obsolete)
        if (tfd & 0x08) cout << "DRQ "; // Data Request
        if (tfd & 0x04) cout << "CORR "; // Corrected Data (Obsolete)
        if (tfd & 0x02) cout << "IDX "; // Index (Obsolete)
        if (tfd & 0x01) cout << "ERR "; // Error
        cout << "(Raw TFD: 0x" << tfd << ")\n";

        // Check SError register for more details if ERR bit is set
        if (tfd & 0x01) {
            uint32_t serr_val = read_mem32(port_addr + PORT_SERR);
            cout << " SError: 0x" << serr_val << "\n";
            // Decode SERR bits here based on AHCI spec 3.3.11 if needed
            // Write 1s to clear SERR bits
            write_mem32(port_addr + PORT_SERR, serr_val);
        }
        return -8;
    }

    // Check how many bytes were actually transferred (optional but good practice)
    uint32_t bytes_transferred = cmd_header->prdbc;
    if (expected_bytes > 0 && bytes_transferred != expected_bytes) {
        cout << "WARNING: Expected " << expected_bytes << " bytes, but received/transferred " << bytes_transferred << " bytes.\n";
        // This might be an error depending on the command. For read/write it usually is.
        return -9; // Treat mismatch as an error for I/O
    }
    else if (expected_bytes == 0 && bytes_transferred != 0) {
        cout << "DEBUG: Command had PRDTL=0 but PRDBC=" << bytes_transferred << "?\n";
        // Not necessarily an error for commands like FLUSH CACHE.
    }

    return 0; // Success
}


// Common setup steps before issuing any command
// Returns 0 on success, negative on error
int prepare_port_for_command(uint64_t port_addr, int port) {
    // Ensure FIS Receive is Enabled (PORT_CMD.FRE, bit 4)
    uint32_t port_cmd = read_mem32(port_addr + PORT_CMD);
    if (!(port_cmd & HBA_PORT_CMD_FRE)) {
        cout << "WARNING: Port " << port << " FIS Receive (FRE) is not enabled. Attempting to enable.\n";
        write_mem32(port_addr + PORT_CMD, port_cmd | HBA_PORT_CMD_FRE);
        for (volatile int j = 0; j < 10000; j++); // Short delay
        port_cmd = read_mem32(port_addr + PORT_CMD); // Re-read
        if (!(port_cmd & HBA_PORT_CMD_FRE)) {
            cout << "ERROR: Failed to enable FIS Receive (FRE) on port " << port << "\n";
            return -2;
        }
    }

    // Ensure Port is Started (PORT_CMD.ST, bit 0)
    if (!(port_cmd & HBA_PORT_CMD_ST)) {
        cout << "WARNING: Port " << port << " Start (ST) is not set. Attempting to start.\n";
        write_mem32(port_addr + PORT_CMD, port_cmd | HBA_PORT_CMD_ST);
        for (volatile int j = 0; j < 10000; j++); // Short delay
        port_cmd = read_mem32(port_addr + PORT_CMD); // Re-read
        if (!(port_cmd & HBA_PORT_CMD_ST)) {
            cout << "ERROR: Failed to start port " << port << " (ST bit)\n";
            return -3;
        }
    }

    // Check device presence and PHY communication (PORT_SSTS)
    // DET (bits 3:0) should be 3 (Device presence and Phy communication)
    // IPM (bits 11:8) should be 1 (Active state)
    uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
    uint8_t det = ssts & 0x0F;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    if (det != 3) {
        cout << "ERROR: No device detected or communication not established on port " << port << ". DET = " << (int)det << "\n";
        return -4;
    }
    if (ipm != 1) {
        cout << "WARNING: Port " << port << " is not in active state (IPM = " << (int)ipm << "). May not respond.\n";
        // Attempting to wake? Requires SCTL writes. For now, just warn.
    }

    // Clear any pending interrupt status bits for the port
    write_mem32(port_addr + PORT_IS, 0xFFFFFFFF); // Write 1s to clear bits

    // Clear any SATA error bits
    uint32_t serr = read_mem32(port_addr + PORT_SERR);
    if (serr != 0) {
        write_mem32(port_addr + PORT_SERR, serr); // Write 1s to clear bits
    }

    return 0; // Port is ready
}

// Function to display IDENTIFY data in a readable format

// DEBUG: Assumes iostream_wrapper can handle basic types and C-style strings.

// DEBUG: Assumes identify_data_buffer contains valid data after successful command.

void display_identify_data(uint16_t* data) {

    // DEBUG: Check for null pointer?

    if (!data) {

        cout << "ERROR: display_identify_data called with null pointer.\n";

        return;

    }



    char model_number[41];  // Model number is 40 bytes (20 words, offset 27)

    char serial_number[21]; // Serial number is 20 bytes (10 words, offset 10)



    // Extract Model Number (Words 27-46) - Characters are swapped in each word

    for (int i = 0; i < 20; i++) {

        uint16_t word = data[27 + i];

        model_number[i * 2] = (char)(word >> 8); // High byte first

        model_number[i * 2 + 1] = (char)(word & 0xFF); // Low byte second

    }

    model_number[40] = '\0'; // Null terminate



    // Extract Serial Number (Words 10-19) - Characters are swapped

    for (int i = 0; i < 10; i++) {

        uint16_t word = data[10 + i];

        serial_number[i * 2] = (char)(word >> 8);

        serial_number[i * 2 + 1] = (char)(word & 0xFF);

    }

    serial_number[20] = '\0'; // Null terminate



    // Trim trailing spaces (common in IDENTIFY data)

    for (int i = 39; i >= 0 && model_number[i] == ' '; i--) model_number[i] = '\0';

    for (int i = 19; i >= 0 && serial_number[i] == ' '; i--) serial_number[i] = '\0';



    cout << "IDENTIFY Device Information:\n";

    cout << "--------------------------\n";

    cout << "Model Number:  [" << model_number << "]\n"; // Brackets help see spaces

    cout << "Serial Number: [" << serial_number << "]\n";



    // Capabilities Word (Word 49)

    cout << "Supports LBA:  " << ((data[49] & (1 << 9)) ? "Yes" : "No") << "\n";



    // Command Set/Feature Support Words (Word 82, 83, 84, etc.)

    lba48_available = (data[83] & (1 << 10)); // Set global flag
    cout << "Supports LBA48:" << (lba48_available ? "Yes" : "No") << "\n";



    if (lba48_available) {

        // LBA48 Max Sectors (Words 100-103)

        uint64_t max_lba48 =

            ((uint64_t)data[100]) |

            ((uint64_t)data[101] << 16) |

            ((uint64_t)data[102] << 32) |

            ((uint64_t)data[103] << 48);



        // DEBUG: The iostream_wrapper limitation workaround.

        //        This might still overflow 'unsigned int' if max_lba48 >= 2^64.

        //        A proper 64-bit print function is better if available.

        unsigned int lba_high = (unsigned int)(max_lba48 >> 32);

        unsigned int lba_low = (unsigned int)(max_lba48 & 0xFFFFFFFF);



        cout << "LBA48 Max Sectors: ";

        if (lba_high > 0) {

            // NOTE: This assumes iostream_wrapper can print unsigned int.

            cout << lba_high << " * 2^32 + ";

        }

        //cout << lba_low << " (" << (unsigned long long)max_lba48 << " total)\n"; // Try printing uint64_t if possible



        // Calculate capacity (assuming 512 bytes/sector)

        // DEBUG: Potential overflow if max_lba48 * 512 exceeds uint64_t max. Unlikely with current tech.

        // DEBUG: Integer division truncates. Result is in GiB (1024^3).

        uint64_t total_bytes = max_lba48 * SECTOR_SIZE;

        unsigned int gib_capacity = (unsigned int)(total_bytes / (1024ULL * 1024 * 1024));

        cout << "Capacity (approx): " << gib_capacity << " GiB\n";



    }

    else if (data[49] & (1 << 9)) { // Check LBA support again

        // LBA28 Max Sectors (Words 60-61)

        unsigned int max_lba28 =

            ((unsigned int)data[60]) |

            ((unsigned int)data[61] << 16);

        cout << "LBA28 Max Sectors: " << max_lba28 << "\n";



        // Calculate capacity

        // DEBUG: Use unsigned long long (uint64_t) for intermediate calculation to avoid overflow.

        uint64_t total_bytes = (uint64_t)max_lba28 * SECTOR_SIZE;

        unsigned int gib_capacity = (unsigned int)(total_bytes / (1024ULL * 1024 * 1024));

        // Calculate remaining MiB more carefully

        unsigned int mib_remainder = (unsigned int)((total_bytes % (1024ULL * 1024 * 1024)) / (1024 * 1024));

        cout << "Capacity (approx): " << gib_capacity << "." << (mib_remainder * 100 / 1024) << " GiB\n"; // Rough decimal

    }

    else {

        cout << "CHS addressing only (not supported by this display logic).\n";

    }



    // SATA Capabilities (Word 76)

    if (data[76] != 0 && data[76] != 0xFFFF) { // Check if word is valid

        cout << "SATA Gen Supported: ";

        if (data[76] & (1 << 3)) cout << "3 (6.0 Gbps) ";

        if (data[76] & (1 << 2)) cout << "2 (3.0 Gbps) ";

        if (data[76] & (1 << 1)) cout << "1 (1.5 Gbps) ";

        cout << "\n";

    }



    // Features (Words 82-87)

    cout << "Features:\n";

    if (data[82] & (1 << 0))  cout << "  - SMART supported\n";

    if (data[82] & (1 << 1))  cout << "  - Security Mode supported\n";

    if (data[82] & (1 << 3))  cout << "  - Power Management supported\n";

    if (data[82] & (1 << 5))  cout << "  - Write Cache supported\n";

    if (data[82] & (1 << 6))  cout << "  - Look-ahead supported\n";

    // Word 83

    if (data[83] & (1 << 10)) cout << "  - LBA48 supported\n"; // Already checked, but good to list

    if (data[83] & (1 << 12)) cout << "  - AAM supported\n";   // Automatic Acoustic Management

    if (data[83] & (1 << 13)) cout << "  - SET MAX security extension supported\n";

    // Word 84

    if (data[84] & (1 << 0))  cout << "  - Device Configuration Overlay (DCO) supported\n";

    if (data[84] & (1 << 5))  cout << "  - NCQ supported\n"; // Native Command Queuing

    // Word 85

    if (data[85] & (1 << 0))  cout << "  - General Purpose Logging (GPL) supported\n";

    if (data[85] & (1 << 4))  cout << "  - Write DMA Queued supported\n"; // If NCQ

    if (data[85] & (1 << 5))  cout << "  - Read DMA Queued supported\n";  // If NCQ

    // Word 86

    if (data[86] & (1 << 13)) cout << "  - IDLE IMMEDIATE with UNLOAD supported\n";

    // Word 87

    if (data[87] & (1 << 14)) cout << "  - World Wide Name (WWN) supported\n";

    // Word 78 for SATA features

    if (data[78] & (1 << 2))  cout << "  - NCQ Management supported\n";

    if (data[78] & (1 << 8))  cout << "  - Software Settings Preservation supported\n";

    if (data[78] & (1 << 9))  cout << "  - Hardware Feature Control supported\n";

    if (data[78] & (1 << 10)) cout << "  - Device Initiated Power Management (DIPM) supported\n";

}



// Main function to send IDENTIFY DEVICE command

// Returns 0 on success, negative on error

int send_identify_command(uint64_t ahci_base, int port) {

    // DEBUG: Validate port number against HBA capabilities (e.g., read HBA_CAP register)

    uint64_t port_addr = ahci_base + 0x100 + (port * 0x80);


    int prep_status = prepare_port_for_command(port_addr, port);
    if (prep_status < 0) {
        return prep_status; // Propagate error
    }


    // Find an empty command slot
    int slot = find_free_command_slot(port_addr);
    if (slot < 0) {
        cout << "ERROR: No free command slot found on port " << port << ".\n";
        return -5;
    }


    // --- Setup Command Structures ---

    // DEBUG: Ensure physical addresses are used for DMA. Cast virtual addresses to physical if needed.

    //        This code assumes identity mapping or that buffers are already in physical memory.

    uint64_t cmd_list_phys = (uint64_t)cmd_list_buffer; // DEBUG: Replace with actual physical address if different
    uint64_t fis_buffer_phys = (uint64_t)fis_buffer;   // DEBUG: Replace with actual physical address if different
    uint64_t cmd_table_phys = (uint64_t)cmd_table_buffer; // DEBUG: Replace with actual physical address if different
    uint64_t identify_data_phys = (uint64_t)data_buffer; // DEBUG: Using generic data buffer now


    // --- Program HBA Registers (BEFORE setting up command details) ---

    // Set the command list base and FIS base addresses
    // *** THIS IS CRITICAL - DO NOT SKIP ***
    write_mem32(port_addr + PORT_CLB, (uint32_t)cmd_list_phys);
    write_mem32(port_addr + PORT_CLBU, (uint32_t)(cmd_list_phys >> 32));

    write_mem32(port_addr + PORT_FB, (uint32_t)fis_buffer_phys);
    write_mem32(port_addr + PORT_FBU, (uint32_t)(fis_buffer_phys >> 32));


    // Get pointers to the (virtual) buffers for the chosen slot
    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)(cmd_list_buffer + (slot * sizeof(hba_cmd_header_t)));
    hba_cmd_tbl_t* cmd_table = (hba_cmd_tbl_t*)cmd_table_buffer; // Using the single static cmd table buffer
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmd_table->cfis;

    // Clear buffers (important!)

    // Clear command header for the specific slot
    uint8_t* hdr_ptr = (uint8_t*)cmd_header;
    for (int i = 0; i < sizeof(hba_cmd_header_t); i++) hdr_ptr[i] = 0;

    // Clear the command table buffer (CFIS + PRDT)
    uint8_t* tbl_ptr = (uint8_t*)cmd_table;
    // Only clear enough for CFIS and 1 PRDT entry for IDENTIFY
    for (int i = 0; i < CMD_TABLE_STATIC_SIZE + 1 * sizeof(hba_prdt_entry_t); i++) tbl_ptr[i] = 0;

    // Clear the data buffer before the read
    uint8_t* data_ptr = (uint8_t*)data_buffer;
    for (int i = 0; i < SECTOR_SIZE; i++) data_ptr[i] = 0;


    // Configure the command header (for the chosen slot)
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); // 20 bytes / 4 = 5 DWORDs
    cmd_header->a = 0;   // Not ATAPI
    cmd_header->w = 0;   // Write = 0 (Host to Device) for the *FIS*, direction for *DATA* is Read (Device to Host)
    cmd_header->p = 1;   // Prefetchable = 1 is okay if PRDT is used.
    cmd_header->prdtl = 1; // We are using 1 PRDT entry.
    // cmd_header->prdbc is volatile, cleared by HBA before command execution, updated on completion.
    cmd_header->ctba = cmd_table_phys; // Physical address of the command table


    // Configure the command table
    // Setup PRDT entry 0
    cmd_table->prdt[0].dba = identify_data_phys;       // Physical address of data buffer
    cmd_table->prdt[0].dbc = SECTOR_SIZE - 1; // Byte count (0-based). 511 for 512 bytes.
    cmd_table->prdt[0].i = 1; // Interrupt on completion (useful even if polling CI)


    // Setup the Command FIS (H2D Register FIS) within the command table
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // This is a command FIS
    cmdfis->command = ATA_CMD_IDENTIFY;
    cmdfis->device = 0; // Master device, LBA mode (bit 6=1). 0xE0 is standard for LBA master. 0 might also work. Let's stick with 0 for now.
    // DEBUG: Try cmdfis->device = 0xE0; if 0 fails.

    // LBA and Feature registers are not used for IDENTIFY DEVICE
    cmdfis->lba0 = 0; cmdfis->lba1 = 0; cmdfis->lba2 = 0;
    cmdfis->lba3 = 0; cmdfis->lba4 = 0; cmdfis->lba5 = 0;
    cmdfis->featurel = 0; cmdfis->featureh = 0;
    // Count should be 0 for IDENTIFY (implies 1 sector in some contexts, ignored here)
    cmdfis->countl = 0;
    cmdfis->counth = 0;
    cmdfis->control = 0; // Control register (usually 0)


    // --- Issue Command and Wait ---
    cout << "Issuing IDENTIFY command on slot " << slot << "...\n";
    int issue_status = issue_ahci_command(port_addr, slot);
    if (issue_status < 0) {
        return issue_status; // Propagate error
    }


    cout << "Command issued, waiting for completion...\n";
    int complete_status = wait_for_ahci_completion(port_addr, slot, cmd_header, SECTOR_SIZE);
    if (complete_status < 0) {
        return complete_status; // Propagate error
    }


    // --- Process Results ---
    cout << "\nIDENTIFY command completed successfully.\n";
    display_identify_data((uint16_t*)data_buffer); // Cast the byte buffer to word pointer


    return 0; // Success

}


// Function to read sectors from disk
// ahci_base: Base address of AHCI controller MMIO space
// port: Port number (0-31)
// lba: Starting Logical Block Address (64-bit)
// count: Number of sectors to read (max depends on MAX_TRANSFER_SECTORS)
// buffer: Pointer to a DMA-accessible buffer to store the data (must be large enough: count * SECTOR_SIZE)
// Returns 0 on success, negative on error
int read_sectors(uint64_t ahci_base, int port, uint64_t lba, uint16_t count, void* buffer) {
    uint64_t port_addr = ahci_base + 0x100 + (port * 0x80);
    uint8_t command = 0;
    bool use_lba48 = false;

    // Basic validation
    if (count == 0) return 0; // Nothing to do
    if (count > MAX_TRANSFER_SECTORS) {
        cout << "ERROR: Read count " << count << " exceeds maximum " << MAX_TRANSFER_SECTORS << "\n";
        return -10;
    }
    if (!buffer) {
        cout << "ERROR: Read buffer is null.\n";
        return -11;
    }
    // Check LBA range and select command
    if (lba + count > (1ULL << 28)) { // Check if LBA48 is required
        if (!lba48_available) {
            //cout << "ERROR: LBA address " << (unsigned long long)lba << " requires LBA48, but it's not supported by the device.\n";
            return -12;
        }
        command = ATA_CMD_READ_DMA_EXT;
        use_lba48 = true;
    }
    else {
        // Can use LBA28 or LBA48 if available
        command = lba48_available ? ATA_CMD_READ_DMA_EXT : ATA_CMD_READ_DMA;
        use_lba48 = lba48_available;
    }

    // Prepare port (checks presence, enables FRE/ST, clears errors)
    int prep_status = prepare_port_for_command(port_addr, port);
    if (prep_status < 0) {
        return prep_status; // Propagate error
    }

    // Find an empty command slot
    int slot = find_free_command_slot(port_addr);
    if (slot < 0) {
        cout << "ERROR: No free command slot found for read on port " << port << ".\n";
        return -5;
    }

    // --- Setup Command Structures ---
    uint64_t cmd_list_phys = (uint64_t)cmd_list_buffer;
    uint64_t fis_buffer_phys = (uint64_t)fis_buffer;
    uint64_t cmd_table_phys = (uint64_t)cmd_table_buffer;
    uint64_t data_buffer_phys = (uint64_t)buffer; // Use the provided buffer

    // Set base addresses (might be redundant if IDENTIFY was just called, but good practice)
    write_mem32(port_addr + PORT_CLB, (uint32_t)cmd_list_phys);
    write_mem32(port_addr + PORT_CLBU, (uint32_t)(cmd_list_phys >> 32));
    write_mem32(port_addr + PORT_FB, (uint32_t)fis_buffer_phys);
    write_mem32(port_addr + PORT_FBU, (uint32_t)(fis_buffer_phys >> 32));


    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)(cmd_list_buffer + (slot * sizeof(hba_cmd_header_t)));
    hba_cmd_tbl_t* cmd_table = (hba_cmd_tbl_t*)cmd_table_buffer;
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmd_table->cfis;

    // Clear command header and command table area
    uint8_t* hdr_ptr = (uint8_t*)cmd_header;
    for (int i = 0; i < sizeof(hba_cmd_header_t); i++) hdr_ptr[i] = 0;
    uint8_t* tbl_ptr = (uint8_t*)cmd_table;
    // Clear enough for CFIS and 1 PRDT entry (assuming single PRDT entry for simplicity)
    for (int i = 0; i < CMD_TABLE_STATIC_SIZE + 1 * sizeof(hba_prdt_entry_t); i++) tbl_ptr[i] = 0;

    // Configure command header
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); // 5 DWORDs
    cmd_header->w = 0;   // Direction of DATA transfer is READ (Device to Host)
    cmd_header->prdtl = 1; // Using 1 PRDT entry
    cmd_header->ctba = cmd_table_phys;

    // Configure PRDT entry
    uint32_t byte_count = count * SECTOR_SIZE;
    cmd_table->prdt[0].dba = data_buffer_phys;
    cmd_table->prdt[0].dbc = byte_count - 1; // 0-based count
    cmd_table->prdt[0].i = 1; // Interrupt on completion

    // Configure command FIS
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // Command
    cmdfis->command = command;

    cmdfis->lba0 = (uint8_t)(lba & 0xFF);
    cmdfis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    cmdfis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    if (use_lba48) {
        cmdfis->device = (1 << 6); // LBA mode
        cmdfis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
        cmdfis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
        cmdfis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
    }
    else {
        cmdfis->device = (1 << 6) | ((uint8_t)((lba >> 24) & 0x0F)); // LBA mode + LBA high nibble
        cmdfis->lba3 = 0;
        cmdfis->lba4 = 0;
        cmdfis->lba5 = 0;
    }

    cmdfis->countl = (uint8_t)(count & 0xFF);
    cmdfis->counth = (uint8_t)((count >> 8) & 0xFF);

    cmdfis->featurel = 0;
    cmdfis->featureh = 0;
    cmdfis->control = 0;

    // --- Issue Command and Wait ---
   // cout << "Issuing Read DMA command on slot " << slot << " (LBA=" << (unsigned long long)lba << ", Count=" << count << ")...\n";
    int issue_status = issue_ahci_command(port_addr, slot);
    if (issue_status < 0) {
        return issue_status;
    }

    int complete_status = wait_for_ahci_completion(port_addr, slot, cmd_header, byte_count);
    if (complete_status < 0) {
        return complete_status;
    }

    //cout << "Read command completed successfully.\n";
    return 0; // Success
}


// Function to write sectors to disk
// ahci_base: Base address of AHCI controller MMIO space
// port: Port number (0-31)
// lba: Starting Logical Block Address (64-bit)
// count: Number of sectors to write (max depends on MAX_TRANSFER_SECTORS)
// buffer: Pointer to a DMA-accessible buffer containing the data (must be count * SECTOR_SIZE bytes)
// Returns 0 on success, negative on error
int write_sectors(uint64_t ahci_base, int port, uint64_t lba, uint16_t count, void* buffer) {
    uint64_t port_addr = ahci_base + 0x100 + (port * 0x80);
    uint8_t command = 0;
    bool use_lba48 = false;

    // Basic validation
    if (count == 0) return 0; // Nothing to do
    if (count > MAX_TRANSFER_SECTORS) {
        cout << "ERROR: Write count " << count << " exceeds maximum " << MAX_TRANSFER_SECTORS << "\n";
        return -10;
    }
    if (!buffer) {
        cout << "ERROR: Write buffer is null.\n";
        return -11;
    }
    // Check LBA range and select command
    if (lba + count > (1ULL << 28)) { // Check if LBA48 is required
        if (!lba48_available) {
            //cout << "ERROR: LBA address " << (unsigned long long)lba << " requires LBA48, but it's not supported by the device.\n";
            return -12;
        }
        command = ATA_CMD_WRITE_DMA_EXT;
        use_lba48 = true;
    }
    else {
        // Can use LBA28 or LBA48 if available
        command = lba48_available ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_WRITE_DMA;
        use_lba48 = lba48_available;
    }

    // Prepare port
    int prep_status = prepare_port_for_command(port_addr, port);
    if (prep_status < 0) {
        return prep_status;
    }

    // Find an empty command slot
    int slot = find_free_command_slot(port_addr);
    if (slot < 0) {
        cout << "ERROR: No free command slot found for write on port " << port << ".\n";
        return -5;
    }

    // --- Setup Command Structures ---
    uint64_t cmd_list_phys = (uint64_t)cmd_list_buffer;
    uint64_t fis_buffer_phys = (uint64_t)fis_buffer;
    uint64_t cmd_table_phys = (uint64_t)cmd_table_buffer;
    uint64_t data_buffer_phys = (uint64_t)buffer;

    // Set base addresses
    write_mem32(port_addr + PORT_CLB, (uint32_t)cmd_list_phys);
    write_mem32(port_addr + PORT_CLBU, (uint32_t)(cmd_list_phys >> 32));
    write_mem32(port_addr + PORT_FB, (uint32_t)fis_buffer_phys);
    write_mem32(port_addr + PORT_FBU, (uint32_t)(fis_buffer_phys >> 32));

    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)(cmd_list_buffer + (slot * sizeof(hba_cmd_header_t)));
    hba_cmd_tbl_t* cmd_table = (hba_cmd_tbl_t*)cmd_table_buffer;
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmd_table->cfis;

    // Clear command header and command table area
    uint8_t* hdr_ptr = (uint8_t*)cmd_header;
    for (int i = 0; i < sizeof(hba_cmd_header_t); i++) hdr_ptr[i] = 0;
    uint8_t* tbl_ptr = (uint8_t*)cmd_table;
    for (int i = 0; i < CMD_TABLE_STATIC_SIZE + 1 * sizeof(hba_prdt_entry_t); i++) tbl_ptr[i] = 0;


    // Configure command header
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); // 5 DWORDs
    cmd_header->w = 1;   // Direction of DATA transfer is WRITE (Host to Device)
    cmd_header->prdtl = 1; // Using 1 PRDT entry
    cmd_header->ctba = cmd_table_phys;

    // Configure PRDT entry
    uint32_t byte_count = count * SECTOR_SIZE;
    cmd_table->prdt[0].dba = data_buffer_phys;
    cmd_table->prdt[0].dbc = byte_count - 1; // 0-based count
    cmd_table->prdt[0].i = 1; // Interrupt on completion

    // Configure command FIS
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // Command
    cmdfis->command = command;

    cmdfis->lba0 = (uint8_t)(lba & 0xFF);
    cmdfis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    cmdfis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    if (use_lba48) {
        cmdfis->device = (1 << 6); // LBA mode
        cmdfis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
        cmdfis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
        cmdfis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
    }
    else {
        cmdfis->device = (1 << 6) | ((uint8_t)((lba >> 24) & 0x0F)); // LBA mode + LBA high nibble
        cmdfis->lba3 = 0;
        cmdfis->lba4 = 0;
        cmdfis->lba5 = 0;
    }

    cmdfis->countl = (uint8_t)(count & 0xFF);
    cmdfis->counth = (uint8_t)((count >> 8) & 0xFF);

    cmdfis->featurel = 0;
    cmdfis->featureh = 0;
    cmdfis->control = 0;

    // --- Issue Command and Wait ---
    //cout << "Issuing Write DMA command on slot " << slot << " (LBA=" << (unsigned long long)lba << ", Count=" << count << ")...\n";
    int issue_status = issue_ahci_command(port_addr, slot);
    if (issue_status < 0) {
        return issue_status;
    }

    int complete_status = wait_for_ahci_completion(port_addr, slot, cmd_header, byte_count);
    if (complete_status < 0) {
        return complete_status;
    }

    // Optional: Issue FLUSH CACHE command after writing for data persistence
    // This is highly recommended if the device has a volatile write cache.
    // Implementation similar to read/write but with different command code and PRDTL=0.
    // Example: send_flush_cache_command(ahci_base, port);

    //cout << "Write command completed successfully.\n";
    return 0; // Success
}


// Helper function to calculate string length (like strlen)
// Assumes null-terminated string.
// WARNING: No buffer overflow check! Use with caution.
size_t simple_strlen(const char* str) {
    size_t len = 0;
    while (str && *str != '\0') {
        len++;
        str++;
    }
    return len;
}

// Helper function to copy string (like strcpy)
// WARNING: No buffer overflow check! dst must be large enough.
void simple_strcpy(char* dst, const char* src) {
    if (!dst || !src) return;
    while (*src != '\0') {
        *dst = *src;
        dst++;
        src++;
    }
    *dst = '\0'; // Null terminate destination
}

// Helper function to clear buffer (like memset 0)
void clear_buffer(void* buffer, size_t size) {
    uint8_t* ptr = (uint8_t*)buffer;
    for (size_t i = 0; i < size; ++i) {
        ptr[i] = 0;
    }
}


// Simple function to write a C-string to a specific sector.
// WARNING: Writes the string AND the null terminator. Overwrites the entire sector.
// WARNING: Writing to arbitrary sectors (especially low LBAs) is dangerous!
// Returns 0 on success, negative on error.
int write_string_to_sector(uint64_t ahci_base, int port, uint64_t lba, const char* str) {
    if (!str) {
        cout << "ERROR: Cannot write null string.\n";
        return -20;
    }
    size_t len = simple_strlen(str);
    if (len + 1 > SECTOR_SIZE) { // +1 for null terminator
        cout << "ERROR: String too long (" << (unsigned int)len << " chars) to fit in one sector (" << SECTOR_SIZE << " bytes).\n";
        return -21;
    }

    // Use the shared static data buffer for this simple case
    // Clear the buffer first to ensure only the string and null terminator are written
    clear_buffer(data_buffer, SECTOR_SIZE);
    simple_strcpy((char*)data_buffer, str); // Copy string including null term

    cout << "Writing string \"" << str << "\" to LBA " << (unsigned int)lba << "...\n";
    int result = write_sectors(ahci_base, port, lba, 1, data_buffer); // Write 1 sector

    // Clear the buffer after use (optional, for security/cleanup)
    clear_buffer(data_buffer, SECTOR_SIZE);

    return result;
}

// Simple function to read a C-string from a specific sector.
// Reads the entire sector, assumes it contains a null-terminated string.
// Copies the string (up to buffer_size-1 chars) into the provided buffer.
// Ensures the output buffer is always null-terminated.
// Returns 0 on success, negative on error reading sector, 1 if string was truncated.
int read_string_from_sector(uint64_t ahci_base, int port, uint64_t lba, char* out_buffer, size_t buffer_size) {
    if (!out_buffer || buffer_size == 0) {
        cout << "ERROR: Invalid output buffer for reading string.\n";
        return -22;
    }
    // Ensure buffer is initially empty / null terminated
    out_buffer[0] = '\0';

    // Use the shared static data buffer to read the sector
    clear_buffer(data_buffer, SECTOR_SIZE);
    cout << "Reading string from LBA " << (unsigned int)lba << "...\n";
    int result = read_sectors(ahci_base, port, lba, 1, data_buffer); // Read 1 sector
    if (result < 0) {
        //cout << "ERROR: Failed to read sector " << (unsigned long long)lba << ".\n";
        return result; // Propagate read error
    }

    // Find the null terminator within the sector data
    char* sector_string = (char*)data_buffer;
    size_t string_len = 0;
    bool found_null = false;
    for (string_len = 0; string_len < SECTOR_SIZE; ++string_len) {
        if (sector_string[string_len] == '\0') {
            found_null = true;
            break;
        }
    }

    if (!found_null) {
        //cout << "WARNING: No null terminator found in sector " << (unsigned long long)lba << ". Reading entire sector content.\n";
        string_len = SECTOR_SIZE; // Treat the whole sector as data (might not be a string)
    }

    // Copy the string (or data) to the output buffer, respecting its size limit
    size_t copy_len = string_len;
    int truncated = 0;
    if (copy_len >= buffer_size) {
        copy_len = buffer_size - 1; // Leave space for null terminator
        cout << "WARNING: String read from sector was truncated to fit buffer size " << (unsigned int)buffer_size << ".\n";
        truncated = 1;
    }

    for (size_t i = 0; i < copy_len; ++i) {
        out_buffer[i] = sector_string[i];
    }
    out_buffer[copy_len] = '\0'; // Ensure null termination

    // Clear the temporary buffer (optional)
    clear_buffer(data_buffer, SECTOR_SIZE);

    //cout << "String read: \"" << out_buffer << "\"\n";
    return truncated; // Return 0 on success, 1 if truncated
}


#endif // IDENTIFY_H