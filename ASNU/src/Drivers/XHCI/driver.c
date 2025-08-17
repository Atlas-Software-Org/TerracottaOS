#include "../XHCI.h"
#include <PCI/pci.h>
#include <KiSimple.h>
#include <IDT/idt.h>
#include <VMM/vmm.h>
#include <stdint.h>
#include <stddef.h>

#define XHCI_IRQ_VECTOR        0x51
#define XHCI_CAPLENGTH         0x00
#define XHCI_DBOFF             0x14
#define XHCI_RTSOFF            0x18
#define XHCI_USBCMD            0x00
#define XHCI_USBSTS            0x04
#define XHCI_DCBAAP            0x30
#define XHCI_CONFIG            0x38
#define XHCI_PORTSC_BASE       0x400
#define XHCI_PORT_COUNT        8
#define XHCI_TRB_RING_SIZE     16

#define TRB_TYPE_PORT_STATUS_CHANGE 0x20
#define TRB_TYPE_TRANSFER_EVENT     0x21

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2

typedef struct {
    uint64_t ring[XHCI_TRB_RING_SIZE];
    uint64_t phys;
    uint8_t cycle;
    size_t index;
} TrbRing_t;

typedef struct {
    uint64_t RingSegmentBaseAddress;
    uint32_t RingSegmentSize;
    uint32_t Reserved;
} ErstEntry_t __attribute__((packed));

static void* XhciMmioBase;
static void* XhciRuntimeBase;
static void* XhciDoorbellBase;
static uint8_t XhciIrqLine;
static TrbRing_t EventRing;
static ErstEntry_t ErstTable __attribute__((aligned(64)));
static uint8_t HaveKeyboard = 0;
static uint8_t HaveMouse = 0;

static inline void MmioWrite32(uint32_t offset, uint32_t val) {
    *(volatile uint32_t*)((uintptr_t)XhciMmioBase + offset) = val;
}

static inline uint32_t MmioRead32(uint32_t offset) {
    return *(volatile uint32_t*)((uintptr_t)XhciMmioBase + offset);
}

static const char* DecodePortSpeed(uint32_t speed) {
    switch (speed) {
        case 1: return "Low Speed (1.5 Mbps)";
        case 2: return "Full Speed (12 Mbps)";
        case 3: return "High Speed (480 Mbps)";
        case 4: return "SuperSpeed (5 Gbps)";
        case 5: return "SuperSpeed+ (10 Gbps)";
        default: return "Unknown";
    }
}

static void XhciIrqHandler(void) {
    volatile uint64_t* trb = &EventRing.ring[EventRing.index];
    uint32_t trbType = (trb[2] >> 10) & 0x3F;
    uint8_t cycle = trb[2] & 1;

    if (cycle != EventRing.cycle) return;

    if (trbType == TRB_TYPE_PORT_STATUS_CHANGE) {
        uint8_t portId = (trb[0] >> 24) & 0xFF;
        if (portId < 1 || portId > XHCI_PORT_COUNT) return;

        uint32_t portsc = MmioRead32(XHCI_PORTSC_BASE + (portId - 1) * 0x10);
        uint32_t speed = (portsc >> 10) & 0xF;
        uint8_t connected = portsc & 1;

        if (connected) {
            const char* type = DecodePortSpeed(speed);
            printk("USB Device attached at port %u, detected type: %s\n", portId, type);

            if (speed == 3) {
                if (HaveKeyboard) return;
                HaveKeyboard = 1;
                printk("USB-Keyboard triggered IRQ.\n");
            } else if (speed == 2) {
                if (HaveMouse) return;
                HaveMouse = 1;
                printk("USB-Mouse triggered IRQ.\n");
            }
        }
    } else if (trbType == TRB_TYPE_TRANSFER_EVENT) {
        printk("Transfer Event. Data = %02X\n", (uint8_t)(trb[0] & 0xFF));
    }

    EventRing.index = (EventRing.index + 1) % XHCI_TRB_RING_SIZE;
    if (EventRing.index == 0) EventRing.cycle ^= 1;

    volatile uint32_t* iman = (volatile uint32_t*)((uintptr_t)XhciRuntimeBase + 0x20);
    iman[0] |= 0x1;

    KiPicSendEoi(XhciIrqLine);
}

void xHciInit(PciDevice_t* UsbController) {
    XhciMmioBase = UsbController->MMIOBase;
    XhciIrqLine = UsbController->interrupt_line;

    for (uintptr_t addr = (uintptr_t)XhciMmioBase; addr < (uintptr_t)XhciMmioBase + 0x10000; addr += 0x1000)
        KiMMap((void*)addr, (void*)addr, PAGE_PRESENT | PAGE_RW);

    uint32_t capLength = *(volatile uint8_t*)(XhciMmioBase + XHCI_CAPLENGTH);
    uint32_t dboff = *(volatile uint32_t*)(XhciMmioBase + XHCI_DBOFF);
    uint32_t rtsoff = *(volatile uint32_t*)(XhciMmioBase + XHCI_RTSOFF);

    XhciRuntimeBase = (void*)((uintptr_t)XhciMmioBase + (rtsoff & ~0x1F));
    XhciDoorbellBase = (void*)((uintptr_t)XhciMmioBase + (dboff & ~0x3));

    printk("xHCI MMIO = %p, RT = %p, DB = %p, IRQ = %u\n", XhciMmioBase, XhciRuntimeBase, XhciDoorbellBase, XhciIrqLine);

    MmioWrite32(XHCI_USBCMD, MmioRead32(XHCI_USBCMD) & ~1);
    while (MmioRead32(XHCI_USBSTS) & 1);

    EventRing.phys = (uintptr_t)&EventRing.ring;
    EventRing.cycle = 1;
    EventRing.index = 0;

    ErstTable.RingSegmentBaseAddress = EventRing.phys;
    ErstTable.RingSegmentSize = XHCI_TRB_RING_SIZE;
    ErstTable.Reserved = 0;

    uintptr_t erstPhys = (uintptr_t)&ErstTable;

    volatile uint32_t* interrupter = (volatile uint32_t*)((uintptr_t)XhciRuntimeBase + 0x20);
    interrupter[0] = (uint32_t)(erstPhys);
    interrupter[1] = (uint32_t)(erstPhys >> 32);
    interrupter[2] = 1;

    volatile uint32_t* iman = (volatile uint32_t*)((uintptr_t)XhciRuntimeBase + 0x20);
    iman[0] |= 0x2;

    MmioWrite32(XHCI_DCBAAP, 0);
    MmioWrite32(XHCI_DCBAAP + 4, 0);

    MmioWrite32(XHCI_CONFIG, 1);
    MmioWrite32(XHCI_USBCMD, MmioRead32(XHCI_USBCMD) | 1);

    idt_set_desc(XHCI_IRQ_VECTOR, XhciIrqHandler, 0x8E);
    idt_irq_clear_mask(XHCI_IRQ_VECTOR);

    printk("xHCI controller started\n");
}
