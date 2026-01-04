#include "pcie.hpp"
#include <uacpi/uacpi.h>
#include <uacpi/tables.h>
#include <uacpi/types.h>
#include <mem/mem.hpp>
#include <cstdio>
#include <cstdint>
#include <config.hpp>

struct mcfg_allocation {
    uint64_t base_address;
    uint16_t segment_group;
    uint8_t start_bus;
    uint8_t end_bus;
    uint32_t reserved;
} __attribute__((packed));

struct mcfg_table {
    struct acpi_sdt_hdr {
        char signature[4];
        uint32_t length;
        uint8_t revision;
        uint8_t checksum;
        char oemid[6];
        char oem_table_id[8];
        uint32_t oem_revision;
        uint32_t creator_id;
        uint32_t creator_revision;
    } header;
    uint64_t reserved;
    mcfg_allocation allocations[];
} __attribute__((packed));

namespace pcie {

uacpi_table mcfg_handle;
mcfg_table* mcfg;
uint32_t num_allocations = 0;
uint32_t total_devices = 0;

pcie_device* first_device = nullptr;
uint64_t num_devs = 0;

void install_pcie_device(pcie_device* dev) {
    dev->next = first_device;
    first_device = dev;
    num_devs++;
    total_devices++;
}

static void* get_config_space(uint16_t segment, uint8_t bus, 
                              uint8_t device, uint8_t function) {
    for (uint32_t i = 0; i < num_allocations; i++) {
        uint64_t base = mcfg->allocations[i].base_address;
        
        if (base == 0) continue;
        
        if (mcfg->allocations[i].segment_group == segment &&
            bus >= mcfg->allocations[i].start_bus &&
            bus <= mcfg->allocations[i].end_bus) {
            
            uint64_t offset = ((uint64_t)(bus - mcfg->allocations[i].start_bus) << 20) |
                             ((uint64_t)device << 15) |
                             ((uint64_t)function << 12);
            
            uint64_t phys_addr = base + offset;
            return (void*)mem::vmm::pa_to_va(phys_addr);
        }
    }
    return nullptr;
}

void enumerate_devices() {
    for (uint32_t alloc = 0; alloc < num_allocations; alloc++) {
        uint16_t segment = mcfg->allocations[alloc].segment_group;
        uint8_t start_bus = mcfg->allocations[alloc].start_bus;
        uint8_t end_bus = mcfg->allocations[alloc].end_bus;
        uint64_t base = mcfg->allocations[alloc].base_address;
        
        if (base == 0) continue;
        
        for (uint16_t bus = start_bus; bus <= end_bus; bus++) {
            for (uint8_t device = 0; device < 32; device++) {
                for (uint8_t function = 0; function < 8; function++) {
                    void* config = get_config_space(segment, bus, device, function);
                    if (!config) continue;
                    
                    uint16_t vendor_id = *(volatile uint16_t*)config;
                    
                    if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
                        if (function == 0) break;
                        continue;
                    }
                    
                    pcie_device* dev = (pcie_device*)mem::heap::malloc(sizeof(pcie_device));
                    dev->segment = segment;
                    dev->bus = bus;
                    dev->device = device;
                    dev->function = function;
                    dev->config_space = config;
                    dev->vendor_id = vendor_id;
                    dev->device_id = *((volatile uint16_t*)config + 1);
                    dev->class_code = *((volatile uint8_t*)config + 0x0B);
                    dev->subclass = *((volatile uint8_t*)config + 0x0A);
                    dev->prog_if = *((volatile uint8_t*)config + 0x09);
                    dev->revision_id = *((volatile uint8_t*)config + 0x08);
                    dev->header_type = *((volatile uint8_t*)config + 0x0E) & 0x7F;
                    
                    for (int i = 0; i < 6; i++) {
                        dev->bars[i] = *((volatile uint32_t*)config + 4 + i);
                    }
                    
                    install_pcie_device(dev);
                    
                    if (function == 0) {
                        uint8_t hdr = *((volatile uint8_t*)config + 0x0E);
                        if (!(hdr & 0x80)) break;
                    }
                }
            }
        }
    }
}

uint32_t get_num_devices() {
    return num_devs;
}

uint64_t initialise() {
    uacpi_status status = uacpi_table_find_by_signature("MCFG", &mcfg_handle);
    if (uacpi_unlikely_error(status)) {
        Log::errf("PCIe initialise: %s\n\r", uacpi_status_to_string(status));
        return 1;
    }
    
    mcfg = (mcfg_table*)mcfg_handle.ptr;
    
    num_allocations = (mcfg->header.length - sizeof(mcfg_table)) / sizeof(mcfg_allocation);

#ifdef CONFIG_PCI_VERBOSE
    printf("MCFG table at %p, length=%u\n", mcfg, mcfg->header.length);
    printf("Got %u MCFG allocations\n\r", num_allocations);
#endif
    
    for (uint32_t i = 0; i < num_allocations; i++) {
#ifdef CONFIG_PCI_VERBOSE
        printf("  [%u] base=0x%016lx seg=%u bus=%u-%u\n",
               i, mcfg->allocations[i].base_address,
               mcfg->allocations[i].segment_group,
               mcfg->allocations[i].start_bus,
               mcfg->allocations[i].end_bus);
#endif
    }

#ifdef CONFIG_PCI_VERBOSE
    printf("Enumerating PCIe devices...\n\r");
#endif
    enumerate_devices();
#ifdef CONFIG_PCI_VERBOSE
    printf("Found %lu PCIe devices\n\r", num_devs);
#endif
    
    return total_devices;
}

void cleanup() {
    pcie_device* curr = first_device;
    while (curr) {
        pcie_device* next = curr->next;
        mem::heap::free(curr);
        curr = next;
    }
    first_device = nullptr;
    num_devs = 0;
    
    uacpi_table_unref(&mcfg_handle);
}

pcie_device* get_device_bdf(uint8_t bus, uint8_t device, uint8_t function) {
    pcie_device* curr = first_device;
    while (curr) {
        if (curr->bus == bus && curr->device == device && curr->function == function) {
            return curr;
        }
        curr = curr->next;
    }
    return nullptr;
}

pcie_device* get_device_class_code(uint8_t class_code, uint8_t subclass, bool check_progif, uint8_t prog_if) {
    pcie_device* curr = first_device;
    while (curr) {
        if (curr->class_code == class_code && curr->subclass == subclass) {
            if (!check_progif || curr->prog_if == prog_if) {
                return curr;
            }
        }
        curr = curr->next;
    }
    return nullptr;
}

pcie_device* get_device_vendor_id(uint16_t vendor_id, uint16_t device_id) {
    pcie_device* curr = first_device;
    while (curr) {
        if (curr->vendor_id == vendor_id && curr->device_id == device_id) {
            return curr;
        }
        curr = curr->next;
    }
    return nullptr;
}

}
