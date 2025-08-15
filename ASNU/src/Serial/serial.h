#ifndef SERIAL_H
#define SERIAL_H 1

#include <stdint.h>
#include <stdarg.h>
#include <KiSimple.h>

void serial_init(void);

void serial_fwrite(const char* fmt, ...);

void serial_write_char(char c);
char* serial_i2s(int value);
void serial_write_uint(unsigned int value);
void serial_write_hex(uintptr_t value, int uppercase);
void serial_write_octal(unsigned int value);

#endif /* SERIAL_H */
