#pragma once
#include <stdint.h>
#include <stddef.h>

#define DMA_MAX_CHANNELS 8
#define DMA_BUFFER_SIZE 4096
#define DMA_ALIGNMENT 64

class DMAManager {
private:
    bool initialized;
    uint32_t active_channels;
    
public:
    DMAManager() : initialized(false), active_channels(0) {}
    
    // Core DMA functions - DECLARE EACH METHOD ONLY ONCE
    bool initialize(uint64_t base_address);
    int allocate_channel();
    void free_channel(int channel_id);
    bool setup_transfer(int channel_id, uint64_t src, uint64_t dst, size_t length);
    bool start_transfer(int channel_id);
    bool wait_transfer_complete(int channel_id);
    
    // Memory management
    void* allocate_dma_buffer(size_t size);
    void free_dma_buffer(void* buffer);
    
    // Memory operations - ONLY ONE DECLARATION EACH
    bool read_memory_dma(uint64_t address, void* buffer, size_t size);
    bool write_memory_dma(uint64_t address, const void* data, size_t size);
    void dump_memory_region(uint64_t start_addr, size_t length);
    
    // Advanced operations - ADD THESE MISSING METHODS
    bool pattern_fill(uint64_t address, uint8_t pattern, size_t length);
    bool memory_copy(uint64_t src, uint64_t dst, size_t length);
    void show_channel_status();
    bool verify_memory_range(uint64_t address, size_t size);
};

