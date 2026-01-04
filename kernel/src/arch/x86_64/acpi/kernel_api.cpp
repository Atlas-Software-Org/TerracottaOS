#include <uacpi/kernel_api.h>
#include <limine.h>
#include <mem/mem.hpp>
#include <cstdio>
#include <cstdarg>
#include <arch/arch.hpp>
#include <drivers/timers/pit.hpp>
#include <panic.hpp>
#include <cstdint>

extern "C" {

__attribute__((section(".limine_requests")))
volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
};

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
    if (!rsdp_request.response || !rsdp_request.response->address) panic((char*)"K_NO_RSDP");
    *out_rsdp_address = reinterpret_cast<uacpi_phys_addr>(rsdp_request.response->address);

    return UACPI_STATUS_OK;
}

void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    uint64_t virt_addr = mem::vmm::pa_to_va((uint64_t)addr);
    
    if (!mem::vmm::is_mapped((void*)(virt_addr & ~0xFFF))) {
        void* phys_aligned = (void*)((uint64_t)addr & ~(0xFFF));
        void* virt_aligned = (void*)(virt_addr & ~(0xFFF));
        uint64_t pages = (len + 0xFFF) / 0x1000;
        mem::vmm::mmap(phys_aligned, virt_aligned, pages, PAGE_PRESENT | PAGE_RW);
    }
    
    return (void*)virt_addr;
}

void uacpi_kernel_unmap(void *addr, uacpi_size len) {
    void* _addr = (void*)((uint64_t)addr & ~(0xFFF));
    uint64_t pages = (len + 0xFFF) / 0x1000;

    mem::vmm::munmap(_addr, pages);
}

#include <config.hpp>
#ifndef UACPI_FORMATTED_LOGGING
void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char* s) {
    (void)lvl;
#ifdef CONFIG_ACPI_VERBOSE
    printf("[ \x1b[95mUACPI\x1b[0m ] %s", s);
#endif
}
#else
void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char* s, ...) {
#ifdef CONFIG_ACPI_VERBOSE
    printf("[ UACPI ] ");
    va_list va;
    va_start(va, s);
    vprintf(s, va);
    va_end(va);
#endif
}

void uacpi_kernel_vlog(uacpi_log_level lvl, const uacpi_char* s, uacpi_va_list va) {
    printf("[ UACPI ] ");
    vprintf(s, va);
}
#endif

uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl) {(void)current_init_lvl;return UACPI_STATUS_OK;}
void uacpi_kernel_deinitialize(void) {}

struct pci_dev {
    uint8_t b, d, f;
};

uacpi_status uacpi_kernel_pci_device_open(
    uacpi_pci_address address, uacpi_handle *out_handle
) {
    pci_dev* dev = (pci_dev*)mem::heap::malloc(sizeof(pci_dev));
    if (!dev) return UACPI_STATUS_OUT_OF_MEMORY;
    *out_handle = dev;

    dev->b = address.bus;
    dev->d = address.device;
    dev->f = address.function;

    return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    mem::heap::free(handle);
}

uint32_t uacpi_pci_read_helper(int sz_order, uint8_t bus, uint8_t device, uint8_t func, uacpi_size offset) {
    using namespace arch::x86_64::io;

    uint32_t address = 0x80000000 | ((bus & 0xFF) << 16) | ((device & 0x1F) << 11) |
                       ((func & 0x07) << 8) | (offset & 0xFC);

    outl(0xCF8, address);
    uint32_t data = inl(0xCFC);

    switch (sz_order) {
        case 0:
            return (data >> ((offset & 3) * 8)) & 0xFF;
        case 1:
            return (data >> ((offset & 2) * 8)) & 0xFFFF;
        case 2:
            return data;
        default:
            return 0;
    }
}

void uacpi_pci_write_helper(int sz_order, uint8_t bus, uint8_t device, uint8_t func, uacpi_size offset, uint32_t value) {
    using namespace arch::x86_64::io;

    uint32_t address = 0x80000000 | ((bus & 0xFF) << 16) | ((device & 0x1F) << 11) |
                       ((func & 0x07) << 8) | (offset & 0xFC);

    outl(0xCF8, address);
    uint32_t data = inl(0xCFC);

    switch (sz_order) {
        case 0:
            data &= ~(0xFF << ((offset & 3) * 8));
            data |= (value & 0xFF) << ((offset & 3) * 8);
            break;
        case 1:
            data &= ~(0xFFFF << ((offset & 2) * 8));
            data |= (value & 0xFFFF) << ((offset & 2) * 8);
            break;
        case 2:
            data = value;
            break;
        default:
            return;
    }

    outl(0xCFC, data);
}

uacpi_status uacpi_kernel_pci_read8(
    uacpi_handle device, uacpi_size offset, uacpi_u8 *value
) {
    pci_dev* dev = (pci_dev*)device;

    *value = uacpi_pci_read_helper(0, dev->b, dev->d, dev->f, offset);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(
    uacpi_handle device, uacpi_size offset, uacpi_u16 *value
) {
    pci_dev* dev = (pci_dev*)device;

    *value = uacpi_pci_read_helper(1, dev->b, dev->d, dev->f, offset);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(
    uacpi_handle device, uacpi_size offset, uacpi_u32 *value
) {
    pci_dev* dev = (pci_dev*)device;

    *value = uacpi_pci_read_helper(2, dev->b, dev->d, dev->f, offset);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(
    uacpi_handle device, uacpi_size offset, uacpi_u8 value
) {
    pci_dev* dev = (pci_dev*)device;

    uacpi_pci_write_helper(0, dev->b, dev->d, dev->f, offset, value);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(
    uacpi_handle device, uacpi_size offset, uacpi_u16 value
) {
    pci_dev* dev = (pci_dev*)device;

    uacpi_pci_write_helper(1, dev->b, dev->d, dev->f, offset, value);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(
    uacpi_handle device, uacpi_size offset, uacpi_u32 value
) {
    pci_dev* dev = (pci_dev*)device;

    uacpi_pci_write_helper(2, dev->b, dev->d, dev->f, offset, value);

    return UACPI_STATUS_OK;
}

struct io_entry {
    uacpi_io_addr base;
    uacpi_size len;
};

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle) {
    io_entry* e = (io_entry*)mem::heap::malloc(sizeof(io_entry));
    if (!e) return UACPI_STATUS_OUT_OF_MEMORY;
    e->base = base;
    e->len = len;
    *out_handle = e;

    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
    mem::heap::free(handle);
}

uacpi_status uacpi_kernel_io_read8(
    uacpi_handle handle, uacpi_size offset, uacpi_u8 *out_value
) {
    io_entry* e = (io_entry*)handle;

    if (offset > e->len) panic((char*)"K_IO_OUT_OF_BOUND");

    *out_value = arch::x86_64::io::inb(e->base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(
    uacpi_handle handle, uacpi_size offset, uacpi_u16 *out_value
) {
    io_entry* e = (io_entry*)handle;

    if (offset > e->len) panic((char*)"K_IO_OUT_OF_BOUND");

    *out_value = arch::x86_64::io::inw(e->base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(
    uacpi_handle handle, uacpi_size offset, uacpi_u32 *out_value
) {
    io_entry* e = (io_entry*)handle;

    if (offset > e->len) panic((char*)"K_IO_OUT_OF_BOUND");

    *out_value = arch::x86_64::io::inl(e->base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(
    uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value
) {
    io_entry* e = (io_entry*)handle;

    if (offset > e->len) panic((char*)"K_IO_OUT_OF_BOUND");

    arch::x86_64::io::outb(e->base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(
    uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value
) {
    io_entry* e = (io_entry*)handle;

    if (offset > e->len) panic((char*)"K_IO_OUT_OF_BOUND");

    arch::x86_64::io::outw(e->base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(
    uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value
) {
    io_entry* e = (io_entry*)handle;

    if (offset > e->len) panic((char*)"K_IO_OUT_OF_BOUND");

    arch::x86_64::io::outl(e->base + offset, in_value);
    return UACPI_STATUS_OK;
}

void* uacpi_kernel_alloc(uacpi_size size) {
    void* ptr = mem::heap::malloc(size);
    return ptr;
}

#ifdef UACPI_NATIVE_ALLOC_ZEROED
void* uacpi_kernel_alloc_zeroed(uacpi_size size) {
    void* ptr = mem::heap::calloc(1, size);
    return ptr;
}
#endif

#ifndef UACPI_SIZED_FREES
void uacpi_kernel_free(void* mem) {
    mem::heap::free(mem);
}
#else
void uacpi_kernel_free(void* mem, uacpi_size size_hint) {
    mem::heap::free(mem);
}
#endif

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    uint64_t ns = drivers::timers::pit::ns_elapsed_time();
    return ns;
}

void uacpi_kernel_stall(uacpi_u8 usec) {
    if (usec == 0) return;
    drivers::timers::pit::sleep_ms(usec / 1000);
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
    drivers::timers::pit::sleep_ms(msec);
}

struct mutex {
    bool locked;
};

uacpi_handle uacpi_kernel_create_mutex(void) {
    mutex* m = (mutex*)mem::heap::malloc(sizeof(mutex));
    if (!m) return nullptr;
    m->locked = false;

    return m;
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
    mem::heap::free(handle);
}

struct event {
    bool signaled;
};

uacpi_handle uacpi_kernel_create_event(void) {
    event* e = (event*)mem::heap::malloc(sizeof(event));
    if (!e) return nullptr;
    e->signaled = false;

    return e;
}

void uacpi_kernel_free_event(uacpi_handle handle) {
    mem::heap::free(handle);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    return (uacpi_thread_id)1;
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout) {
    uint64_t current_nsec = drivers::timers::pit::ns_elapsed_time();
    uint64_t target_nsec = current_nsec + (timeout * 1000);
    bool op_successful = false;
    if (timeout == 0x0000) {
        mutex* e = (mutex*)handle;
        if (e->locked == true) return UACPI_STATUS_TIMEOUT;
        else return UACPI_STATUS_OK;
    } else if (timeout == 0xFFFF) {
        target_nsec = (uint64_t)-1;
    }
    while (drivers::timers::pit::ns_elapsed_time() < target_nsec) {
        mutex* e = (mutex*)handle;
        if (e->locked == true) continue;
        else {
            op_successful = true;
            break;
        }
    }

    if (!op_successful) return UACPI_STATUS_TIMEOUT;

    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
    mutex* m = (mutex*)handle;
    m->locked = false;
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
    uint64_t current_nsec = drivers::timers::pit::ns_elapsed_time();
    uint64_t target_nsec = current_nsec + (timeout * 1000);
    bool op_successful = false;
    if (timeout == 0x0000) {
        event* e = (event*)handle;
        if (e->signaled == true) return false;
        else return true;
    } else if (timeout == 0xFFFF) {
        target_nsec = (uint64_t)-1;
    }
    while (drivers::timers::pit::ns_elapsed_time() < target_nsec) {
        event* e = (event*)handle;
        if (e->signaled == false) continue;
        else {
            op_successful = true;
            break;
        }
    }

    if (!op_successful) return false;

    return true;
}

void uacpi_kernel_signal_event(uacpi_handle handle) {
    event* e = (event*)handle;
    e->signaled = true;
}

void uacpi_kernel_reset_event(uacpi_handle handle) {
    event* e = (event*)handle;
    e->signaled = false;
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request* request) {
    (void)request;
    return UACPI_STATUS_OK;
}

struct interrupt {
    uint8_t vector;
    uint8_t irq;
    bool is_irq;
    uacpi_handle ctx;
};

uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx,
    uacpi_handle *out_irq_handle
) {
    uint8_t vector = irq + 0x20;

    interrupt *i = (interrupt*)mem::heap::malloc(sizeof(interrupt));
    if (!i) return UACPI_STATUS_OUT_OF_MEMORY;
    i->vector = vector;
    i->irq = irq;
    i->is_irq = true;
    i->ctx = ctx;

    *out_irq_handle = i;

    arch::x86_64::cpu::idt::set_descriptor(vector, (uint64_t)handler, 0x8E);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(
    uacpi_interrupt_handler unused, uacpi_handle irq_handle
) {
    (void)unused;
    interrupt* i = (interrupt*)irq_handle;
    arch::x86_64::cpu::idt::clear_descriptor(i->vector);

    mem::heap::free(irq_handle);
    return UACPI_STATUS_OK;
}

struct spinlock {
    bool locked;
};

uacpi_handle uacpi_kernel_create_spinlock(void) {
    spinlock* s = (spinlock*)mem::heap::malloc(sizeof(spinlock));
    if (!s) return nullptr;
    s->locked = false;

    return s;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
    mem::heap::free(handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
    spinlock* s = (spinlock*)handle;
    while (s->locked == true);
    s->locked = true;

    return 0;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
    (void)flags;
    spinlock* s = (spinlock*)handle;
    s->locked = false;
}

struct work {
    bool signaled;
    uacpi_work_type type;
    uacpi_work_handler handler;
};

uacpi_status uacpi_kernel_schedule_work(
    uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx
) {
    (void)ctx;
    work* w = (work*)mem::heap::malloc(sizeof(work));
    if (!w) return UACPI_STATUS_OUT_OF_MEMORY;
    w->signaled = false;
    w->type = type;
    w->handler = handler;

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    return UACPI_STATUS_OK;
}

}
