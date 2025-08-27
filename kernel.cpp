#include "terminal_hooks.h"
#include "terminal_io.h"
#include "iostream_wrapper.h"
#include "interrupts.h"
#include "hardware_specs.h"
#include "stdlib_hooks.h"
#include "pci.h"
#include "sata.h"
#include "test.h"
#include "test2.h"
#include "disk.h"
#include "dma_memory.h"
#include "identify.h"


// Fix macro redefinition warning
#undef MAX_COMMAND_LENGTH
#define MAX_COMMAND_LENGTH 256
#define SECTOR_SIZE 512
#define ENTRY_SIZE 32
#define ATTR_LONG_NAME 0x0F
#define ATTR_DIRECTORY 0x10
#define ATTR_VOLUME_ID 0x08
#define ATTR_ARCHIVE 0x20
#define DELETED_ENTRY 0xE5

// Forward declarations for inline functions
static inline void* simple_memcpy(void* dst, const void* src, size_t n);
static inline void* simple_memset(void* s, int c, size_t n);
static inline int simple_memcmp(const void* s1, const void* s2, size_t n);
static inline int stricmp(const char* s1, const char* s2);
// Add these forward declarations
uint64_t parse_hex_input();
size_t parse_decimal_input();
// --- COPY/PASTE TO REPLACE YOUR EXISTING CODE ---

// FIXED: Correct FAT32 Boot Parameter Block structure
typedef struct {
    uint8_t  jmp_boot[3];
    char     oem_name[8];          // CORRECTED: Must be 8 bytes
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint8_t  num_fats;
    uint16_t root_ent_cnt;
    uint16_t tot_sec16;
    uint8_t  media;
    uint16_t fat_sz16;
    uint16_t sec_per_trk;
    uint16_t num_heads;
    uint32_t hidd_sec;
    uint32_t tot_sec32;
    uint32_t fat_sz32;
    uint16_t ext_flags;
    uint16_t fs_ver;
    uint32_t root_clus;
    uint16_t fs_info;
    uint16_t bk_boot_sec;
    uint8_t  reserved[12];
    uint8_t  drv_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t vol_id;
    char     vol_lab[11];
    char     fil_sys_type[8];      // CORRECTED: Must be 8 bytes
} __attribute__((packed)) fat32_bpb_t;



// FAT32 directory entry structure
typedef struct {
    char name[11];
    uint8_t attr;
    uint8_t ntres;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} __attribute__((packed)) fat_dir_entry_t;

// Global FAT32 variables
static fat32_bpb_t fat32_bpb;
static uint32_t fat_start_sector = 0;
static uint32_t data_start_sector = 0;
static uint32_t current_directory_cluster = 2;

// FAT32 cluster management additions
static uint32_t next_free_cluster = 3; // Start searching from cluster 3
static const uint32_t FAT_FREE_CLUSTER = 0x00000000;
static const uint32_t FAT_END_OF_CHAIN = 0x0FFFFFFF;
static const uint32_t FAT_BAD_CLUSTER = 0x0FFFFFF7;


// --- NEW, ROBUST FAT32 FORMATTING FUNCTION ---

bool fat32_format(uint64_t ahci_base, int port, uint32_t total_sectors, uint8_t sectors_per_cluster) {
    uint8_t sector[SECTOR_SIZE];
    simple_memset(sector, 0, SECTOR_SIZE);

    // --- 1. Validate Parameters ---
    if (total_sectors < 65536) {
        cout << "Error: Disk too small. Minimum 65536 sectors required for FAT32.\n";
        return false;
    }
    if ((sectors_per_cluster & (sectors_per_cluster - 1)) != 0 || sectors_per_cluster == 0) {
        cout << "Error: Sectors per cluster must be a power of 2.\n";
        return false;
    }

    // --- 2. Calculate Geometry ---
    uint32_t reserved_sectors = 32;
    uint32_t fat_size;
    uint32_t clusters;

    // A standard, direct formula to calculate FAT size correctly
    uint32_t numerator = total_sectors - reserved_sectors;
    uint32_t denominator = sectors_per_cluster + (512 / SECTOR_SIZE); // (sectors_per_cluster + 2*4/512) -> simplified
    clusters = numerator / denominator;
    
    // Check if cluster count is sufficient for FAT32
    if (clusters < 65525) {
        cout << "Error: Not enough clusters for FAT32.\n";
        cout << "  Calculated Clusters: " << clusters << " (Need >= 65525)\n";
        cout << "  Suggestion: Use a larger disk or smaller cluster size.\n";
        return false;
    }

    fat_size = (clusters * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;

    // --- 3. Create BPB ---
    fat32_bpb_t bpb = {};
    bpb.jmp_boot[0] = 0xEB; bpb.jmp_boot[1] = 0x58; bpb.jmp_boot[2] = 0x90;
    simple_memcpy(bpb.oem_name, "MSDOS5.0", 8);
    bpb.bytes_per_sec = SECTOR_SIZE;
    bpb.sec_per_clus = sectors_per_cluster;
    bpb.rsvd_sec_cnt = reserved_sectors;
    bpb.num_fats = 2;
    bpb.root_ent_cnt = 0;
    bpb.tot_sec16 = 0;
    bpb.media = 0xF8;
    bpb.fat_sz16 = 0;
    bpb.sec_per_trk = 63;
    bpb.num_heads = 255;
    bpb.hidd_sec = 0;
    bpb.tot_sec32 = total_sectors;
    bpb.fat_sz32 = fat_size;
    bpb.ext_flags = 0;
    bpb.fs_ver = 0;
    bpb.root_clus = 2;
    bpb.fs_info = 1;
    bpb.bk_boot_sec = 6;
    bpb.drv_num = 0x80;
    bpb.boot_sig = 0x29;
    bpb.vol_id = 0x12345678; // Example volume ID
    simple_memcpy(bpb.vol_lab, "NO NAME    ", 11);
    simple_memcpy(bpb.fil_sys_type, "FAT32   ", 8);

    // --- 4. Write Boot Sectors ---
    simple_memcpy(sector, &bpb, sizeof(bpb));
    sector[510] = 0x55;
    sector[511] = 0xAA; // FIXED: Correct boot signature

    cout << "Writing boot sector...\n";
    if (write_sectors(ahci_base, port, 0, 1, sector) != 0) return false;
    if (write_sectors(ahci_base, port, 6, 1, sector) != 0) return false; // Backup boot sector

    // --- 5. Write FSInfo Sector ---
    simple_memset(sector, 0, SECTOR_SIZE);
    *(uint32_t*)(sector + 0)   = 0x41615252; // FSI_LeadSig
    *(uint32_t*)(sector + 484) = 0x61417272; // FSI_StrucSig
    *(uint32_t*)(sector + 488) = clusters - 1; // FSI_Free_Count
    *(uint32_t*)(sector + 492) = 3;            // FSI_Nxt_Free
    sector[510] = 0x55;
    sector[511] = 0xAA;

    cout << "Writing FSInfo sector...\n";
    if (write_sectors(ahci_base, port, 1, 1, sector) != 0) return false;

    // --- 6. Initialize FATs ---
    cout << "Initializing FAT tables...\n";
    simple_memset(sector, 0, SECTOR_SIZE);
    *(uint32_t*)(sector + 0) = 0x0FFFFFF8; // Media descriptor & reserved
    *(uint32_t*)(sector + 4) = 0x0FFFFFFF; // Reserved
    *(uint32_t*)(sector + 8) = 0x0FFFFFFF; // EOC for root directory cluster

    uint32_t fat_start = reserved_sectors;
    for (int i = 0; i < bpb.num_fats; ++i) {
        if (write_sectors(ahci_base, port, fat_start, 1, sector) != 0) return false;
        fat_start += fat_size;
    }
    
    // Clear remaining FAT sectors
    simple_memset(sector, 0, SECTOR_SIZE);
    fat_start = reserved_sectors;
    for (int i = 0; i < bpb.num_fats; ++i) {
        for (uint32_t j = 1; j < fat_size; ++j) {
            if (write_sectors(ahci_base, port, fat_start + j, 1, sector) != 0) return false;
        }
        fat_start += fat_size;
    }

    // --- 7. Initialize Root Directory ---
    cout << "Initializing root directory...\n";
    uint32_t data_start = reserved_sectors + (bpb.num_fats * fat_size);
    uint64_t root_lba = data_start + ((bpb.root_clus - 2) * sectors_per_cluster);
    simple_memset(sector, 0, SECTOR_SIZE);
    for (uint8_t i = 0; i < sectors_per_cluster; ++i) {
        if (write_sectors(ahci_base, port, root_lba + i, 1, sector) != 0) return false;
    }

    cout << "Format completed successfully!\n";
    return true;
}


// --- NEW, INTELLIGENT FORMAT COMMAND ---

void cmd_formatfs(uint64_t ahci_base, int port) {
    cout << "=== FAT32 Format Utility ===\n";

    // Use a larger, more realistic disk size for testing.
    // Your previous 65536 sectors (32MB) is too small for FAT32 with large clusters.
    uint32_t total_sectors = 2097152; // 1GB
    
    cout << "Disk size is set to " << total_sectors << " sectors (" 
         << ((uint32_t)total_sectors * 512) / (1024 * 1024) << " MB).\n";

    // Automatically select a valid cluster size
    uint8_t sec_per_clus;
    if (total_sectors >= 33554432) { sec_per_clus = 64; }       // >= 16GB
    else if (total_sectors >= 16777216) { sec_per_clus = 32; }  // >= 8GB
    else if (total_sectors >= 524288) { sec_per_clus = 16; }    // >= 256MB
    else { sec_per_clus = 8; }                                  // < 256MB

    cout << "A cluster size of " << (int)sec_per_clus << " sectors (" 
         << (sec_per_clus * 512) / 1024 << " KB) has been selected.\n";
    cout << "WARNING: This will erase all data on the disk!\n";
    cout << "Continue with format? (y/N): ";
    
    char confirm[10];
    cin >> confirm;
    if (confirm[0] != 'y' && confirm[0] != 'Y') {
        cout << "Format cancelled.\n";
        return;
    }
    
    cout << "\nStarting format...\n";
    if (fat32_format(ahci_base, port, total_sectors, sec_per_clus)) {
        cout << "\n=== Format Successful! ===\n";
        cout << "Use 'mount' to mount the new filesystem.\n";
    } else {
        cout << "\n=== Format Failed! ===\n";
        cout << "Check console for errors. The disk may be too small or parameters invalid.\n";
    }
}

// --- END OF REPLACEMENT CODE ---



// Function to get actual disk size
uint32_t get_disk_sector_count(uint64_t ahci_base, int port) {
    // This should use your AHCI identify command to get actual disk size
    // For now, using a reasonable default, but you should implement proper detection
    uint32_t sectors = 2097152; // 1GB default (2M sectors * 512 bytes)
    
    cout << "Detected disk size: " << sectors << " sectors (" 
         << (sectors * 512) / (1024 * 1024) << " MB)\n";
    
    return sectors;
}


static inline void* simple_memcpy(void* dst, const void* src, size_t n);
static inline void* simple_memset(void* s, int c, size_t n);
// FAT32 Boot Parameter Block structure





static inline void* simple_memcpy(void* dst, const void* src, size_t n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline void* simple_memset(void* s, int c, size_t n) {
    char* p = (char*)s;
    for (size_t i = 0; i < n; i++) p[i] = (char)c;
    return s;
}

static inline int simple_memcmp(const void* s1, const void* s2, size_t n) {
    const char* p1 = (const char*)s1;
    const char* p2 = (const char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

static inline int stricmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return *s1 - *s2;
}

// Convert cluster to LBA
static inline uint64_t cluster_to_lba(uint32_t cluster) {
    if (cluster < 2) return 0;
    return data_start_sector + ((cluster - 2) * fat32_bpb.sec_per_clus);
}

// Convert filename to 8.3 format
static void to_83_format(const char *filename, char *out) {
    simple_memset(out, ' ', 11);
    uint8_t i = 0, j = 0;
    while (filename[i] && filename[i] != '.' && j < 8) {
        char c = filename[i++];
        out[j++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    if (filename[i] == '.') i++;
    j = 8;
    while (filename[i] && j < 11) {
        char c = filename[i++];
        out[j++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
}

// Extract filename from 8.3 format to readable string
void from_83_format(const char* fat_name, char* out) {
    int i, j = 0;
    
    // Copy name part, remove trailing spaces
    for (i = 0; i < 8 && fat_name[i] != ' '; i++) {
        out[j++] = fat_name[i];
    }
    
    // Add extension if present
    if (fat_name[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && fat_name[i] != ' '; i++) {
            out[j++] = fat_name[i];
        }
    }
    
    out[j] = '\0';
}

// Initialize FAT32 filesystem
bool fat32_init(uint64_t ahci_base, int port) {
    uint8_t buffer[SECTOR_SIZE];
    if (read_sectors(ahci_base, port, 0, 1, buffer) != 0) return false;
    
    simple_memcpy(&fat32_bpb, buffer, sizeof(fat32_bpb_t));
    if (simple_memcmp(fat32_bpb.fil_sys_type, "FAT32   ", 8) != 0) return false;
    
    fat_start_sector = fat32_bpb.rsvd_sec_cnt;
    data_start_sector = fat_start_sector + (fat32_bpb.num_fats * fat32_bpb.fat_sz32);
    current_directory_cluster = fat32_bpb.root_clus;
    return true;
}


// Global variables declarations
char buffer[4096];
size_t buffer_size = sizeof(buffer);
uint64_t ahci_base;
DMAManager dma_manager;

// Stub functions for missing test programs
void test_program_1() { cout << "Test program 1 executed\n"; }
void test_program_2() { cout << "Test program 2 executed\n"; }

// Command implementations
void cmd_help() {
    cout << "KERNEL COMMAND REFERENCE\n";
    cout << "SYSTEM INFORMATION:\n";
    cout << "  help                     show this help message\n";
    cout << "  clear                    clear the screen\n";
    cout << "  cpu                      display CPU information\n";
    cout << "  memory                   display memory configuration\n";
    cout << "  cache                    display cache information\n";
    cout << "  topology                 display CPU topology\n";
    cout << "  features                 display CPU features\n";
    cout << "  pstates                  display P-States information\n";
    cout << "  full                     display all hardware information\n";
    cout << "  pciscan                  scan PCI devices\n";
    cout << "  dma                      interactive DMA menu\n";
    cout << "  dmadump                  quick memory dump\n";
    cout << "  fshelp                   filesystem help\n";
 }

// Helper function to parse hex input
uint64_t parse_hex_input() {
    char hex_str[20];
    cin >> hex_str;
    
    uint64_t result = 0;
    for (int i = 0; hex_str[i] != '\0'; i++) {
        char c = hex_str[i];
        result = result << 4;
        
        if (c >= '0' && c <= '9') {
            result |= (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            result |= (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            result |= (c - 'A' + 10);
        }
    }
    return result;
}

// Helper function to parse decimal input
size_t parse_decimal_input() {
    char dec_str[20];
    cin >> dec_str;
    
    size_t result = 0;
    for (int i = 0; dec_str[i] != '\0'; i++) {
        if (dec_str[i] >= '0' && dec_str[i] <= '9') {
            result = result * 10 + (dec_str[i] - '0');
        }
    }
    return result;
}

// Complete DMA test function
void cmd_dma_test() {
    cout << "=== DMA Memory Editor ===\n";
    cout << "1. Read Memory Block\n";
    cout << "2. Write Memory Block\n";
    cout << "3. Memory Dump\n";
    cout << "4. Pattern Fill\n";
    cout << "5. Memory Copy\n";
    cout << "6. DMA Channel Status\n";
    cout << "7. Performance Test\n";
    cout << "Enter choice: ";
    
    char choice[10];
    cin >> choice;
    
    switch(int(choice)) {
        case '1': {
            cout << "=== DMA Read Memory Block ===\n";
            cout << "Enter source address (hex): 0x";
            uint64_t addr = parse_hex_input();
            
            cout << "Enter size (bytes): ";
            size_t size = parse_decimal_input();
            
            if (size > 4096) {
                cout << "Size too large (max 4096 bytes)\n";
                break;
            }
            
            void* buffer = dma_manager.allocate_dma_buffer(size);
            if (!buffer) {
                cout << "Failed to allocate DMA buffer\n";
                break;
            }
            
            cout << "Starting DMA read from 0x";
            // Print hex address manually
            char hex_output[17];
            uint64_t temp_addr = addr;
            int pos = 15;
            hex_output[16] = '\0';
            
            do {
                int digit = temp_addr & 0xF;
                hex_output[pos--] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
                temp_addr >>= 4;
            } while (temp_addr > 0 && pos >= 0);
            
            while (pos >= 0) {
                hex_output[pos--] = '0';
            }
            cout << hex_output << "...\n";
            
            if (dma_manager.read_memory_dma(addr, buffer, size)) {
                cout << "DMA read successful!\n";
                cout << "Data contents (first 64 bytes):\n";
                
                uint8_t* data = (uint8_t*)buffer;
                size_t display_size = (size > 64) ? 64 : size;
                
                for (size_t i = 0; i < display_size; i += 16) {
                    cout << "  ";
                    for (size_t j = 0; j < 16 && (i + j) < display_size; j++) {
                        uint8_t byte = data[i + j];
                        char hex_byte[3];


                        hex_byte[0] = ((byte >> 4) < 10) ? ('0' + (byte >> 4)) : ('A' + (byte >> 4) - 10);
                        hex_byte[1] = ((byte & 0xF) < 10) ? ('0' + (byte & 0xF)) : ('A' + (byte & 0xF) - 10);
                        hex_byte[2] = '\0';
                        cout << hex_byte << " ";
                    }
                    cout << "\n";
                }
            } else {
                cout << "DMA read failed\n";
            }
            
            dma_manager.free_dma_buffer(buffer);
            break;
        }
        
        case '2': {
            cout << "=== DMA Write Memory Block ===\n";
            cout << "Enter destination address (hex): 0x";
            uint64_t addr = parse_hex_input();
            
            cout << "Enter data pattern (hex byte): 0x";
            uint64_t pattern = parse_hex_input();
            uint8_t byte_pattern = (uint8_t)(pattern & 0xFF);
            
            cout << "Enter size (bytes): ";
            size_t size = parse_decimal_input();
            
            if (size > 4096) {
                cout << "Size too large (max 4096 bytes)\n";
                break;
            }
            
            void* buffer = dma_manager.allocate_dma_buffer(size);
            if (!buffer) {
                cout << "Failed to allocate DMA buffer\n";
                break;
            }
            
            // Fill buffer with pattern
            uint8_t* data = (uint8_t*)buffer;
            for (size_t i = 0; i < size; i++) {
                data[i] = byte_pattern;
            }
            
            cout << "Writing pattern 0x";
            char hex_byte[3];
            hex_byte[0] = (byte_pattern >> 4) < 10 ? ('0' + (byte_pattern >> 4)) : ('A' + (byte_pattern >> 4) - 10);
            hex_byte[1] = (byte_pattern & 0xF) < 10 ? ('0' + (byte_pattern & 0xF)) : ('A' + (byte_pattern & 0xF) - 10);
            hex_byte[2] = '\0';
            cout << hex_byte << " to memory...\n";
            
            if (dma_manager.write_memory_dma(addr, buffer, size)) {
                cout << "DMA write successful!\n";
            } else {
                cout << "DMA write failed\n";
            }
            
            dma_manager.free_dma_buffer(buffer);
            break;
        }
        
        case '3': {
            cout << "=== DMA Memory Dump ===\n";
            cout << "Enter start address (hex): 0x";
            uint64_t addr = parse_hex_input();
            
            cout << "Enter dump size (bytes): ";
            size_t size = parse_decimal_input();
            
            if (size > 2048) {
                cout << "Size too large for display (max 2048 bytes)\n";
                break;
            }
            
            dma_manager.dump_memory_region(addr, size);
            break;
        }
        
        case '4': {
            cout << "=== DMA Pattern Fill ===\n";
            cout << "Enter destination address (hex): 0x";
            uint64_t addr = parse_hex_input();
            
            cout << "Enter pattern (hex byte): 0x";
            uint64_t pattern = parse_hex_input();
            uint8_t byte_pattern = (uint8_t)(pattern & 0xFF);
            
            cout << "Enter size (bytes): ";
            size_t size = parse_decimal_input();
            
            if (dma_manager.pattern_fill(addr, byte_pattern, size)) {
                cout << "Pattern fill completed successfully\n";
            } else {
                cout << "Pattern fill failed\n";
            }
            break;
        }
        
        case '5': {
            cout << "=== DMA Memory Copy ===\n";
            cout << "Enter source address (hex): 0x";
            uint64_t src_addr = parse_hex_input();
            
            cout << "Enter destination address (hex): 0x";
            uint64_t dst_addr = parse_hex_input();
            
            cout << "Enter size (bytes): ";
            size_t size = parse_decimal_input();
            
            cout << "Copying " << (int)size << " bytes via DMA...\n";
            
            if (dma_manager.memory_copy(src_addr, dst_addr, size)) {
                cout << "Memory copy completed successfully\n";
            } else {
                cout << "Memory copy failed\n";
            }
            break;
        }
        
        case '6': {
            cout << "=== DMA Channel Status ===\n";
            dma_manager.show_channel_status();
            break;
        }
        
        case '7': {
            cout << "=== DMA Performance Test ===\n";
            cout << "Running DMA performance benchmark...\n";
            
            const size_t test_size = 1024;
            uint64_t test_addr = 0x100000;  // 1MB mark
            
            void* src_buffer = dma_manager.allocate_dma_buffer(test_size);
            void* dst_buffer = dma_manager.allocate_dma_buffer(test_size);
            
            if (src_buffer && dst_buffer) {
                // Fill source with test data
                uint8_t* src_data = (uint8_t*)src_buffer;
                for (size_t i = 0; i < test_size; i++) {
                    src_data[i] = (uint8_t)(i & 0xFF);
                }
                
                cout << "Testing DMA read performance...\n";
                if (dma_manager.read_memory_dma(test_addr, dst_buffer, test_size)) {
                    cout << "Read test completed\n";
                }
                
                cout << "Testing DMA write performance...\n";
                if (dma_manager.write_memory_dma(test_addr, src_buffer, test_size)) {
                    cout << "Write test completed\n";
                }
                
                cout << "Testing memory-to-memory copy...\n";
                if (dma_manager.memory_copy(test_addr, test_addr + test_size, test_size)) {
                    cout << "Copy test completed\n";
                }
                
                cout << "Performance test completed successfully\n";
            } else {
                cout << "Failed to allocate test buffers\n";
            }
            
            if (src_buffer) dma_manager.free_dma_buffer(src_buffer);
            if (dst_buffer) dma_manager.free_dma_buffer(dst_buffer);
            break;
        }
        
        default:
            cout << "Invalid choice\n";
            break;
    }
}

// Command processing function
void command_prompt() {
    char input[MAX_COMMAND_LENGTH + 1];
    ahci_base = disk_init();

    int port = 0;
    bool fat32_initialized = false;
    
    cout << "Kernel Command Prompt Ready\n";
    cout << "Type 'help' for available commands\n\n";
    
    while (true) {
        cout << "> ";

        // Safely read input and null-terminate
        cin >> input;
        input[MAX_COMMAND_LENGTH] = '\0';

        // Parse command and arguments
        char* space = strchr(input, ' ');
        size_t cmd_len = space ? space - input : simple_strlen(input);
        char* args = space ? space + 1 : nullptr;
        
        // Create null-terminated command string
        char cmd_str[MAX_COMMAND_LENGTH + 1];
        simple_memcpy(cmd_str, input, cmd_len);
        cmd_str[cmd_len] = '\0';
        
        // SYSTEM INFORMATION COMMANDS
        if (stricmp(cmd_str, "help") == 0) {
            cmd_help();
        } else if (stricmp(cmd_str, "clear") == 0) {
            clear_screen();
        }else if (stricmp(cmd_str, "format") == 0) {
            cmd_formatfs(ahci_base, 0);
        } else if (stricmp(cmd_str, "cpu") == 0) {
            cmd_cpu();
        } else if (stricmp(cmd_str, "memory") == 0) {
            cmd_memory();
        } else if (stricmp(cmd_str, "cache") == 0) {
            cmd_cache();
        } else if (stricmp(cmd_str, "topology") == 0) {
            cmd_topology();
        } else if (stricmp(cmd_str, "features") == 0) {
            cmd_features();
        } else if (stricmp(cmd_str, "pstates") == 0) {
            cmd_pstates();
        } else if (stricmp(cmd_str, "full") == 0) {
            cmd_full();
        } else if (stricmp(cmd_str, "pciscan") == 0) {
            cout << "PCI scan not implemented yet\n";
        } else if (stricmp(cmd_str, "dma") == 0) {
            cmd_dma_test();
        } else if (stricmp(cmd_str, "dmadump") == 0) {
            cout << "Enter address: 0x";
            uint64_t addr = parse_hex_input();
            dma_manager.dump_memory_region(addr, 256);
            
        // TEST PROGRAMS
        } else if (stricmp(cmd_str, "program1") == 0) {
            test_program_1();
        } else if (stricmp(cmd_str, "program2") == 0) {
            test_program_2();
        } else if (stricmp(cmd_str, "mount") == 0) {

            fat32_initialized = true;
            cout << "FAT32 filesystem mounted successfully.\n";

        } else if (stricmp(cmd_str, "unmount") == 0) {
            fat32_initialized = false;
            cout << "FAT32 filesystem unmounted.\n";
        } else {
            cout << "Unknown command: " << input << "\n";
            cout << "Type 'help' for a list of commands.\n";

        }
    }
}

// Update kernel_main() to initialize new systems:
extern "C" void kernel_main() {
    terminal_initialize();
    init_terminal_io();
    init_keyboard();
    
    cout << "Hello, kernel World!" << '\n';
    
    // Initialize DMA system
    uint64_t dma_base = 0xFED00000; // Example DMA controller base address
    if (dma_manager.initialize(dma_base)) {
        cout << "DMA Manager initialized successfully\n";
    }
    
    cout << "Kernel initialized successfully!\n";
    cout << "FAT32 Filesystem Support Ready\n";
    cout << "\nBoot complete. Starting command prompt...\n";
    cout << "Use 'mount' to initialize FAT32 filesystem\n";
    
    command_prompt();
}

