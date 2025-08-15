#include "serial.h"

#define COM1 0x3F8

void serial_write_char(char c) {
    outb(COM1, c);
    printk("%c", c);
}

char* serial_i2s(int value) {
    static char temp[16];
    static int last_value = 0;
    static int initialized = 0;

    if (initialized && value == last_value)
        return temp;

    last_value = value;
    initialized = 1;

    int i = 0;
    int is_negative = 0;

    if (value == 0) {
        temp[0]='0'; temp[1]='0'; temp[2]='0'; temp[3]='0'; temp[4]='\0';
        return temp;
    }

    if (value < 0) {
        is_negative = 1;
        value = -value;
    }

    while (value > 0) {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    }

    if (is_negative)
        temp[i++] = '-';

    while (i < 4) temp[i++] = '0';

    for (int j = 0; j < i / 2; j++) {
        char t = temp[j];
        temp[j] = temp[i - j - 1];
        temp[i - j - 1] = t;
    }

    temp[i] = '\0';
    return temp;
}

void serial_write_uint(unsigned int value) {
    char buf[16];
    int i = 0;

    if (value == 0) { buf[i++]='0'; buf[i++]='0'; buf[i++]='0'; buf[i++]='0'; }
    else { while(value>0){ buf[i++]='0'+(value%10); value/=10;} while(i<4) buf[i++]='0'; }

    for (int j=0;j<i/2;j++){ char t=buf[j]; buf[j]=buf[i-j-1]; buf[i-j-1]=t; }
    buf[i]='\0';

    for (int j=0; buf[j]; j++) serial_write_char(buf[j]);
}

void serial_write_hex(uintptr_t value, int uppercase) {
    char buf[16];
    int i = 0;
    const char* hex = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (value == 0) buf[i++] = '0';
    else { while (value > 0) { buf[i++] = hex[value & 0xF]; value >>= 4; } }

    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - j - 1];
        buf[i - j - 1] = t;
    }
    buf[i] = '\0';
    for (int j = 0; buf[j]; j++) serial_write_char(buf[j]);
}

void serial_write_octal(unsigned int value) {
    char buf[16];
    int i = 0;

    if (value == 0) buf[i++]='0';
    else { while(value>0){ buf[i++]='0'+(value%8); value/=8; } }

    for (int j=0; j<i/2; j++){ char t=buf[j]; buf[j]=buf[i-j-1]; buf[i-j-1]=t; }
    buf[i]='\0';
    for (int j=0; buf[j]; j++) serial_write_char(buf[j]);
}

static int serial_log_id = 0;

void serial_fwrite(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    serial_write_char('[');
    char* log_id = serial_i2s(serial_log_id++);
    for (char* p = log_id; *p; p++) serial_write_char(*p);
    serial_write_char(']');
    serial_write_char(' ');

    for (const char* p = fmt; *p; p++) {
        if (*p == '%') {
            p++;
            switch (*p) {
                case 'd': {
                    int v = va_arg(args, int);
                    char* s = serial_i2s(v);
                    for (char* q = s; *q; q++) serial_write_char(*q);
                    break;
                }
                case 'i': {
                    int v = va_arg(args, int);
                    char* s = serial_i2s(v);
                    for (char* q = s; *q; q++) serial_write_char(*q);
                    break;
                }
                case 'u': {
                    unsigned int v = va_arg(args, unsigned int);
                    serial_write_uint(v);
                    break;
                }
                case 'o': {
                    unsigned int v = va_arg(args, unsigned int);
                    serial_write_octal(v);
                    break;
                }
                case 'x': {
                    unsigned int v = va_arg(args, unsigned int);
                    serial_write_hex(v, 0);
                    break;
                }
                case 'X': {
                    unsigned int v = va_arg(args, unsigned int);
                    serial_write_hex(v, 1);
                    break;
                }
                case 'c': {
                    char v = (char) va_arg(args, int);
                    serial_write_char(v);
                    break;
                }
                case 's': {
                    char* v = va_arg(args, char*);
                    while (*v) serial_write_char(*v++);
                    break;
                }
                case 'p': {
                    void* v = va_arg(args, void*);
                    serial_write_hex((uintptr_t)v, 0);
                    break;
                }
                case 'l':
                    p++;
                    if (*p == 'l') {
                        p++;
                        switch (*p) {
                            case 'u': {
                                unsigned long long v = va_arg(args, unsigned long long);
                                char buf[32];
                                int i = 0;
                                if (v == 0) buf[i++] = '0';
                                else while (v > 0) {buf[i++] = '0' + (v % 10); v /= 10;}
                                buf[i] = '\0';
                                for(int j = 0; j < i / 2; j++) {char t = buf[j]; buf[j] = buf[i - j - 1]; buf[i - j - 1] = t;}
                                for(int j = 0; buf[j]; j++) serial_write_char(buf[j]);
                                break;
                            }
                            case 'd': {
                                long long v = va_arg(args, long long);
                                char buf[32];
                                int i = 0;
                                int neg = 0;
                                if (v < 0) {neg = 1; v =- v;}
                                if ( v==0 ) buf[i++] = '0';
                                else while (v > 0) {buf[i++] = '0' + (v % 10); v /= 10;}
                                if (neg) buf[i++] = '-';
                                buf[i] = '\0';
                                for ( int j = 0; j < i / 2; j++) {char t = buf[j]; buf[j] = buf[i - j - 1]; buf[i - j - 1] = t;}
                                for (int j = 0; buf[j]; j++) serial_write_char(buf[j]);
                                break;
                            }
                        }
                    }
                    break;
                case '%': serial_write_char('%'); break;
                default:
                    serial_write_char('%');
                    serial_write_char(*p);
                    break;
            }
        } else serial_write_char(*p);
    }

    serial_write_char('\n');
    va_end(args);
}
