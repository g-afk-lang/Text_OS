#include "stdlib_hooks.h"
#include <cstdarg>  // For va_list, va_start, va_arg, va_end
#include "terminal_hooks.h"

// Static member initialization for KernelHeap
uint8_t KernelHeap::heap_space[HEAP_SIZE];
KernelHeap::MemoryBlock* KernelHeap::free_list = nullptr;

// Global formatting state
extern bool use_hex;

// String operations
bool string_compare(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* original_dest = dest;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return original_dest;
}

// Memory operations
void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    
    // Check for word-aligned addresses for optimization
    if (((reinterpret_cast<uintptr_t>(dest) & 0x3) == 0) && 
        ((reinterpret_cast<uintptr_t>(src) & 0x3) == 0) && 
        (n >= 4)) {
        
        // Copy 4 bytes at a time when possible
        size_t words = n / 4;
        uint32_t* d32 = static_cast<uint32_t*>(dest);
        const uint32_t* s32 = static_cast<const uint32_t*>(src);
        
        for (size_t i = 0; i < words; i++) {
            d32[i] = s32[i];
        }
        
        // Handle remaining bytes
        for (size_t i = words * 4; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        // Byte-by-byte copy for unaligned addresses
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    }
    
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    
    // If destination and source overlap, copy backwards
    if (d > s && d < s + n) {
        for (size_t i = n; i > 0; i--) {
            d[i-1] = s[i-1];
        }
    } else {
        // Otherwise, use standard copy
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    }
    
    return dest;
}

void* memset(void* dest, int val, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    unsigned char v = static_cast<unsigned char>(val);
    
    // Check for word-aligned addresses for optimization
    if (((reinterpret_cast<uintptr_t>(dest) & 0x3) == 0) && n >= 4) {
        // Fill 4 bytes at a time when possible
        size_t words = n / 4;
        uint32_t v32 = v | (v << 8) | (v << 16) | (v << 24);
        uint32_t* d32 = static_cast<uint32_t*>(dest);
        
        for (size_t i = 0; i < words; i++) {
            d32[i] = v32;
        }
        
        // Handle remaining bytes
        for (size_t i = words * 4; i < n; i++) {
            d[i] = v;
        }
    } else {
        // Byte-by-byte fill for unaligned addresses
        for (size_t i = 0; i < n; i++) {
            d[i] = v;
        }
    }
    
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = static_cast<const unsigned char*>(s1);
    const unsigned char* p2 = static_cast<const unsigned char*>(s2);
    
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }
    
    return 0;
}

char* strncpy(char* dest, const char* src, size_t n) {
    char* original_dest = dest;
    size_t i;
    
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    
    return original_dest;
}

char* strcat(char* dest, const char* src) {
    char* original_dest = dest;
    while (*dest) {
        dest++;
    }
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return original_dest;
}

char* strncat(char* dest, const char* src, size_t n) {
    char* original_dest = dest;
    size_t dest_len = strlen(dest);
    size_t i;
    
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[dest_len + i] = src[i];
    }
    
    dest[dest_len + i] = '\0';
    return original_dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return s1[i] < s2[i] ? -1 : 1;
        }
        if (s1[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

char* strchr(const char* s, int c) {
    while (*s && *s != (char)c) {
        s++;
    }
    return *s == (char)c ? const_cast<char*>(s) : nullptr;
}

char* strrchr(const char* s, int c) {
    const char* last = nullptr;
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    return const_cast<char*>(last);
}

// Simple kernel-level printf implementation
int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    int chars_written = 0;
    
    for (const char* p = format; *p; p++) {
        if (*p != '%') {
            terminal_putchar(*p);
            chars_written++;
            continue;
        }
        
        // Handle format specifier
        switch (*++p) {
            case 'c': {
                char c = va_arg(args, int);  // char is promoted to int in varargs
                terminal_putchar(c);
                chars_written++;
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                while (*s) {
                    terminal_putchar(*s++);
                    chars_written++;
                }
                break;
            }
            case 'd': 
            case 'i': {
                int num = va_arg(args, int);
                // Handle negative numbers
                if (num < 0) {
                    terminal_putchar('-');
                    chars_written++;
                    num = -num;
                }
                
                // Convert to string and print digits
                char buffer[12];  // Enough for 32-bit int
                int i = 0;
                
                // Handle 0 specially
                if (num == 0) {
                    terminal_putchar('0');
                    chars_written++;
                } else {
                    // Convert digits
                    while (num > 0) {
                        buffer[i++] = '0' + (num % 10);
                        num /= 10;
                    }
                    
                    // Print in reverse order
                    while (--i >= 0) {
                        terminal_putchar(buffer[i]);
                        chars_written++;
                    }
                }
                break;
            }
            case 'u': {
                unsigned int num = va_arg(args, unsigned int);
                char buffer[12];  // Enough for 32-bit unsigned
                int i = 0;
                
                // Handle 0 specially
                if (num == 0) {
                    terminal_putchar('0');
                    chars_written++;
                } else {
                    // Convert digits
                    while (num > 0) {
                        buffer[i++] = '0' + (num % 10);
                        num /= 10;
                    }
                    
                    // Print in reverse order
                    while (--i >= 0) {
                        terminal_putchar(buffer[i]);
                        chars_written++;
                    }
                }
                break;
            }
            case 'x': {
                unsigned int num = va_arg(args, unsigned int);
                char buffer[12];  // Enough for 32-bit hex
                int i = 0;
                
                // Handle 0 specially
                if (num == 0) {
                    terminal_putchar('0');
                    chars_written++;
                } else {
                    // Convert digits
                    while (num > 0) {
                        int digit = num % 16;
                        buffer[i++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
                        num /= 16;
                    }
                    
                    // Print in reverse order
                    while (--i >= 0) {
                        terminal_putchar(buffer[i]);
                        chars_written++;
                    }
                }
                break;
            }
            case 'p': {
                terminal_putchar('0');
                terminal_putchar('x');
                chars_written += 2;
                
                void* ptr = va_arg(args, void*);
                uintptr_t num = reinterpret_cast<uintptr_t>(ptr);
                
                char buffer[12];  // Enough for 32-bit pointer
                int i = 0;
                
                // Handle null pointer
                if (num == 0) {
                    terminal_putchar('0');
                    chars_written++;
                } else {
                    // Convert digits
                    while (num > 0) {
                        int digit = num % 16;
                        buffer[i++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
                        num /= 16;
                    }
                    
                    // Print in reverse order
                    while (--i >= 0) {
                        terminal_putchar(buffer[i]);
                        chars_written++;
                    }
                }
                break;
            }
            case '%': {
                terminal_putchar('%');
                chars_written++;
                break;
            }
            default: {
                // Unknown format specifier, just output it
                terminal_putchar('%');
                terminal_putchar(*p);
                chars_written += 2;
                break;
            }
        }
    }
    
    va_end(args);
    return chars_written;
}

int sprintf(char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    int chars_written = 0;
    
    for (const char* p = format; *p; p++) {
        if (*p != '%') {
            str[chars_written++] = *p;
            continue;
        }
        
        // Handle format specifier
        switch (*++p) {
            case 'c': {
                char c = va_arg(args, int);  // char is promoted to int in varargs
                str[chars_written++] = c;
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                while (*s) {
                    str[chars_written++] = *s++;
                }
                break;
            }
            case 'd': 
            case 'i': {
                int num = va_arg(args, int);
                // Handle negative numbers
                if (num < 0) {
                    str[chars_written++] = '-';
                    num = -num;
                }
                
                // Convert to string and print digits
                char buffer[12];  // Enough for 32-bit int
                int i = 0;
                
                // Handle 0 specially
                if (num == 0) {
                    str[chars_written++] = '0';
                } else {
                    // Convert digits
                    while (num > 0) {
                        buffer[i++] = '0' + (num % 10);
                        num /= 10;
                        }
                    
                    // Print in reverse order
                    while (--i >= 0) {
                        str[chars_written++] = buffer[i];
                    }
                }
                break;
            }
            case 'u': {
                unsigned int num = va_arg(args, unsigned int);
                char buffer[12];  // Enough for 32-bit unsigned
                int i = 0;
                
                // Handle 0 specially
                if (num == 0) {
                    str[chars_written++] = '0';
                } else {
                    // Convert digits
                    while (num > 0) {
                        buffer[i++] = '0' + (num % 10);
                        num /= 10;
                    }
                    
                    // Print in reverse order
                    while (--i >= 0) {
                        str[chars_written++] = buffer[i];
                    }
                }
                break;
            }
            case 'x': {
                unsigned int num = va_arg(args, unsigned int);
                char buffer[12];  // Enough for 32-bit hex
                int i = 0;
                
                // Handle 0 specially
                if (num == 0) {
                    str[chars_written++] = '0';
                } else {
                    // Convert digits
                    while (num > 0) {
                        int digit = num % 16;
                        buffer[i++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
                        num /= 16;
                    }
                    
                    // Print in reverse order
                    while (--i >= 0) {
                        str[chars_written++] = buffer[i];
                    }
                }
                break;
            }
            case 'p': {
                str[chars_written++] = '0';
                str[chars_written++] = 'x';
                
                void* ptr = va_arg(args, void*);
                uintptr_t num = reinterpret_cast<uintptr_t>(ptr);
                
                char buffer[12];  // Enough for 32-bit pointer
                int i = 0;
                
                // Handle null pointer
                if (num == 0) {
                    str[chars_written++] = '0';
                } else {
                    // Convert digits
                    while (num > 0) {
                        int digit = num % 16;
                        buffer[i++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
                        num /= 16;
                    }
                    
                    // Print in reverse order
                    while (--i >= 0) {
                        str[chars_written++] = buffer[i];
                    }
                }
                break;
            }
            case '%': {
                str[chars_written++] = '%';
                break;
            }
            default: {
                // Unknown format specifier, just output it
                str[chars_written++] = '%';
                str[chars_written++] = *p;
                break;
            }
        }
    }
    
    // Null-terminate the string
    str[chars_written] = '\0';
    
    va_end(args);
    return chars_written;
}

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    int chars_written = 0;
    size_t remaining = size > 0 ? size - 1 : 0;  // Leave room for null terminator
    
    for (const char* p = format; *p && remaining > 0; p++) {
        if (*p != '%') {
            str[chars_written++] = *p;
            remaining--;
            continue;
        }
        
        // Handle format specifier (abbreviated implementation)
        switch (*++p) {
            case 'c': {
                if (remaining > 0) {
                    char c = va_arg(args, int);
                    str[chars_written++] = c;
                    remaining--;
                } else {
                    va_arg(args, int);  // Consume but don't write
                }
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                while (*s && remaining > 0) {
                    str[chars_written++] = *s++;
                    remaining--;
                }
                // Skip remaining characters if no space
                while (*s) s++;
                break;
            }
            case 'd': 
            case 'i': {
                int num = va_arg(args, int);
                // Handle negative numbers
                if (num < 0 && remaining > 0) {
                    str[chars_written++] = '-';
                    remaining--;
                    num = -num;
                }
                
                // Convert to string
                char buffer[12];  // Enough for 32-bit int
                int i = 0;
                
                // Handle 0 specially
                if (num == 0) {
                    if (remaining > 0) {
                        str[chars_written++] = '0';
                        remaining--;
                    }
                } else {
                    // Convert digits
                    while (num > 0) {
                        buffer[i++] = '0' + (num % 10);
                        num /= 10;
                    }
                    
                    // Print in reverse order
                    while (--i >= 0 && remaining > 0) {
                        str[chars_written++] = buffer[i];
                        remaining--;
                    }
                }
                break;
            }
            case 'u':
            case 'x':
            case 'p': {
                // Just consume the argument but don't process to keep example simple
                va_arg(args, unsigned int);
                break;
            }
            case '%': {
                if (remaining > 0) {
                    str[chars_written++] = '%';
                    remaining--;
                }
                break;
            }
            default: {
                // Unknown format specifier, just output it if room
                if (remaining >= 2) {
                    str[chars_written++] = '%';
                    str[chars_written++] = *p;
                    remaining -= 2;
                } else if (remaining == 1) {
                    str[chars_written++] = '%';
                    remaining = 0;
                }
                break;
            }
        }
    }
    
    // Null-terminate the string
    str[chars_written < size ? chars_written : size - 1] = '\0';
    
    va_end(args);
    return chars_written;
}

// KernelHeap implementation
void KernelHeap::init() {
    // Initialize the heap with a single free block
    free_list = reinterpret_cast<MemoryBlock*>(heap_space);
    free_list->size = HEAP_SIZE - sizeof(MemoryBlock);
    free_list->used = false;
    free_list->next = nullptr;
}

void* KernelHeap::allocate(size_t size) {
    // Initialize heap on first allocation
    if (free_list == nullptr) {
        init();
    }
    
    // Align size to 4 bytes
    size = (size + 3) & ~3;
    
    // First-fit allocation strategy
    MemoryBlock* block = free_list;
    MemoryBlock* prev = nullptr;
    
    while (block != nullptr) {
        if (!block->used && block->size >= size) {
            // Found a suitable block
            
            // Split block if it's big enough to create a new free block
            if (block->size >= size + sizeof(MemoryBlock) + 4) {
                MemoryBlock* new_block = reinterpret_cast<MemoryBlock*>(
                    reinterpret_cast<uint8_t*>(block) + sizeof(MemoryBlock) + size
                );
                new_block->size = block->size - size - sizeof(MemoryBlock);
                new_block->used = false;
                new_block->next = block->next;
                
                block->size = size;
                block->next = new_block;
            }
            
            block->used = true;
            
            // Return pointer to the memory after the block header
            return reinterpret_cast<uint8_t*>(block) + sizeof(MemoryBlock);
        }
        
        prev = block;
        block = block->next;
    }
    
    // If we get here, memory allocation failed
    return nullptr;
}

void KernelHeap::deallocate(void* ptr) {
    if (ptr == nullptr) {
        return;
    }
    
    // Get the block header from the pointer
    MemoryBlock* block = reinterpret_cast<MemoryBlock*>(
        reinterpret_cast<uint8_t*>(ptr) - sizeof(MemoryBlock)
    );
    
    // Mark the block as free
    block->used = false;
    
    // Coalesce with adjacent free blocks
    // This is a simple implementation - could be optimized
    MemoryBlock* current = free_list;
    
    while (current != nullptr) {
        if (!current->used) {
            // Check if the next block is also free and adjacent
            MemoryBlock* next = current->next;
            while (next != nullptr && !next->used && 
                   reinterpret_cast<uint8_t*>(current) + sizeof(MemoryBlock) + current->size == 
                   reinterpret_cast<uint8_t*>(next)) {
                // Merge the blocks
                current->size += sizeof(MemoryBlock) + next->size;
                current->next = next->next;
                next = current->next;
            }
        }
        current = current->next;
    }
}

void* KernelHeap::reallocate(void* ptr, size_t size) {
    if (ptr == nullptr) {
        // If ptr is NULL, equivalent to malloc
        return allocate(size);
    }
    
    if (size == 0) {
        // If size is 0, equivalent to free
        deallocate(ptr);
        return nullptr;
    }
    
    // Get the block header from the pointer
    MemoryBlock* block = reinterpret_cast<MemoryBlock*>(
        reinterpret_cast<uint8_t*>(ptr) - sizeof(MemoryBlock)
    );
    
    // If the current block size is sufficient, just return the same pointer
    if (block->size >= size) {
        return ptr;
    }
    
    // Allocate a new block
    void* new_ptr = allocate(size);
    if (new_ptr == nullptr) {
        return nullptr;  // Allocation failed
    }
    
    // Copy the old data to the new block
    memcpy(new_ptr, ptr, block->size);
    
    // Free the old block
    deallocate(ptr);
    
    return new_ptr;
}

// C and C++ memory management hooks
void* malloc(size_t size) {
    return KernelHeap::allocate(size);
}

void free(void* ptr) {
    KernelHeap::deallocate(ptr);
}

void* calloc(size_t num, size_t size) {
    size_t total_size = num * size;
    void* ptr = KernelHeap::allocate(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    return KernelHeap::reallocate(ptr, size);
}

// C++ operators
void* operator new(size_t size) {
    return KernelHeap::allocate(size);
}

void* operator new[](size_t size) {
    return KernelHeap::allocate(size);
}

void operator delete(void* ptr) noexcept {
    KernelHeap::deallocate(ptr);
}

void operator delete[](void* ptr) noexcept {
    KernelHeap::deallocate(ptr);
}