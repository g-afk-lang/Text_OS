#ifndef IO_PORT_H
#define IO_PORT_H

#include "types.h"

static inline void outb(uint16_t port, uint8_t value);
static inline uint8_t inb(uint16_t port);
static inline void outw(uint16_t port, uint16_t value);
static inline uint16_t inw(uint16_t port);
static inline void outl(uint16_t port, uint32_t value);
static inline uint32_t inl(uint16_t port);

#endif