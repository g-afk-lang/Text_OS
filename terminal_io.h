#ifndef TERMINAL_IO_H
#define TERMINAL_IO_H

#include "types.h"

class TerminalIO;

// Terminal IO manipulator function type
typedef TerminalIO& (*ManipulatorFunc)(TerminalIO&);

// Terminal IO class for stream-like IO
class TerminalIO {
public:
    // Output operators
    TerminalIO& operator<<(const char* str);
    TerminalIO& operator<<(char c);
    TerminalIO& operator<<(int num);
    TerminalIO& operator<<(unsigned int num);
    TerminalIO& operator<<(long num);
    TerminalIO& operator<<(unsigned long num);
    TerminalIO& operator<<(void* ptr);
    TerminalIO& operator<<(ManipulatorFunc func);
    
    // Input operators
    TerminalIO& operator>>(char* str);
    TerminalIO& operator>>(int& num);
    TerminalIO& operator>>(unsigned int& num);
};

// Terminal I/O manipulators
TerminalIO& endl(TerminalIO& stream);
TerminalIO& hex(TerminalIO& stream);
TerminalIO& dec(TerminalIO& stream);

// Global terminal I/O object
extern TerminalIO kout;

// Global formatting state
extern bool use_hex;

#endif // TERMINAL_IO_H
