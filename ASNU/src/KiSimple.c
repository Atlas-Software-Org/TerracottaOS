#include <KiSimple.h>
#include <stdint.h>

void outb(uint16_t port, uint8_t byte) {
    asm volatile ("outb %0, %1" : : "a"(byte), "dN"(port));
}
uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile ("inb %1, %0" : "=a"(result) : "dN"(port));
    return result;
}
void outw(uint16_t port, uint16_t word) {
    asm volatile ("outw %0, %1" : : "a"(word), "dN"(port));
}
uint16_t inw(uint16_t port) {
    uint16_t result;
    asm volatile ("inw %1, %0" : "=a"(result) : "dN"(port));
    return result;
}
void outl(uint16_t port, uint32_t dword) {
    asm volatile ("outl %0, %1" : : "a"(dword), "dN"(port));
}
uint32_t inl(uint16_t port) {
    uint32_t result;
    asm volatile ("inl %1, %0" : "=a"(result) : "dN"(port));
    return result;
}

void IOWait() {
    outb(0x80, 0x00);
}

inline void insw(uint16_t port, void* addr, int count) {
    asm volatile ("rep insw"
                  : "+D"(addr), "+c"(count) // Destination address and counter
                  : "d"(port)               // Source port
                  : "memory");              // Clobbers memory
}

inline void outsw(uint16_t port, const void* addr, int count) {
    asm volatile ("rep outsw"
                  : "+S"(addr), "+c"(count) // Source address and counter
                  : "d"(port));             // Destination port
}

static void halt_catch_fire_x86() {
    while (1) {asm volatile ("hlt");}
}

void KiPanic(const char* __restrict string, int _halt) {
	if (_halt == 1) {
    	printk("\x1b[1;91m{ PANIC }\t%s", string);
    } else {
    	printk("{ PANIC }\t%s", string);
    }
    
    printk("\n");
    if (_halt) halt_catch_fire_x86();
    else return;
}

void DisplaySplash(int w, int h, char* text) {
	printk("\x1b[0;0HASNU 1.0.0-rc01");

    int lf_count = 0;
    int i = 0;
    char *line_start = text;
    char *line_end = NULL;
    int line_len = 0;
    int line_num = 0;

    while (text[i]) {
        if (text[i] == '\n') lf_count++;
        i++;
    }

    int total_lines = lf_count + 1;
    int center_y = h / 2 - (total_lines / 2);

    char info[256] = {0};
    int info_len = 0;
    bool info_found = false;

    line_start = text;
    line_num = 0;
    while (line_start && *line_start) {
        line_end = strchr(line_start, '\n');
        if (!line_end) line_end = line_start + strlen(line_start);

        line_len = (int)(line_end - line_start);

        // Check if line ends with $INF< sequence after newline
        if (line_len >= 4 && line_start[0] == '$' &&
            line_start[1] == 'I' && line_start[2] == 'N' && line_start[3] == 'F' &&
            line_start[4] == '<') {
            // This line itself is info start, treat as info (unlikely standalone)
            info_found = true;
            info_len = line_len - 5;
            if (info_len > 0 && info_len < (int)sizeof(info)) {
                memcpy(info, line_start + 5, info_len);
                info[info_len] = '\0';
            }
            break;
        }

        // If newline found, check if next line starts with $INF<
        if (*line_end == '\n') {
            char *next_line = line_end + 1;
            if (next_line[0] == '$' && next_line[1] == 'I' && next_line[2] == 'N' &&
                next_line[3] == 'F' && next_line[4] == '<') {
                info_found = true;
                char *info_start = next_line + 5;
                char *info_end = strstr(info_start, ">INF$");
                if (info_end && info_end > info_start) {
                    info_len = (int)(info_end - info_start);
                    if (info_len > 0 && info_len < (int)sizeof(info)) {
                        memcpy(info, info_start, info_len);
                        info[info_len] = '\0';
                    }
                }
                // Print current line only (not the $INF line)
                int center_x = w / 2 - (line_len / 2);
                if (center_x < 0) center_x = 0;
                printk("\x1b[%d;%dH", center_y + line_num + 1, center_x + 1);
                for (i = 0; i < line_len; i++) printk("%c", line_start[i]);
                line_num++;
                break;
            }
        }

        int center_x = w / 2 - (line_len / 2);
        if (center_x < 0) center_x = 0;

        printk("\x1b[%d;%dH", center_y + line_num + 1, center_x + 1);

        for (i = 0; i < line_len; i++) {
            printk("%c", line_start[i]);
        }

        line_num++;

        if (*line_end == '\n') line_start = line_end + 1;
        else break;
    }

    if (info_found) {
        // One line padding before info
        printk("\x1b[%d;1H", center_y + line_num + 2);

        int info_len_local = (int)strlen(info);
        int center_x = w / 2 - (info_len_local / 2);
        if (center_x < 0) center_x = 0;
        printk("\x1b[%d;%dH", center_y + line_num + 2, center_x + 1);
        printk("\x1b[90m%s\x1b[0m", info);
    }
}
