#include "dma_memory.h"
#include "iostream_wrapper.h"

bool DMAManager::initialize(uint64_t base_address) {
    initialized = true;
    active_channels = 0;
    cout << "DMA Manager initialized at base: 0x";
    
    // Convert uint64_t to hex string manually
    char hex_addr[17];
    uint64_t addr = base_address;
    int pos = 15;
    hex_addr[16] = '\0';
    
    do {
        int digit = addr & 0xF;
        hex_addr[pos--] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        addr >>= 4;
    } while (addr > 0 && pos >= 0);
    
    while (pos >= 0) {
        hex_addr[pos--] = '0';
    }
    
    cout << hex_addr << "\n";
    return true;
}

void* DMAManager::allocate_dma_buffer(size_t size) {
    static uint8_t dma_heap[64 * 1024]; // 64KB heap
    static size_t heap_offset = 0;
    
    size_t aligned_size = (size + DMA_ALIGNMENT - 1) & ~(DMA_ALIGNMENT - 1);
    
    if (heap_offset + aligned_size > sizeof(dma_heap)) {
        return nullptr;
    }
    
    void* buffer = &dma_heap[heap_offset];
    heap_offset += aligned_size;
    return buffer;
}

void DMAManager::free_dma_buffer(void* buffer) {
    // Simple implementation - in real kernel, implement proper free
}

int DMAManager::allocate_channel() {
    for (int i = 0; i < DMA_MAX_CHANNELS; i++) {
        if (!(active_channels & (1 << i))) {
            active_channels |= (1 << i);
            return i;
        }
    }
    return -1;
}

void DMAManager::free_channel(int channel_id) {
    if (channel_id >= 0 && channel_id < DMA_MAX_CHANNELS) {
        active_channels &= ~(1 << channel_id);
    }
}

bool DMAManager::setup_transfer(int channel_id, uint64_t src, uint64_t dst, size_t length) {
    cout << "Setup DMA transfer on channel " << channel_id << "\n";
    return true;
}

bool DMAManager::start_transfer(int channel_id) {
    cout << "Started DMA transfer on channel " << channel_id << "\n";
    return true;
}

bool DMAManager::wait_transfer_complete(int channel_id) {
    cout << "DMA transfer complete on channel " << channel_id << "\n";
    return true;
}

bool DMAManager::read_memory_dma(uint64_t address, void* buffer, size_t size) {
    if (!initialized) return false;
    
    // Direct memory read (replace with actual DMA when hardware ready)
    uint8_t* src = (uint8_t*)address;
    uint8_t* dst = (uint8_t*)buffer;
    
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
    return true;
}

bool DMAManager::write_memory_dma(uint64_t address, const void* data, size_t size) {
    if (!initialized) {
        cout << "DMA Manager not initialized\n";
        return false;
    }
    
    if (!verify_memory_range(address, size)) {
        cout << "Invalid memory range for DMA write\n";
        return false;
    }
    
    // Direct memory write (replace with actual DMA when hardware ready)
    const uint8_t* src = (const uint8_t*)data;
    uint8_t* dst = (uint8_t*)address;
    
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
    
    cout << "DMA write completed: " << (int)size << " bytes\n";
    return true;
}

bool DMAManager::pattern_fill(uint64_t address, uint8_t pattern, size_t length) {
    if (!initialized) return false;
    
    cout << "DMA pattern fill: 0x";
    char hex_pattern[3];
    hex_pattern[0] = (pattern >> 4) < 10 ? ('0' + (pattern >> 4)) : ('A' + (pattern >> 4) - 10);
    hex_pattern[1] = (pattern & 0xF) < 10 ? ('0' + (pattern & 0xF)) : ('A' + (pattern & 0xF) - 10);
    hex_pattern[2] = '\0';
    cout << hex_pattern << " for " << (int)length << " bytes\n";
    
    // Use chunked approach for large fills
    void* buffer = allocate_dma_buffer(length < 1024 ? length : 1024);
    if (!buffer) return false;
    
    uint8_t* fill_data = (uint8_t*)buffer;
    size_t buffer_size = length < 1024 ? length : 1024;
    
    // Fill buffer with pattern
    for (size_t i = 0; i < buffer_size; i++) {
        fill_data[i] = pattern;
    }
    
    // Write in chunks
    size_t remaining = length;
    uint64_t current_addr = address;
    
    while (remaining > 0) {
        size_t chunk_size = remaining < buffer_size ? remaining : buffer_size;
        
        if (!write_memory_dma(current_addr, buffer, chunk_size)) {
            free_dma_buffer(buffer);
            return false;
        }
        
        remaining -= chunk_size;
        current_addr += chunk_size;
    }
    
    free_dma_buffer(buffer);
    cout << "Pattern fill completed\n";
    return true;
}

bool DMAManager::memory_copy(uint64_t src, uint64_t dst, size_t length) {
    if (!initialized) return false;
    
    cout << "DMA memory copy: " << (int)length << " bytes\n";
    
    const size_t chunk_size = 1024;
    void* temp_buffer = allocate_dma_buffer(chunk_size);
    if (!temp_buffer) return false;
    
    size_t remaining = length;
    uint64_t current_src = src;
    uint64_t current_dst = dst;
    
    while (remaining > 0) {
        size_t copy_size = remaining < chunk_size ? remaining : chunk_size;
        
        if (!read_memory_dma(current_src, temp_buffer, copy_size)) {
            free_dma_buffer(temp_buffer);
            return false;
        }
        
        if (!write_memory_dma(current_dst, temp_buffer, copy_size)) {
            free_dma_buffer(temp_buffer);
            return false;
        }
        
        remaining -= copy_size;
        current_src += copy_size;
        current_dst += copy_size;
    }
    
    free_dma_buffer(temp_buffer);
    cout << "Memory copy completed successfully\n";
    return true;
}

void DMAManager::show_channel_status() {
    cout << "=== DMA Channel Status ===\n";
    cout << "Initialized: " << (initialized ? "Yes" : "No") << "\n";
    cout << "Active channels: ";
    
    int count = 0;
    for (int i = 0; i < DMA_MAX_CHANNELS; i++) {
        if (active_channels & (1 << i)) count++;
    }
    cout << count << "/" << DMA_MAX_CHANNELS << "\n";
    
    for (int i = 0; i < DMA_MAX_CHANNELS; i++) {
        cout << "Channel " << i << ": ";
        if (active_channels & (1 << i)) {
            cout << "ACTIVE";
        } else {
            cout << "FREE";
        }
        cout << "\n";
    }
}

bool DMAManager::verify_memory_range(uint64_t address, size_t size) {
    // Basic memory range validation
    //if (address == 0) return false;  // Null pointer
    //if (size == 0) return false;     // Zero size
    //if (size > 0x10000000) return false;  // 256MB max transfer
    
    // Avoid critical system memory ranges
    //if (address < 0x1000) return false;  // First 4KB reserved
    //if (address >= 0xF0000000 && address < 0x100000000) return false;  // Hardware ranges
    
    return true;
}

void DMAManager::dump_memory_region(uint64_t start_addr, size_t length) {
    cout << "Memory Dump - Address: 0x";
    
    // Convert uint64_t to hex string manually
    char hex_addr[17];
    uint64_t addr = start_addr;
    int pos = 15;
    hex_addr[16] = '\0';
    
    do {
        int digit = addr & 0xF;
        hex_addr[pos--] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        addr >>= 4;
    } while (addr > 0 && pos >= 0);
    
    while (pos >= 0) {
        hex_addr[pos--] = '0';
    }
    
    cout << hex_addr << " Length: " << (int)length << " bytes\n";
    
    uint8_t* mem_addr = (uint8_t*)start_addr;
    
    for (size_t i = 0; i < length; i += 16) {
        // Print address for this line
        uint64_t line_addr = start_addr + i;
        char line_hex[17];
        uint64_t temp_addr = line_addr;
        int temp_pos = 15;
        line_hex[16] = '\0';
        
        do {
            int digit = temp_addr & 0xF;
            line_hex[temp_pos--] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
            temp_addr >>= 4;
        } while (temp_addr > 0 && temp_pos >= 0);
        
        while (temp_pos >= 0) {
            line_hex[temp_pos--] = '0';
        }
        
        cout << line_hex << ": ";
        
        // Hex dump
        for (size_t j = 0; j < 16 && (i + j) < length; j++) {
            uint8_t byte = mem_addr[i + j];
            char hex_byte[3];
            hex_byte[0] = (byte >> 4) < 10 ? ('0' + (byte >> 4)) : ('A' + (byte >> 4) - 10);
            hex_byte[1] = (byte & 0xF) < 10 ? ('0' + (byte & 0xF)) : ('A' + (byte & 0xF) - 10);
            hex_byte[2] = '\0';
            cout << hex_byte << " ";
        }
        
        cout << " | ";
        
        // ASCII representation
        for (size_t j = 0; j < 16 && (i + j) < length; j++) {
            char c = mem_addr[i + j];
            cout << (c >= 32 && c <= 126 ? c : '.');
        }
        cout << "\n";
    }
}

