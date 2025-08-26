#ifndef STDLIB_HOOKS_H
#define STDLIB_HOOKS_H

#include <cstddef>
#include <cstdint>

// Memory operations
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
void* memset(void* str, int c, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

// String operations
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);
size_t strlen(const char* str);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
bool string_compare(const char* s1, const char* s2);

// Standard I/O operations
int printf(const char* format, ...);
int sprintf(char* str, const char* format, ...);
int snprintf(char* str, size_t size, const char* format, ...);

// Kernel heap management
class KernelHeap {
public:
    static const size_t HEAP_SIZE = 65536;  // 64 KB heap
    
    static void init();
    static void* allocate(size_t size);
    static void deallocate(void* ptr);
    static void* reallocate(void* ptr, size_t size);
    
private:
    struct MemoryBlock {
        size_t size;
        bool used;
        MemoryBlock* next;
    };
    
    static uint8_t heap_space[HEAP_SIZE];
    static MemoryBlock* free_list;
};

// C and C++ memory management hooks
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t num, size_t size);
void* realloc(void* ptr, size_t size);

// C++ operators
void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;

#endif // STDLIB_HOOKS_H
