#include "terminal_io.h"
#include "terminal_hooks.h"
#include "stdlib_hooks.h"

// Global terminal I/O object
TerminalIO kout;

// Global formatting state
bool use_hex = false;

// Integer input function
TerminalIO& TerminalIO::operator>>(int& num) {
    char buffer[32];
    *this >> buffer;
    
    // Simple string to integer conversion
    num = 0;
    bool negative = false;
    int i = 0;
    
    // Check for negative sign
    if (buffer[0] == '-') {
        negative = true;
        i = 1;
    }
    
    // Process digits
    while (buffer[i] >= '0' && buffer[i] <= '9') {
        num = num * 10 + (buffer[i] - '0');
        i++;
    }
    
    if (negative) {
        num = -num;
    }
    
    return *this;
}

// Unsigned integer input function
TerminalIO& TerminalIO::operator>>(unsigned int& num) {
    char buffer[32];
    *this >> buffer;
    
    // Simple string to unsigned integer conversion
    num = 0;
    int i = 0;
    
    // Process digits
    while (buffer[i] >= '0' && buffer[i] <= '9') {
        num = num * 10 + (buffer[i] - '0');
        i++;
    }
    
    return *this;
}

// Stream manipulators
TerminalIO& endl(TerminalIO& stream) {
    stream << '\n';
    return stream;
}

TerminalIO& hex(TerminalIO& stream) {
    use_hex = true;
    return stream;
}

TerminalIO& dec(TerminalIO& stream) {
    use_hex = false;
    return stream;
}

// Output operators
TerminalIO& TerminalIO::operator<<(const char* str) {
    while (*str) {
        terminal_putchar(*str++);
    }
    return *this;
}

TerminalIO& TerminalIO::operator<<(char c) {
    terminal_putchar(c);
    return *this;
}

TerminalIO& TerminalIO::operator<<(int num) {
    if (use_hex) {
        // Print in hexadecimal
        char buffer[12];  // Enough for 32-bit hex
        int i = 0;
        
        // Handle 0 specially
        if (num == 0) {
            terminal_putchar('0');
            return *this;
        }
        
        // Handle negative numbers
        if (num < 0) {
            terminal_putchar('-');
            num = -num;
        }
        
        // Convert digits
        while (num > 0) {
            int digit = num % 16;
            buffer[i++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
            num /= 16;
        }
        
        // Print in reverse order
        while (--i >= 0) {
            terminal_putchar(buffer[i]);
        }
    } else {
        // Print in decimal
        char buffer[12];  // Enough for 32-bit int
        int i = 0;
        
        // Handle 0 specially
        if (num == 0) {
            terminal_putchar('0');
            return *this;
        }
        
        // Handle negative numbers
        if (num < 0) {
            terminal_putchar('-');
            num = -num;
        }
        
        // Convert digits
        while (num > 0) {
            buffer[i++] = '0' + (num % 10);
            num /= 10;
        }
        
        // Print in reverse order
        while (--i >= 0) {
            terminal_putchar(buffer[i]);
        }
    }
    
    return *this;
}

TerminalIO& TerminalIO::operator<<(unsigned int num) {
    if (use_hex) {
        // Print in hexadecimal
        char buffer[12];  // Enough for 32-bit hex
        int i = 0;
        
        // Handle 0 specially
        if (num == 0) {
            terminal_putchar('0');
            return *this;
        }
        
        // Convert digits
        while (num > 0) {
            int digit = num % 16;
            buffer[i++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
            num /= 16;
        }
        
        // Print in reverse order
        while (--i >= 0) {
            terminal_putchar(buffer[i]);
        }
    } else {
        // Print in decimal
        char buffer[12];  // Enough for 32-bit unsigned
        int i = 0;
        
        // Handle 0 specially
        if (num == 0) {
            terminal_putchar('0');
            return *this;
        }
        
        // Convert digits
        while (num > 0) {
            buffer[i++] = '0' + (num % 10);
            num /= 10;
        }
        
        // Print in reverse order
        while (--i >= 0) {
            terminal_putchar(buffer[i]);
        }
    }
    
    return *this;
}

TerminalIO& TerminalIO::operator<<(long num) {
    // Implementation similar to int version, but handles the larger size
    return operator<<(static_cast<int>(num));  // Simplified for example
}

TerminalIO& TerminalIO::operator<<(unsigned long num) {
    // Implementation similar to unsigned int version, but handles the larger size
    return operator<<(static_cast<unsigned int>(num));  // Simplified for example
}

TerminalIO& TerminalIO::operator<<(void* ptr) {
    if (ptr == nullptr) {
        *this << "(null)";
        return *this;
    }
    
    *this << "0x";
    
    // Store current format and switch to hex
    bool old_hex = use_hex;
    use_hex = true;
    
    // Print the pointer value
    *this << reinterpret_cast<unsigned int>(ptr);
    
    // Restore format
    use_hex = old_hex;
    
    return *this;
}

// Handle manipulators (like endl, hex, dec)
TerminalIO& TerminalIO::operator<<(ManipulatorFunc func) {
    return func(*this);
}

// Keyboard input function
TerminalIO& TerminalIO::operator>>(char* str) {
    // Reset the input ready flag
    input_ready = false;
    
    // Display prompt to indicate input is expected
    *this << "> ";
    
    // Wait for the keyboard handler to set input_ready flag
    // This will be set when the user presses Enter
    while (!input_ready) {
        // Use hlt instruction to wait efficiently for interrupts
        asm volatile ("hlt");
    }
    
    // Copy the input buffer to the provided string
    strcpy(str, input_buffer);
    
    // Return this object for chaining
    return *this;
}