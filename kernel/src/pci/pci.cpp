#include "pci.hpp"
#include <mem/mem.hpp>
#include <arch/arch.hpp>
#include <cstdio>
#include <config.hpp>

pci_device* pci_dev_head = nullptr;
pci_device* pci_dev_tail = nullptr;
uint64_t n_detected = 0;

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

uint8_t pci_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint32_t offset) {
    uint32_t aligned_offset = offset & 0xFC;
    uint32_t shift = (offset & 3) * 8;
    arch::x86_64::io::outl(PCI_CONFIG_ADDRESS, (1U << 31) | (bus << 16) | (device << 11) | (function << 8) | aligned_offset);
    uint32_t data = arch::x86_64::io::inl(PCI_CONFIG_DATA);
    return (data >> shift) & 0xFF;
}

uint16_t pci_read_word(uint8_t bus, uint8_t device, uint8_t function, uint32_t offset) {
    uint32_t aligned_offset = offset & 0xFC;
    uint32_t shift = (offset & 2) * 8;
    arch::x86_64::io::outl(PCI_CONFIG_ADDRESS, (1U << 31) | (bus << 16) | (device << 11) | (function << 8) | aligned_offset);
    uint32_t data = arch::x86_64::io::inl(PCI_CONFIG_DATA);
    return (data >> shift) & 0xFFFF;
}

uint32_t pci_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint32_t offset) {
    arch::x86_64::io::outl(PCI_CONFIG_ADDRESS, (1U << 31) | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC));
    return arch::x86_64::io::inl(PCI_CONFIG_DATA);
}

pci_device* pci_enumerate_helper(uint8_t bus, uint8_t device, uint8_t function) {
    pci_device* dev = (pci_device*)mem::heap::malloc(sizeof(pci_device));
    if (!dev) return nullptr;

    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->next = nullptr;

    dev->vendor_id = pci_read_word(bus, device, function, 0x00);
    dev->device_id = pci_read_word(bus, device, function, 0x02);

    dev->command = pci_read_word(bus, device, function, 0x04);
    dev->status = pci_read_word(bus, device, function, 0x06);

    dev->revision_id = pci_read_byte(bus, device, function, 0x08);
    dev->prog_if = pci_read_byte(bus, device, function, 0x09);
    dev->subclass = pci_read_byte(bus, device, function, 0x0A);
    dev->class_code = pci_read_byte(bus, device, function, 0x0B);

    dev->cache_line_size = pci_read_byte(bus, device, function, 0x0C);
    dev->latency_timer = pci_read_byte(bus, device, function, 0x0D);
    dev->header_type = pci_read_byte(bus, device, function, 0x0E);
    dev->bist = pci_read_byte(bus, device, function, 0x0F);

    for (int i = 0; i < 6; ++i) {
        dev->bars[i] = pci_read_dword(bus, device, function, 0x10 + i * 4);
    }

    dev->interrupt_line = pci_read_byte(bus, device, function, 0x3C);
    dev->interrupt_pin = pci_read_byte(bus, device, function, 0x3D);
    dev->min_grant = pci_read_byte(bus, device, function, 0x3E);
    dev->max_latency = pci_read_byte(bus, device, function, 0x3F);

    return dev;
}

bool pci_add_device(pci_device* dev) {
    if (!dev) return false;

    if (!pci_dev_head) {
        pci_dev_head = dev;
        pci_dev_tail = dev;
    } else {
        pci_dev_tail->next = dev;
        pci_dev_tail = dev;
    }

    if (dev->vendor_id == 0xFFFF) return false;
    n_detected++;
    return true;
}

const char* get_type(pci_device* dev) {
    uint8_t class_code = dev->class_code;
    uint8_t subclass   = dev->subclass;

    switch (class_code) {
        case 0x01: // Mass Storage Controller
            switch (subclass) {
                case 0x01: return "IDE Controller";
                case 0x02: return "Floppy Controller";
                case 0x03: return "SCSI Controller";
                case 0x05: return "ATA Controller";
                case 0x06: return "Serial ATA (AHCI)";
                case 0x07: return "Serial Attached SCSI (SAS)";
                case 0x80: return "Other Mass Storage";
                default:   return "Unknown Mass Storage";
            }
        case 0x02: // Network Controller
            switch (subclass) {
                case 0x00: return "Ethernet Controller";
                case 0x01: return "Token Ring Controller";
                case 0x02: return "FDDI Controller";
                case 0x03: return "ATM Controller";
                case 0x80: return "Other Network";
                default:   return "Unknown Network";
            }
        case 0x03: // Display Controller
            switch (subclass) {
                case 0x00: return "VGA Compatible Controller";
                case 0x01: return "XGA Controller";
                case 0x80: return "Other Display Controller";
                default:   return "Unknown Display";
            }
        case 0x06: // Bridge Device
            switch (subclass) {
                case 0x00: return "Host Bridge";
                case 0x01: return "ISA Bridge";
                case 0x04: return "PCI-to-PCI Bridge";
                default:   return "Unknown Bridge";
            }
        case 0x0C: // Serial Bus Controller
            switch (subclass) {
                case 0x03: return "USB Controller";
                default:   return "Other Serial Bus Controller";
            }
        default:
            return "Unknown PCI Device";
    }
}

namespace pci {

uint64_t initialise() {
    for (uint16_t bus = 0; bus < 8; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                pci_device* dev = pci_enumerate_helper(bus, device, function);
                bool found = pci_add_device(dev);
                if (found) {
#ifdef CONFIG_PCI_VERBOSE
                    printf("Enumerated device %d:%d.%d [%s] %s (%02x / %02x / %02x) [%04X:%04X]\n\r", bus, device, function, found ? "Device present" : "Empty slot", get_type(dev), dev->class_code, dev->subclass, dev->prog_if, dev->vendor_id, dev->device_id);
                    printf("BARs: %8X %8X %8X %8X %8X %8X\n\r", dev->bars[0], dev->bars[1], dev->bars[2], dev->bars[3], dev->bars[4], dev->bars[5]);
#endif
                }
            }
        }
    }

    return n_detected;
}

pci_device* get_device(const pci_query& query) {
    pci_device* curr = pci_dev_head;
    while (curr) {
        bool match = true;

        if (query.type & GET_VIA_VENDOR_ID_DEVICE_ID) {
            if (curr->vendor_id != query.vendor_id || curr->device_id != query.device_id)
                match = false;
        }

        if (query.type & GET_VIA_CLASS_CODE_SUBCLASS_CODE) {
            if (curr->class_code != query.class_code || curr->subclass != query.subclass_code)
                match = false;

            if ((query.type & GET_CHECK_PROG_IF) && match) {
                if (curr->prog_if != query.prog_if)
                    match = false;
            }
        }

        if (query.type & GET_VIA_BUS_DEVICE_FUNCTION_ADDRESS) {
            if (curr->bus != query.bus || curr->device != query.device || curr->function != query.function)
                match = false;
        }

        if (match) return curr;

        curr = curr->next;
    }
    return nullptr;
}

pci_device* get_device_bdf(uint8_t bus, uint8_t device, uint8_t function) {
    pci_device* curr = pci_dev_head;
    while (curr) {
        if (curr->bus == bus && curr->device == device && curr->function == function)
            return curr;
        curr = curr->next;
    }
    return nullptr;
}

pci_device* get_device_class_code(uint8_t class_code, uint8_t subclass, bool check_progif, uint8_t prog_if) {
    pci_device* curr = pci_dev_head;
    while (curr) {
        bool match = true;
        if (curr->class_code != class_code) match = false;
        if (curr->subclass != subclass) match = false;
        if (check_progif && curr->prog_if != prog_if) match = false;

        if (match) return curr;
        curr = curr->next;
    }
    return nullptr;
}

pci_device* get_device_vendor_id(uint16_t vendor_id, uint16_t device_id) {
    pci_device* curr = pci_dev_head;
    while (curr) {
        if (curr->vendor_id == vendor_id && curr->device_id == device_id)
            return curr;
        curr = curr->next;
    }
    return nullptr;
}

}
