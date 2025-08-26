
#include <cstddef>
extern "C" void* memcpy(void* dest, const void* src, size_t count) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < count; i++) {
        d[i] = s[i];
    }
    return dest;
}

// Custom implementation of memset function
extern "C" void* memset(void* dest, int value, size_t count) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    unsigned char v = static_cast<unsigned char>(value);
    
    for (size_t i = 0; i < count; i++) {
        d[i] = v;
    }
    
    return dest;
}