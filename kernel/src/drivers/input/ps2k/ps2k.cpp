#include <config.hpp>
#include "ps2k.hpp"
#include <mem/mem.hpp>
#include <arch/arch.hpp>
#include <cstdio>
#include <tmpfs/tmpfs.hpp>
#include "ps2k_key_event.hpp"
#include "ps2k_scancode_map.hpp"
#include <errno.hpp>
#include <drivers/timers/pit/pit.hpp>

namespace drivers::input::ps2k {

#ifdef CONFIG_PS2K_ALLOC_BUF
static key_event fifo_buffer[CONFIG_PS2K_INITIAL_BUF_SIZE];
#endif

struct event_buffer {
    key_event* events;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    bool drop_events;
    event_callback_fn callback;
    void* callback_userdata;
};

static event_buffer evbuf;

static inline bool buffer_is_empty(const event_buffer& buf) {
    return buf.count == 0;
}

static inline bool buffer_is_full(const event_buffer& buf) {
    return buf.count >= buf.capacity;
}

static bool buffer_push(event_buffer& buf, const key_event& ev) {
    if (buffer_is_full(buf)) {
#ifdef CONFIG_PS2K_DEBUG
        printf("[ps2k] Warning: event buffer overflow, %s\n", buf.drop_events == true ? "dropping event" : "flushing buffer");
#endif

		if (buf.drop_events) {
			return false;
		} else {
			flush_events();
		}
    }
    
    buf.events[buf.tail] = ev;
    buf.tail = (buf.tail + 1) % buf.capacity;
    buf.count++;
    
    if (buf.callback) {
        buf.callback(ev, buf.callback_userdata);
    }
    
    return true;
}

static bool buffer_pop(event_buffer& buf, key_event& ev) {
    if (buffer_is_empty(buf)) {
        return false;
    }
    
    ev = buf.events[buf.head];
    buf.head = (buf.head + 1) % buf.capacity;
    buf.count--;
    
    return true;
}

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

#define PS2_CMD_DISABLE_PORT1  0xAD
#define PS2_CMD_ENABLE_PORT1   0xAE
#define PS2_CMD_READ_CONFIG    0x20
#define PS2_CMD_WRITE_CONFIG   0x60

#define KBD_CMD_RESET          0xFF
#define KBD_CMD_ENABLE         0xF4
#define KBD_CMD_DISABLE        0xF5
#define KBD_CMD_SET_SCANCODE   0xF0

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02

static inline bool ps2_output_ready() {
    return arch::x86_64::io::inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL;
}

static inline bool ps2_input_ready() {
    return !(arch::x86_64::io::inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL);
}

static uint8_t ps2_read_data() {
    while (!ps2_output_ready())
        ;
    return arch::x86_64::io::inb(PS2_DATA_PORT);
}

static void ps2_write_data(uint8_t data) {
    while (!ps2_input_ready())
        ;
    arch::x86_64::io::outb(PS2_DATA_PORT, data);
}

enum class scan_state {
    NORMAL,
    EXTENDED_E0,
    EXTENDED_E1
};

static scan_state current_scan_state = scan_state::NORMAL;

__attribute__((interrupt))
static void ps2k_interrupt_handler(void*) {
	if (!(arch::x86_64::io::inb(PS2_STATUS_PORT) & 0x01)) {
		arch::x86_64::cpu::idt::send_eoi(1);
		return;
	}

    uint8_t scancode = arch::x86_64::io::inb(PS2_DATA_PORT);
    
    if (scancode == 0xE0) {
        current_scan_state = scan_state::EXTENDED_E0;
        arch::x86_64::cpu::idt::send_eoi(1);
        return;
    }
    
    if (scancode == 0xE1) {
        current_scan_state = scan_state::EXTENDED_E1;
        arch::x86_64::cpu::idt::send_eoi(1);
        return;
    }
    
    bool pressed = !(scancode & 0x80);
    scancode &= 0x7F;
    
    keycode kc = KEY_NONE;
    if (current_scan_state == scan_state::EXTENDED_E0) {
        kc = extended_scancode_to_keycode[scancode];
        current_scan_state = scan_state::NORMAL;
    } else if (current_scan_state == scan_state::EXTENDED_E1) {
        current_scan_state = scan_state::NORMAL;
        arch::x86_64::cpu::idt::send_eoi(1);
        return;
    } else {
        kc = scancode_to_keycode[scancode];
    }
    
    if (kc != KEY_NONE) {
        key_event ev;
        ev.keycode = kc;
        ev.state = pressed ? key_state::PRESSED : key_state::RELEASED;
        ev.timestamp = drivers::timers::pit::ns_elapsed_time();
        
        buffer_push(evbuf, ev);
        
#ifdef CONFIG_PS2K_DEBUG
        printf("[ps2k] Key event: code=0x%04X state=%s\n",
               kc, pressed ? "PRESSED" : "RELEASED");
#endif
    }
    
    arch::x86_64::cpu::idt::send_eoi(1);
}

void user_ps2k_poll() {
	if (!(arch::x86_64::io::inb(PS2_STATUS_PORT) & 0x01)) {
		return;
	}

    uint8_t scancode = arch::x86_64::io::inb(PS2_DATA_PORT);
    
    if (scancode == 0xE0) {
        current_scan_state = scan_state::EXTENDED_E0;
        return;
    }
    
    if (scancode == 0xE1) {
        current_scan_state = scan_state::EXTENDED_E1;
        return;
    }
    
    bool pressed = !(scancode & 0x80);
    scancode &= 0x7F;
    
    keycode kc = KEY_NONE;
    if (current_scan_state == scan_state::EXTENDED_E0) {
        kc = extended_scancode_to_keycode[scancode];
        current_scan_state = scan_state::NORMAL;
    } else if (current_scan_state == scan_state::EXTENDED_E1) {
        current_scan_state = scan_state::NORMAL;
        arch::x86_64::cpu::idt::send_eoi(1);
        return;
    } else {
        kc = scancode_to_keycode[scancode];
    }
    
    if (kc != KEY_NONE) {
        key_event ev;
        ev.keycode = kc;
        ev.state = pressed ? key_state::PRESSED : key_state::RELEASED;
        ev.timestamp = drivers::timers::pit::ns_elapsed_time();
        
        buffer_push(evbuf, ev);
        
#ifdef CONFIG_PS2K_DEBUG
        printf("[ps2k] Key event: code=0x%04X state=%s\n",
               kc, pressed ? "PRESSED" : "RELEASED");
#endif
    }
}

static ssize_t ps2k_read(void* buf, size_t count) {
    if (count < sizeof(key_event)) {
        return -EINVAL;
    }
    
    size_t bytes_read = 0;
    key_event* event_buf = static_cast<key_event*>(buf);
    size_t events_requested = count / sizeof(key_event);
    
    for (size_t i = 0; i < events_requested; i++) {
        if (!buffer_pop(evbuf, event_buf[i])) {
            break;
        }
        bytes_read += sizeof(key_event);
    }
    
    return bytes_read;
}

static ssize_t ps2k_write() {
    return -ENOSYS;
}

static int ps2k_ioctl(unsigned long request, void* arg) {
    switch (request) {
        case 0x1000:
            if (arg) {
                *static_cast<size_t*>(arg) = evbuf.count;
                return 0;
            }
            return -EINVAL;
        
        case 0x1001:
            evbuf.head = evbuf.tail = evbuf.count = 0;
            return 0;
        
        default:
            return -EINVAL;
    }
}

void initialise() {
#ifdef CONFIG_PS2K_ALLOC_BUF
    evbuf.events = fifo_buffer;
    evbuf.capacity = CONFIG_PS2K_INITIAL_BUF_SIZE;
#else
    evbuf.capacity = CONFIG_PS2K_INITIAL_BUF_SIZE;
#ifdef CONFIG_PS2K_ALLOC_BUF_MALLOC
    evbuf.events = static_cast<key_event*>(
        mem::heap::malloc(evbuf.capacity * sizeof(key_event))
    );
#else
    size_t pages = (evbuf.capacity * sizeof(key_event) + 0xFFF) / 0x1000;
    evbuf.events = static_cast<key_event*>(mem::vmm::valloc(pages));
#endif
#endif

#ifdef CONFIG_PS2K_DROP_EVENTS_ON_FAILURE_OR_OVERFLOW
	evbuf.drop_events = true;
#else
	evbuf.drop_events = false;
#endif
    
    if (!evbuf.events) {
        printf("[ps2k] ERROR: Failed to allocate event buffer\n");
        return;
    }
    
    evbuf.head = 0;
    evbuf.tail = 0;
    evbuf.count = 0;
    evbuf.callback = nullptr;
    evbuf.callback_userdata = nullptr;
    
    init_scancode_map();
    
    ps2_write_data(KBD_CMD_RESET);
    ps2_read_data();
    ps2_read_data();
    
    ps2_write_data(KBD_CMD_ENABLE);
    ps2_read_data();
    
    arch::x86_64::cpu::idt::set_descriptor(
        0x21,
        reinterpret_cast<uint64_t>(ps2k_interrupt_handler),
        0x8E
    );
    
    arch::x86_64::cpu::idt::send_eoi(1);
}

bool read(key_event& ev) {
    return buffer_pop(evbuf, ev);
}

size_t read_events(key_event* events, size_t max_events) {
    size_t count = 0;
    for (size_t i = 0; i < max_events; i++) {
        if (!buffer_pop(evbuf, events[i])) {
            break;
        }
        count++;
    }
    return count;
}

size_t available_events() {
    return evbuf.count;
}

void flush_events() {
    evbuf.head = evbuf.tail = evbuf.count = 0;
}

void set_event_callback(event_callback_fn callback, void* userdata) {
    evbuf.callback = callback;
    evbuf.callback_userdata = userdata;
}

void clear_event_callback() {
    evbuf.callback = nullptr;
    evbuf.callback_userdata = nullptr;
}

}
