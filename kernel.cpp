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

static inline void* simple_memcpy(void* dst, const void* src, size_t n);
static inline void* simple_memset(void* s, int c, size_t n);
// FAT32 Boot Parameter Block structure
typedef struct {
    uint8_t jmp_boot[3];
    char oem_name[2];
    uint16_t bytes_per_sec;
    uint8_t sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint8_t num_fats;
    uint16_t root_ent_cnt;
    uint16_t tot_sec16;
    uint8_t media;
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
    uint8_t reserved[12];
    uint8_t drv_num;
    uint8_t reserved1;
    uint8_t boot_sig;
    uint32_t vol_id;
    char vol_lab[11];
    char fil_sys_type[2];
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


// Format disk with FAT32 filesystem
bool fat32_format(uint64_t ahci_base, int port, uint32_t total_sectors, uint8_t sectors_per_cluster) {
    uint8_t sector[SECTOR_SIZE];
    simple_memset(sector, 0, SECTOR_SIZE);

    // Calculate FAT size (simplified calculation)
    uint32_t data_sectors = total_sectors - 32; // 32 reserved sectors
    uint32_t clusters = data_sectors / sectors_per_cluster;
    uint32_t fat_size = (clusters * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE; // 4 bytes per FAT32 entry

    // Create FAT32 Boot Parameter Block
    fat32_bpb_t bpb = {};
    
    // Boot sector signature and jump instruction
    bpb.jmp_boot[0] = 0xEB;
    bpb.jmp_boot[1] = 0x58;
    bpb.jmp_boot[2] = 0x90;
    
    // OEM Name
    simple_memcpy(bpb.oem_name, "MSDOS5.0", 8);
    
    // BPB fields
    bpb.bytes_per_sec = SECTOR_SIZE;
    bpb.sec_per_clus = sectors_per_cluster;
    bpb.rsvd_sec_cnt = 32;
    bpb.num_fats = 2;
    bpb.root_ent_cnt = 0; // FAT32 has no fixed root directory
    bpb.tot_sec16 = 0; // Use 32-bit field instead
    bpb.media = 0xF8; // Fixed disk
    bpb.fat_sz16 = 0; // Use 32-bit field instead
    bpb.sec_per_trk = 63;
    bpb.num_heads = 255;
    bpb.hidd_sec = 0;
    bpb.tot_sec32 = total_sectors;
    
    // FAT32 specific fields
    bpb.fat_sz32 = fat_size;
    bpb.ext_flags = 0;
    bpb.fs_ver = 0;
    bpb.root_clus = 2; // Root directory starts at cluster 2
    bpb.fs_info = 1;
    bpb.bk_boot_sec = 6;
    
    // Extended boot signature
    bpb.drv_num = 0x80;
    bpb.reserved1 = 0;
    bpb.boot_sig = 0x29;
    bpb.vol_id = 0x12345678;
    
    // Volume label and filesystem type
    simple_memcpy(bpb.vol_lab, "NO NAME    ", 11);
    simple_memcpy(bpb.fil_sys_type, "FAT32   ", 8);
    
    // Copy BPB to sector buffer
    simple_memcpy(sector, &bpb, sizeof(bpb));
    
    // Add boot signature
    

    sector[510] = 0x55;
    //sector = 0xAA;
    
    // Write boot sector
    cout << "Writing boot sector...\n";
    if (write_sectors(ahci_base, port, 0, 1, sector) != 0) {
        cout << "Failed to write boot sector\n";
        return false;
    }
    
    // Write backup boot sector
    if (write_sectors(ahci_base, port, 6, 1, sector) != 0) {
        cout << "Failed to write backup boot sector\n";
        return false;
    }
    
    // Initialize FAT tables
    cout << "Initializing FAT tables...\n";
    simple_memset(sector, 0, SECTOR_SIZE);
    
    // First FAT entry: media descriptor + end of chain
    uint32_t* fat_entries = (uint32_t*)sector;
    fat_entries[0] = 0x0FFFFFF8; // Media descriptor
    fat_entries[1] = 0x0FFFFFFF; // End of chain
    fat_entries[2] = 0x0FFFFFFF; // Root directory end of chain
    
    // Write first sector of each FAT
    for (int fat_num = 0; fat_num < bpb.num_fats; fat_num++) {
        uint32_t fat_start = bpb.rsvd_sec_cnt + (fat_num * bpb.fat_sz32);
        if (write_sectors(ahci_base, port, fat_start, 1, sector) != 0) {
            cout << "Failed to write FAT " << fat_num << "\n";
            return false;
        }
        
        // Clear remaining FAT sectors
        simple_memset(sector, 0, SECTOR_SIZE);
        for (uint32_t i = 1; i < bpb.fat_sz32; i++) {
            if (write_sectors(ahci_base, port, fat_start + i, 1, sector) != 0) {
                cout << "Failed to clear FAT sector\n";
                return false;
            }
        }
    }
    
    // Initialize root directory cluster
    cout << "Initializing root directory...\n";
    uint32_t data_start = bpb.rsvd_sec_cnt + (bpb.num_fats * bpb.fat_sz32);
    simple_memset(sector, 0, SECTOR_SIZE);
    
    for (uint8_t i = 0; i < bpb.sec_per_clus; i++) {
        if (write_sectors(ahci_base, port, data_start + i, 1, sector) != 0) {
            cout << "Failed to initialize root directory\n";
            return false;
        }
    }
    
    cout << "Format completed successfully!\n";
    cout << "Total sectors: " << total_sectors << "\n";
    cout << "Sectors per cluster: " << (int)sectors_per_cluster << "\n";
    cout << "FAT size: " << fat_size << " sectors\n";
    cout << "Data starts at sector: " << data_start << "\n";
    
    return true;
}

// Format filesystem command
void cmd_formatfs(uint64_t ahci_base, int port) {
    size_t total_sectors = 65536;
    
    if (total_sectors < 65536) {
        cout << "Error: Minimum 65536 sectors required for FAT32\n";
        return;
    }
    

    size_t sec_per_clus = 64;
    
    // Validate sectors per cluster is power of 2 and reasonable
    if (sec_per_clus == 0 || sec_per_clus > 128 || 
        (sec_per_clus & (sec_per_clus - 1)) != 0) {
        cout << "Error: Sectors per cluster must be a power of 2 (1-128)\n";
        return;
    }
    
    cout << "\nFormatting disk with FAT32...\n";
    cout << "Total sectors: " << total_sectors << "\n";
    cout << "Sectors per cluster: " << sec_per_clus << "\n";
    cout << "This may take a few moments...\n";
    
    if (fat32_format(ahci_base, port, (uint32_t)total_sectors, (uint8_t)sec_per_clus)) {
        cout << "Disk formatted successfully!\n";
        cout << "Use 'mount' to mount the new filesystem.\n";
    } else {
        cout << "Format failed!\n";
    }
}


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

// Read a FAT entry
uint32_t read_fat_entry(uint64_t ahci_base, int port, uint32_t cluster) {
    if (cluster < 2) return FAT_BAD_CLUSTER;
    
    // Calculate FAT sector and offset
    uint32_t fat_offset = cluster * 4; // 4 bytes per FAT32 entry
    uint32_t fat_sector = fat_start_sector + (fat_offset / SECTOR_SIZE);
    uint32_t entry_offset = fat_offset % SECTOR_SIZE;
    
    uint8_t buffer[SECTOR_SIZE];
    if (read_sectors(ahci_base, port, fat_sector, 1, buffer) != 0) {
        return FAT_BAD_CLUSTER;
    }
    
    // Extract 32-bit FAT entry (only lower 28 bits are used)
    uint32_t fat_entry = *(uint32_t*)(buffer + entry_offset);
    return fat_entry & 0x0FFFFFFF;
}

// Write a FAT entry
bool write_fat_entry(uint64_t ahci_base, int port, uint32_t cluster, uint32_t value) {
    if (cluster < 2) return false;
    
    // Calculate FAT sector and offset
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_sector + (fat_offset / SECTOR_SIZE);
    uint32_t entry_offset = fat_offset % SECTOR_SIZE;
    
    uint8_t buffer[SECTOR_SIZE];
    if (read_sectors(ahci_base, port, fat_sector, 1, buffer) != 0) {
        return false;
    }
    
    // Update FAT entry (preserve upper 4 bits)
    uint32_t* fat_entry_ptr = (uint32_t*)(buffer + entry_offset);
    *fat_entry_ptr = (*fat_entry_ptr & 0xF0000000) | (value & 0x0FFFFFFF);
    
    // Write back to all FAT copies
    for (uint8_t fat_num = 0; fat_num < fat32_bpb.num_fats; fat_num++) {
        uint32_t current_fat_sector = fat_start_sector + (fat_num * fat32_bpb.fat_sz32) + (fat_offset / SECTOR_SIZE);
        if (write_sectors(ahci_base, port, current_fat_sector, 1, buffer) != 0) {
            return false;
        }
    }
    
    return true;
}

// Find next free cluster starting from a given cluster
uint32_t find_free_cluster(uint64_t ahci_base, int port, uint32_t start_cluster) {
    uint32_t max_clusters = (fat32_bpb.tot_sec32 - data_start_sector) / fat32_bpb.sec_per_clus + 2;
    
    for (uint32_t cluster = start_cluster; cluster < max_clusters; cluster++) {
        uint32_t fat_entry = read_fat_entry(ahci_base, port, cluster);
        if (fat_entry == FAT_FREE_CLUSTER) {
            return cluster;
        }
    }
    
    // Wrap around and search from cluster 2
    if (start_cluster > 2) {
        for (uint32_t cluster = 2; cluster < start_cluster; cluster++) {
            uint32_t fat_entry = read_fat_entry(ahci_base, port, cluster);
            if (fat_entry == FAT_FREE_CLUSTER) {
                return cluster;
            }
        }
    }
    
    return 0; // No free clusters
}

// Free a cluster chain starting from given cluster
void free_cluster_chain(uint64_t ahci_base, int port, uint32_t start_cluster) {
    uint32_t current_cluster = start_cluster;
    
    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF7) {
        uint32_t next_cluster = read_fat_entry(ahci_base, port, current_cluster);
        
        // Mark cluster as free
        if (!write_fat_entry(ahci_base, port, current_cluster, FAT_FREE_CLUSTER)) {
            cout << "Warning: Failed to free cluster " << current_cluster << "\n";
        }
        
        // Update next free cluster hint if this cluster is earlier
        if (current_cluster < next_free_cluster) {
            next_free_cluster = current_cluster;
        }
        
        current_cluster = next_cluster;
    }
}

// Allocate a single cluster
uint32_t allocate_cluster(uint64_t ahci_base, int port) {
    uint32_t cluster = find_free_cluster(ahci_base, port, next_free_cluster);
    if (cluster == 0) {
        cout << "Disk full: no free clusters available\n";
        return 0;
    }
    
    // Mark cluster as end of chain
    if (!write_fat_entry(ahci_base, port, cluster, FAT_END_OF_CHAIN)) {
        cout << "Failed to update FAT entry\n";
        return 0;
    }
    
    // Clear the cluster data
    uint8_t zero_buffer[SECTOR_SIZE];
    simple_memset(zero_buffer, 0, SECTOR_SIZE);
    uint64_t cluster_lba = cluster_to_lba(cluster);
    
    for (uint8_t s = 0; s < fat32_bpb.sec_per_clus; s++) {
        if (write_sectors(ahci_base, port, cluster_lba + s, 1, zero_buffer) != 0) {
            cout << "Failed to clear cluster data\n";
            // Still return the cluster as it's allocated in FAT
        }
    }
    
    next_free_cluster = cluster + 1;
    return cluster;
}

// Allocate a chain of clusters
uint32_t allocate_cluster_chain(uint64_t ahci_base, int port, uint32_t num_clusters) {
    if (num_clusters == 0) return 0;
    
    uint32_t first_cluster = allocate_cluster(ahci_base, port);
    if (first_cluster == 0) return 0;
    
    uint32_t current_cluster = first_cluster;
    
    for (uint32_t i = 1; i < num_clusters; i++) {
        uint32_t next_cluster = allocate_cluster(ahci_base, port);
        if (next_cluster == 0) {
            // Allocation failed, free what we've allocated so far
            free_cluster_chain(ahci_base, port, first_cluster);
            return 0;
        }
        
        // Link current cluster to next cluster
        if (!write_fat_entry(ahci_base, port, current_cluster, next_cluster)) {
            cout << "Failed to link clusters\n";
            free_cluster_chain(ahci_base, port, first_cluster);
            return 0;
        }
        
        current_cluster = next_cluster;
    }
    
    return first_cluster;
}

// Get cluster chain length
uint32_t get_cluster_chain_length(uint64_t ahci_base, int port, uint32_t start_cluster) {
    uint32_t length = 0;
    uint32_t current_cluster = start_cluster;
    
    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF7) {
        length++;
        current_cluster = read_fat_entry(ahci_base, port, current_cluster);
        
        // Prevent infinite loops
        if (length > 65536) {
            cout << "Warning: Possible corrupt cluster chain detected\n";
            break;
        }
    }
    
    return length;
}

// Calculate clusters needed for given size
uint32_t clusters_needed(uint32_t size) {
    uint32_t cluster_size = fat32_bpb.sec_per_clus * fat32_bpb.bytes_per_sec;
    return (size + cluster_size - 1) / cluster_size;
}

// Write data to cluster chain
bool write_data_to_clusters(uint64_t ahci_base, int port, uint32_t start_cluster, const void* data, uint32_t size) {
    const uint8_t* data_ptr = (const uint8_t*)data;
    uint32_t remaining = size;
    uint32_t current_cluster = start_cluster;
    uint32_t cluster_size = fat32_bpb.sec_per_clus * fat32_bpb.bytes_per_sec;
    
    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF7 && remaining > 0) {
        uint64_t cluster_lba = cluster_to_lba(current_cluster);
        uint32_t to_write = (remaining > cluster_size) ? cluster_size : remaining;
        
        // Write full sectors
        uint32_t full_sectors = to_write / SECTOR_SIZE;
        if (full_sectors > 0) {
            if (write_sectors(ahci_base, port, cluster_lba, full_sectors, (void*)data_ptr) != 0) {
                return false;
            }
            data_ptr += full_sectors * SECTOR_SIZE;
            remaining -= full_sectors * SECTOR_SIZE;
        }
        
        // Handle partial last sector
        uint32_t partial_bytes = to_write % SECTOR_SIZE;
        if (partial_bytes > 0) {
            uint8_t sector_buffer[SECTOR_SIZE];
            simple_memset(sector_buffer, 0, SECTOR_SIZE);
            simple_memcpy(sector_buffer, data_ptr, partial_bytes);
            
            if (write_sectors(ahci_base, port, cluster_lba + full_sectors, 1, sector_buffer) != 0) {
                return false;
            }
            remaining -= partial_bytes;
        }
        
        current_cluster = read_fat_entry(ahci_base, port, current_cluster);
    }
    
    return remaining == 0;
}

// Read data from cluster chain
bool read_data_from_clusters(uint64_t ahci_base, int port, uint32_t start_cluster, void* data, uint32_t size) {
    uint8_t* data_ptr = (uint8_t*)data;
    uint32_t remaining = size;
    uint32_t current_cluster = start_cluster;
    uint32_t cluster_size = fat32_bpb.sec_per_clus * fat32_bpb.bytes_per_sec;
    
    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF7 && remaining > 0) {
        uint64_t cluster_lba = cluster_to_lba(current_cluster);
        uint32_t to_read = (remaining > cluster_size) ? cluster_size : remaining;
        
        // Read full sectors
        uint32_t full_sectors = to_read / SECTOR_SIZE;
        if (full_sectors > 0) {
            if (read_sectors(ahci_base, port, cluster_lba, full_sectors, data_ptr) != 0) {
                return false;
            }
            data_ptr += full_sectors * SECTOR_SIZE;
            remaining -= full_sectors * SECTOR_SIZE;
        }
        
        // Handle partial last sector
        uint32_t partial_bytes = to_read % SECTOR_SIZE;
        if (partial_bytes > 0) {
            uint8_t sector_buffer[SECTOR_SIZE];
            if (read_sectors(ahci_base, port, cluster_lba + full_sectors, 1, sector_buffer) != 0) {
                return false;
            }
            simple_memcpy(data_ptr, sector_buffer, partial_bytes);
            remaining -= partial_bytes;
        }
        
        current_cluster = read_fat_entry(ahci_base, port, current_cluster);
    }
    
    return remaining == 0;
}

// List files in current directory
void fat32_list_files(uint64_t ahci_base, int port) {
    uint8_t buffer[SECTOR_SIZE];
    uint64_t lba = cluster_to_lba(current_directory_cluster);
    
    cout << "Directory listing:\n";
    cout << "Name          Size       Attr\n";
    cout << "--------------------------------\n";
    
    for (uint8_t s = 0; s < fat32_bpb.sec_per_clus; s++) {
        if (read_sectors(ahci_base, port, lba + s, 1, buffer) != 0) {
            cout << "Error reading directory\n";
            return;
        }
        
        for (uint8_t e = 0; e < SECTOR_SIZE / ENTRY_SIZE; e++) {
            fat_dir_entry_t *entry = (fat_dir_entry_t *)(buffer + e * ENTRY_SIZE);
            
            // End of directory
            if (entry->name[0] == 0) return;
            
            // Skip deleted entries and long filename entries
            if (entry->name[0] == DELETED_ENTRY) continue;
            if (entry->attr & ATTR_LONG_NAME) continue;
            if (entry->attr & ATTR_VOLUME_ID) continue;
            
            // Extract and display filename
            char fname[13];
            from_83_format(entry->name, fname);
            
            cout << fname;
            
            // Pad filename to 12 characters
            int len = simple_strlen(fname);
            for (int i = len; i < 12; i++) cout << " ";
            
            // Display size
            cout << entry->file_size << "       ";
            
            // Display attributes
            if (entry->attr & ATTR_DIRECTORY) cout << "DIR";
            else cout << "FILE";
            
            cout << "\n";
        }
    }
}

// Updated fat32_add_file with proper cluster allocation
int fat32_add_file(uint64_t ahci_base, int port, const char *filename, const void *data, uint32_t size) {
    uint8_t buffer[SECTOR_SIZE];
    uint64_t lba = cluster_to_lba(current_directory_cluster);
    char target[11];
    to_83_format(filename, target);
    
    // Check if file already exists
    for (uint8_t s = 0; s < fat32_bpb.sec_per_clus; s++) {
        if (read_sectors(ahci_base, port, lba + s, 1, buffer) != 0) {
            return -1;
        }
        
        for (uint8_t e = 0; e < SECTOR_SIZE / ENTRY_SIZE; e++) {
            fat_dir_entry_t *entry = (fat_dir_entry_t *)(buffer + e * ENTRY_SIZE);
            
            if (entry->name[0] == 0) break;
            if (entry->name[0] == DELETED_ENTRY) continue;
            if (entry->attr & ATTR_LONG_NAME) continue;
            
            if (simple_memcmp(entry->name, target, 11) == 0) {
                return -5; // File already exists
            }
        }
    }
    
    // Allocate clusters if file has data
    uint32_t first_cluster = 0;
    if (size > 0) {
        uint32_t needed_clusters = clusters_needed(size);
        first_cluster = allocate_cluster_chain(ahci_base, port, needed_clusters);
        if (first_cluster == 0) {
            return -6; // Disk full
        }
        
        // Write data to allocated clusters
        if (!write_data_to_clusters(ahci_base, port, first_cluster, data, size)) {
            free_cluster_chain(ahci_base, port, first_cluster);
            return -7; // Write failed
        }
    }
    
    // Find free directory entry
    for (uint8_t s = 0; s < fat32_bpb.sec_per_clus; s++) {
        if (read_sectors(ahci_base, port, lba + s, 1, buffer) != 0) {
            if (first_cluster) free_cluster_chain(ahci_base, port, first_cluster);
            return -1;
        }
        
        for (uint8_t e = 0; e < SECTOR_SIZE / ENTRY_SIZE; e++) {
            fat_dir_entry_t *entry = (fat_dir_entry_t *)(buffer + e * ENTRY_SIZE);
            
            if (entry->name[0] == 0 || entry->name[0]== DELETED_ENTRY) {
                // Create directory entry
                simple_memcpy(entry->name, target, 11);
                entry->attr = ATTR_ARCHIVE;
                entry->file_size = size;
                entry->fst_clus_lo = first_cluster & 0xFFFF;
                entry->fst_clus_hi = (first_cluster >> 16) & 0xFFFF;
                
                // Set timestamps (simplified)
                entry->crt_date = entry->wrt_date = 0x4E21; // Example date
                entry->crt_time = entry->wrt_time = 0x8000; // Example time
                entry->lst_acc_date = entry->crt_date;
                entry->crt_time_tenth = 0;
                entry->ntres = 0;
                
                // Write directory entry back
                if (write_sectors(ahci_base, port, lba + s, 1, buffer) != 0) {
                    if (first_cluster) free_cluster_chain(ahci_base, port, first_cluster);
                    return -2;
                }
                
                return 0; // Success
            }
        }
    }
    
    // No space in directory
    if (first_cluster) free_cluster_chain(ahci_base, port, first_cluster);
    return -4;
}

// Updated fat32_remove_file with proper cluster deallocation
int fat32_remove_file(uint64_t ahci_base, int port, const char *filename) {
    uint8_t buffer[SECTOR_SIZE];
    uint64_t lba = cluster_to_lba(current_directory_cluster);
    char target[11];
    to_83_format(filename, target);
    
    for (uint8_t s = 0; s < fat32_bpb.sec_per_clus; s++) {
        if (read_sectors(ahci_base, port, lba + s, 1, buffer) != 0) {
            return -1;
        }
        
        for (uint8_t e = 0; e < SECTOR_SIZE / ENTRY_SIZE; e++) {
            fat_dir_entry_t *entry = (fat_dir_entry_t *)(buffer + e * ENTRY_SIZE);
            
            if (entry->name[0] == 0) return -4; // End of directory
            if (entry->name[0] == DELETED_ENTRY) continue;
            if (entry->attr & ATTR_LONG_NAME) continue;
            
            if (simple_memcmp(entry->name, target, 11) == 0) {
                // Get first cluster
                uint32_t first_cluster = (entry->fst_clus_hi << 16) | entry->fst_clus_lo;
                
                // Mark directory entry as deleted
                entry->name[0] = DELETED_ENTRY;
                
                // Write directory entry back
                if (write_sectors(ahci_base, port, lba + s, 1, buffer) != 0) {
                    return -2;
                }
                
                // Free cluster chain if file had clusters allocated
                if (first_cluster >= 2) {
                    free_cluster_chain(ahci_base, port, first_cluster);
                }
                
                return 0; // Success
            }
        }
    }
    
    return -4; // File not found
}

// Updated fat32_read_file with proper cluster chain following
void fat32_read_file(uint64_t ahci_base, int port, const char* filename) {
    uint8_t buffer[SECTOR_SIZE];
    uint64_t lba = cluster_to_lba(current_directory_cluster);
    char target[11];
    to_83_format(filename, target);
    
    // Find file in directory
    for (uint8_t s = 0; s < fat32_bpb.sec_per_clus; s++) {
        if (read_sectors(ahci_base, port, lba + s, 1, buffer) != 0) {
            cout << "Error reading directory\n";
            return;
        }
        
        for (uint8_t e = 0; e < SECTOR_SIZE / ENTRY_SIZE; e++) {
            fat_dir_entry_t *entry = (fat_dir_entry_t *)(buffer + e * ENTRY_SIZE);
            
            if (entry->name[0] == 0) break;
            if (entry->name[0] == DELETED_ENTRY) continue;

            if (entry->attr & (ATTR_LONG_NAME | ATTR_DIRECTORY)) continue;
            
            if (simple_memcmp(entry->name, target, 11) == 0) {
                uint32_t first_cluster = (entry->fst_clus_hi << 16) | entry->fst_clus_lo;
                uint32_t file_size = entry->file_size;
                
                cout << "Contents of " << filename << " (" << file_size << " bytes):\n";
                cout << "--------------------------------\n";
                
                if (file_size == 0) {
                    cout << "(Empty file)\n";
                } else if (first_cluster >= 2) {
                    // Allocate buffer for file data (limit display to reasonable size)
                    uint32_t display_size = (file_size > 2048) ? 2048 : file_size;
                    uint8_t* file_data = new uint8_t[display_size];
                    
                    if (read_data_from_clusters(ahci_base, port, first_cluster, file_data, display_size)) {
                        for (uint32_t i = 0; i < display_size; i++) {
                            char c = file_data[i];
                            if (c >= 32 && c <= 126) cout << c;
                            else if (c == '\n' || c == '\r' || c == '\t') cout << c;
                            else cout << '.';
                        }
                        if (file_size > display_size) {
                            cout << "\n... (truncated, showing first " << display_size << " bytes)";
                        }
                    } else {
                        cout << "Error reading file data\n";
                    }
                    
                    delete[] file_data;
                } else {
                    cout << "File has no allocated clusters\n";
                }
                
                cout << "\n--------------------------------\n";
                return;
            }
        }
    }
    
    cout << "File '" << filename << "' not found\n";
}

// Show filesystem information
void fat32_show_filesystem_info(uint64_t ahci_base, int port) {
    cout << "FAT32 Filesystem Information:\n";
    cout << "=============================\n";
    cout << "OEM Name: ";
    for (int i = 0; i < 8; i++) cout << fat32_bpb.oem_name[i];
    cout << "\n";
    cout << "Bytes per Sector: " << fat32_bpb.bytes_per_sec << "\n";
    cout << "Sectors per Cluster: " << (int)fat32_bpb.sec_per_clus << "\n";
    cout << "Reserved Sectors: " << fat32_bpb.rsvd_sec_cnt << "\n";
    cout << "Number of FATs: " << (int)fat32_bpb.num_fats << "\n";
    cout << "Sectors per FAT: " << fat32_bpb.fat_sz32 << "\n";
    cout << "Root Cluster: " << fat32_bpb.root_clus << "\n";
    cout << "Total Sectors: " << fat32_bpb.tot_sec32 << "\n";
    cout << "FAT Start Sector: " << fat_start_sector << "\n";
    cout << "Data Start Sector: " << data_start_sector << "\n";
    cout << "Current Directory Cluster: " << current_directory_cluster << "\n";
}

// Show cluster allocation statistics
void show_cluster_stats(uint64_t ahci_base, int port) {
    uint32_t max_clusters = (fat32_bpb.tot_sec32 - data_start_sector) / fat32_bpb.sec_per_clus + 2;
    uint32_t free_clusters = 0;
    uint32_t used_clusters = 0;
    uint32_t bad_clusters = 0;
    
    cout << "Scanning FAT for cluster statistics...\n";
    
    for (uint32_t cluster = 2; cluster < max_clusters; cluster++) {
        uint32_t fat_entry = read_fat_entry(ahci_base, port, cluster);
        
        if (fat_entry == FAT_FREE_CLUSTER) {
            free_clusters++;
        } else if (fat_entry == FAT_BAD_CLUSTER) {
            bad_clusters++;
        } else {
            used_clusters++;
        }
        
        // Progress indicator
        if ((cluster % 1000) == 0) {
            cout << ".";
        }
    }
    
    cout << "\n\nCluster Statistics:\n";
    cout << "==================\n";
    cout << "Total Clusters: " << (max_clusters - 2) << "\n";
    cout << "Free Clusters:  " << free_clusters << "\n";
    cout << "Used Clusters:  " << used_clusters << "\n";
    cout << "Bad Clusters:   " << bad_clusters << "\n";
    cout << "Next Free Hint: " << next_free_cluster << "\n";
    
    uint32_t cluster_size = fat32_bpb.sec_per_clus * fat32_bpb.bytes_per_sec;
    uint64_t free_space = (uint64_t)free_clusters * cluster_size;
    uint64_t used_space = (uint64_t)used_clusters * cluster_size;
    
    cout << "Free Space:     " << (unsigned int)(free_space / 1024) << " KB\n";
    cout << "Used Space:     " << (unsigned int)(used_space / 1024) << " KB\n";
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
            
        // DMA COMMANDS
        } else if (stricmp(cmd_str, "formatfs") == 0) {
            cmd_formatfs(ahci_base, port);
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
        } else if (stricmp(cmd_str, "read") == 0 && !fat32_initialized) {
            // Only handle as test command if FAT32 not mounted
            cout << "Reading test file...\n";
            // Add your test file read logic here
        } else if (stricmp(cmd_str, "write") == 0 && !fat32_initialized) {
            // Only handle as test command if FAT32 not mounted
            cout << "Writing to test file...\n";
            // Add your test file write logic here
            
        // FAT32 FILESYSTEM COMMANDS
        } else if (stricmp(cmd_str, "mount") == 0) {

            fat32_initialized = true;
            cout << "FAT32 filesystem mounted successfully.\n";

        } else if (stricmp(cmd_str, "unmount") == 0) {
            fat32_initialized = false;
            cout << "FAT32 filesystem unmounted.\n";
        } else if (stricmp(cmd_str, "ls") == 0 || stricmp(cmd_str, "dir") == 0) {
            if (!fat32_initialized) {
                cout << "FAT32 filesystem not mounted. Use 'mount' first.\n";
            } else {
                fat32_list_files(ahci_base, port);
            }
        } else if (stricmp(cmd_str, "cat") == 0 || 
                  (stricmp(cmd_str, "read") == 0 && fat32_initialized)) {
            if (!fat32_initialized) {
                cout << "FAT32 filesystem not mounted. Use 'mount' first.\n";
            } else if (!args) {
                cout << "Usage: cat <filename>\n";
                cout << "Display the contents of a file\n";
                cout << "Example: cat readme.txt\n";
            } else {
                fat32_read_file(ahci_base, port, args);
            }
        } else if (stricmp(cmd_str, "create") == 0 || 
                  (stricmp(cmd_str, "write") == 0 && fat32_initialized)) {
            if (!fat32_initialized) {
                cout << "FAT32 filesystem not mounted. Use 'mount' first.\n";
            } else if (!args) {
                cout << "Usage: create <filename> <content>\n";
                cout << "Create a new file with specified content\n";
                cout << "Example: create hello.txt \"Hello World!\"\n";
            } else {
                // Parse filename and content
                char* content_start = strchr(args, ' ');
                if (content_start) {
                    *content_start = '\0'; // Null terminate filename
                    content_start++; // Move to content
                    int result = fat32_add_file(ahci_base, port, args, content_start, simple_strlen(content_start));
                    if (result == 0) {
                        cout << "File '" << args << "' created successfully.\n";
                    } else {
                        cout << "Failed to create file '" << args << "' (error " << result << ")\n";
                        if (result == -5) cout << "  → File already exists\n";
                        else if (result == -6) cout << "  → Disk full\n";
                        else if (result == -7) cout << "  → Write failed\n";
                        else if (result == -4) cout << "  → No space in directory\n";
                    }
                } else {
                    cout << "Usage: create <filename> <content>\n";
                    cout << "Example: create test.txt \"This is test content\"\n";
                }
            }
        } else if (stricmp(cmd_str, "delete") == 0 || stricmp(cmd_str, "rm") == 0) {
            if (!fat32_initialized) {
                cout << "FAT32 filesystem not mounted. Use 'mount' first.\n";
            } else if (!args) {
                cout << "Usage: delete <filename> | rm <filename>\n";
                cout << "Remove a file from the filesystem\n";
                cout << "Example: delete oldfile.txt\n";
            } else {
                int result = fat32_remove_file(ahci_base, port, args);
                if (result == 0) {
                    cout << "File '" << args << "' deleted successfully.\n";
                } else {
                    cout << "Failed to delete file '" << args << "' (error " << result << ")\n";
                    if (result == -4) cout << "  → File not found\n";
                }
            }
        } else if (stricmp(cmd_str, "touch") == 0) {
            if (!fat32_initialized) {
                cout << "FAT32 filesystem not mounted. Use 'mount' first.\n";
            } else if (!args) {
                cout << "Usage: touch <filename>\n";
                cout << "Create an empty file\n";
                cout << "Example: touch newfile.txt\n";
            } else {
                int result = fat32_add_file(ahci_base, port, args, "", 0);
                if (result == 0) {
                    cout << "Empty file '" << args << "' created.\n";
                } else {
                    cout << "Failed to create file '" << args << "' (error " << result << ")\n";
                    if (result == -5) cout << "  → File already exists\n";
                    else if (result == -4) cout << "  → No space in directory\n";
                }
            }
        } else if (stricmp(cmd_str, "fsinfo") == 0) {
            if (!fat32_initialized) {
                cout << "FAT32 filesystem not mounted. Use 'mount' first.\n";
            } else {
                fat32_show_filesystem_info(ahci_base, port);
            }
        } else if (stricmp(cmd_str, "clusterstats") == 0) {
            if (!fat32_initialized) {
                cout << "FAT32 filesystem not mounted. Use 'mount' first.\n";
            } else {
                show_cluster_stats(ahci_base, port);
            }
        } else if (stricmp(cmd_str, "fshelp") == 0) {
            cout << "FAT32 FILESYSTEM COMMANDS\n";
            cout << "mount                      initialize FAT32 filesystem\n";
            cout << "unmount                    disconnect FAT32 filesystem\n";
            cout << "ls | dir                   list files and directories\n";
            cout << "cat <filename>             display file contents\n";
            cout << "create <filename> <data>   create file with content\n";
            cout << "touch <filename>           create empty file\n";
            cout << "delete <filename>          remove file\n";
            cout << "rm <filename>              alias for delete\n";
            cout << "fsinfo                     show filesystem information\n";
            cout << "clusterstats               show cluster allocation stats\n";
            cout << "\n";
            cout << "EXAMPLES:\n";
            cout << "  mount\n";
            cout << "  create hello.txt \"Hello World!\"\n";
            cout << "  ls\n";
            cout << "  cat hello.txt\n";
            cout << "  delete hello.txt\n";
            cout << "  clusterstats\n";
            
        // UNKNOWN COMMAND
        } else {
            cout << "Unknown command: " << input << "\n";
            cout << "Type 'help' for a list of commands.\n";
            
            // Smart suggestions based on common typos
            if (stricmp(cmd_str, "list") == 0 || stricmp(cmd_str, "ll") == 0) {
                cout << "Did you mean 'ls' or 'dir'?\n";
            } else if (stricmp(cmd_str, "remove") == 0 || stricmp(cmd_str, "del") == 0) {
                cout << "Did you mean 'delete' or 'rm'?\n";
            } else if (stricmp(cmd_str, "show") == 0 || stricmp(cmd_str, "display") == 0) {
                cout << "Did you mean 'cat' to display a file?\n";
            } else if (stricmp(cmd_str, "make") == 0 || stricmp(cmd_str, "new") == 0) {
                cout << "Did you mean 'create' or 'touch'?\n";
            }
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

