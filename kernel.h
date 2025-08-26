#ifndef KERNEL_H
#define KERNEL_H

// External declarations for commands
void cmd_help();
void cmd_hello();
void cmd_clear();
void print_prog();
void print_prog2();

// Main command prompt function
void command_prompt();

// Kernel entry point
extern "C" void kernel_main();

#endif // KERNEL_H
