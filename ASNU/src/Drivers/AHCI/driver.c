#include <KiSimple.h>
#include <PCI/pci.h>
#include <PMM/pmm.h>
#include <VMM/vmm.h>
#include <string.h>
#include "../AHCI.h"

#define FIS_TYPE_REG_H2D 0x27
#define ATA_CMD_READ_DMA  0x25
#define ATA_CMD_WRITE_DMA 0xCA

static hba_mem* hba = NULL;
static hba_port* active_port = NULL;

static void print_port_status(hba_port* port) {
    printk("PxCMD  = %08X\n\r", port->cmd);
    printk("PxTFD  = %08X\n\r", port->tfd);
    printk("PxIS   = %08X\n\r", port->is);
    printk("PxSERR = %08X\n\r", port->serr);
    printk("PxCI   = %08X\n\r", port->ci);
}

static int check_port_type(hba_port* port) {
    uint32_t ssts = port->ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;
    return det == HBA_PORT_DEV_PRESENT && ipm == HBA_PORT_IPM_ACTIVE;
}

void ahci_init(PciDevice_t* dev) {
    if (dev->class_code != AHCI_BASE_CLASS || dev->subclass != AHCI_SUBCLASS || dev->prog_if != AHCI_PROGIF)
        return;

    uintptr_t phys = dev->bar[dev->MMIOBarIndex] & ~0xF;
    uintptr_t virt = (uintptr_t)PA2VA(phys);
    uintptr_t size = dev->MMIOSize;
    for (uintptr_t offset = 0; offset < size; offset += 0x1000)
        mmap((void*)(virt + offset), (void*)(virt + offset), PAGE_PRESENT | PAGE_RW);

    hba = (hba_mem*)virt;

    uint32_t pi = hba->pi;
    for (int i = 0; i < 32; i++) {
        if ((pi & (1 << i)) && check_port_type(&hba->ports[i])) {
            active_port = &hba->ports[i];
            break;
        }
    }
}

static void stop_cmd(hba_port* port) {
    port->cmd &= ~HBA_PXCMD_ST;
    port->cmd &= ~HBA_PXCMD_FRE;
    while (port->cmd & (HBA_PXCMD_FR | HBA_PXCMD_CR));
}

static void start_cmd(hba_port* port) {
    while (port->cmd & HBA_PXCMD_CR);
    port->cmd |= HBA_PXCMD_FRE | HBA_PXCMD_ST;
}

static int wait_for_completion(hba_port* port, uint32_t slot_mask) {
    int timeout = 500000;
    for (int i = 0; i < timeout; i++) {
        if ((port->ci & slot_mask) == 0) return 0;
        if ((i % 100000) == 0) printk("AHCI: waiting... (%d)\n\r", i);
    }
    return -1;
}

static uint32_t find_free_slot(hba_port* port) {
    uint32_t slots = port->sact | port->ci;
    for (uint32_t i = 0; i < 32; i++) {
        if ((slots & (1 << i)) == 0) return i;
    }
    return 0xFFFFFFFF;
}

static void prepare_command(hba_cmd_header* cmd_hdr, hba_cmd_table* cmd_tbl, void* buffer, int write) {
    memset(cmd_hdr, 0, sizeof(hba_cmd_header));
    memset(cmd_tbl, 0, sizeof(hba_cmd_table));

    cmd_hdr->cfl = sizeof(fis_reg_h2d) / sizeof(uint32_t);
    cmd_hdr->w = write ? 1 : 0;
    cmd_hdr->prdtl = 1;
    cmd_hdr->ctba = (uintptr_t)cmd_tbl;

    cmd_tbl->prdt_entry[0].dba = (uintptr_t)buffer;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc = 512 - 1;
    cmd_tbl->prdt_entry[0].i = 1;
}

static void prepare_fis(fis_reg_h2d* fis, int write, uint64_t lba) {
    memset(fis, 0, sizeof(fis_reg_h2d));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA;
    fis->device = 1 << 6;
    fis->lba0 = lba & 0xFF;
    fis->lba1 = (lba >> 8) & 0xFF;
    fis->lba2 = (lba >> 16) & 0xFF;
    fis->lba3 = (lba >> 24) & 0xFF;
    fis->lba4 = (lba >> 32) & 0xFF;
    fis->lba5 = (lba >> 40) & 0xFF;
    fis->countl = 1;
    fis->counth = 0;
}

static int ahci_transfer(uint64_t lba, void* buffer, int write) {
    if (!active_port) return -1;

    stop_cmd(active_port);

    hba_cmd_header* cmd_hdr = kalloc(sizeof(hba_cmd_header));
    hba_cmd_table* cmd_tbl = kalloc(sizeof(hba_cmd_table));
    void* fb = kalloc(512);

    mmap(cmd_hdr, cmd_hdr, PAGE_PRESENT | PAGE_RW);
    mmap(cmd_tbl, cmd_tbl, PAGE_PRESENT | PAGE_RW);
    mmap(fb, fb, PAGE_PRESENT | PAGE_RW);

    active_port->clb = (uintptr_t)cmd_hdr;
    active_port->fb  = (uintptr_t)fb;

    prepare_command(cmd_hdr, cmd_tbl, buffer, write);
    prepare_fis((fis_reg_h2d*)cmd_tbl->cfis, write, lba);

    start_cmd(active_port);
    uint32_t slot = find_free_slot(active_port);
    if (slot == 0xFFFFFFFF) {
        printk("AHCI: no free command slots!\n\r");
        return -1;
    }
    active_port->ci |= 1 << slot;

    int res = wait_for_completion(active_port, 1 << slot);
    if (res < 0) print_port_status(active_port);

    unmap(cmd_hdr);
    unmap(cmd_tbl);
    unmap(fb);

    return res;
}

void ahci_read_sector(uint64_t lba, void* buffer) {
    ahci_transfer(lba, buffer, 0);
}

void ahci_write_sector(uint64_t lba, void* buffer) {
    ahci_transfer(lba, buffer, 1);
}
