#ifndef SATA_H
#define SATA_H

#include "types.h"
#include "pci.h"

/**
 * SATA Controller Module
 * For bare metal AMD64 environment
 */

// AHCI registers offsets
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

// SATA device signatures
#define SATA_SIG_ATA    0x00000101  // SATA drive
#define SATA_SIG_ATAPI  0xEB140101  // ATAPI device
#define SATA_SIG_SEMB   0xC33C0101  // Enclosure management bridge
#define SATA_SIG_PM     0x96690101  // Port multiplier

// Port SSTS (SATA Status) bits
#define SSTS_DET_MASK   0x0000000F  // Device Detection
#define SSTS_DET_NONE   0x00000000  // No device detected, PHY offline
#define SSTS_DET_PRESENT 0x00000001 // Device present but no communication
#define SSTS_DET_ESTABLISHED 0x00000003 // Device present and communication established
#define SSTS_DET_OFFLINE 0x00000004 // PHY offline, in BIST or loopback mode

#define SSTS_IPM_MASK   0x00000F00  // Interface Power Management
#define SSTS_IPM_NONE   0x00000000  // Not present, disabled
#define SSTS_IPM_ACTIVE 0x00000100  // Active state
#define SSTS_IPM_PARTIAL 0x00000200 // Partial power management state
#define SSTS_IPM_SLUMBER 0x00000600 // Slumber power management state
#define SSTS_IPM_DEVSLEEP 0x00000800 // DevSleep power management state

// Port TFD (Task File Data) bits
#define TFD_STS_ERR     0x00000001  // Error
#define TFD_STS_IDX     0x00000002  // Index
#define TFD_STS_CORR    0x00000004  // Corrected data
#define TFD_STS_DRQ     0x00000008  // Data transfer requested
#define TFD_STS_DSC     0x00000010  // Device ready to transfer
#define TFD_STS_DF      0x00000020  // Device fault
#define TFD_STS_DRDY    0x00000040  // Device ready
#define TFD_STS_BSY     0x00000080  // Device busy

// Port CMD (Command and Status) bits
#define PORT_CMD_ST     0x00000001  // Start
#define PORT_CMD_SUD    0x00000002  // Spin-Up Device
#define PORT_CMD_POD    0x00000004  // Power On Device
#define PORT_CMD_CLO    0x00000008  // Command List Override
#define PORT_CMD_FRE    0x00000010  // FIS Receive Enable
#define PORT_CMD_MPSS   0x00000020  // Mechanical Presence Switch State
#define PORT_CMD_FR     0x00004000  // FIS Receive Running
#define PORT_CMD_CR     0x00008000  // Command List Running

// Error register bits (in TFD high byte)
#define ERR_AMNF        0x00000001  // Address mark not found
#define ERR_TK0NF       0x00000002  // Track 0 not found
#define ERR_ABRT        0x00000004  // Command aborted
#define ERR_MCR         0x00000008  // Media change request
#define ERR_IDNF        0x00000010  // ID not found
#define ERR_MC          0x00000020  // Media changed
#define ERR_UNC         0x00000040  // Uncorrectable data
#define ERR_ICRC        0x00000080  // Interface CRC error

// Function prototypes

/**
 * Simple memory access to read 32-bit value
 * @param addr Physical memory address to read from
 * @return 32-bit value at the specified address
 */
inline uint32_t read_mem32(uint64_t addr);

/**
 * Print a register value in hexadecimal format with a label
 * @param label Text label to print before the value
 * @param value 32-bit value to print in hex
 */
void print_hex(const char* label, uint32_t value);

/**
 * Decode and print port status register information
 * @param ssts Value of the SSTS (SATA Status) register
 */
void decode_port_status(uint32_t ssts);

/**
 * Decode and print task file data register information
 * @param tfd Value of the TFD (Task File Data) register
 */
void decode_task_file(uint32_t tfd);

/**
 * Decode and print port command register information
 * @param cmd Value of the CMD (Command and Status) register
 */
void decode_port_cmd(uint32_t cmd);

/**
 * Debug function to fully examine SATA controller state
 * This function scans for AHCI controllers via PCI, then
 * inspects all implemented ports and their status
 */
void fs();

/**
 * Initialize the SATA controller
 * @return 1 if successful, 0 if failed or no controller found
 */
int init_sata_controller();

/**
 * Get the base address of the AHCI controller
 * @return Physical base address of the AHCI controller's registers or 0 if not found
 */
uint64_t get_ahci_base();

/**
 * Reset a SATA port
 * @param port_num Port number to reset
 * @return 1 if reset successful, 0 if failed
 */
int reset_sata_port(uint8_t port_num);

/**
 * Check if a device is present on the specified port
 * @param port_num Port number to check
 * @return 1 if device is present and communication established, 0 otherwise
 */
int is_device_present(uint8_t port_num);

/**
 * Get the type of device connected to a port
 * @param port_num Port number to check
 * @return Device type based on signature (1=ATA, 2=ATAPI, 3=SEMB, 4=PM, 0=unknown/none)
 */
int get_device_type(uint8_t port_num);

#endif // SATA_H