#include "../include/idt.h"
#include "../include/panic.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idt_descriptor;

extern void idt_load(uint32_t idt_ptr_address);
extern void isr0_stub(void);
extern void isr1_stub(void);
extern void isr2_stub(void);
extern void isr3_stub(void);
extern void isr4_stub(void);
extern void isr5_stub(void);
extern void isr6_stub(void);
extern void isr7_stub(void);
extern void isr8_stub(void);
extern void isr9_stub(void);
extern void isr10_stub(void);
extern void isr11_stub(void);
extern void isr12_stub(void);
extern void isr13_stub(void);
extern void isr14_stub(void);
extern void isr15_stub(void);
extern void isr16_stub(void);
extern void isr17_stub(void);
extern void isr18_stub(void);
extern void isr19_stub(void);
extern void isr20_stub(void);
extern void isr21_stub(void);
extern void isr22_stub(void);
extern void isr23_stub(void);
extern void isr24_stub(void);
extern void isr25_stub(void);
extern void isr26_stub(void);
extern void isr27_stub(void);
extern void isr28_stub(void);
extern void isr29_stub(void);
extern void isr30_stub(void);
extern void isr31_stub(void);

static void (* const idt_fault_handlers[32])(void) = {
    isr0_stub,
    isr1_stub,
    isr2_stub,
    isr3_stub,
    isr4_stub,
    isr5_stub,
    isr6_stub,
    isr7_stub,
    isr8_stub,
    isr9_stub,
    isr10_stub,
    isr11_stub,
    isr12_stub,
    isr13_stub,
    isr14_stub,
    isr15_stub,
    isr16_stub,
    isr17_stub,
    isr18_stub,
    isr19_stub,
    isr20_stub,
    isr21_stub,
    isr22_stub,
    isr23_stub,
    isr24_stub,
    isr25_stub,
    isr26_stub,
    isr27_stub,
    isr28_stub,
    isr29_stub,
    isr30_stub,
    isr31_stub
};

static uint16_t idt_current_code_selector(void) {
    uint16_t selector;

    __asm__ volatile ("mov %%cs, %0" : "=r"(selector));
    return selector;
}

void idt_set_gate(uint8_t vector, uint32_t handler) {
    KASSERT(handler != 0U);

    idt[vector].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector = idt_current_code_selector();
    idt[vector].zero = 0;
    idt[vector].type_attr = 0x8E;
    idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void idt_init(void) {
    int i;
    int vector;

    for (i = 0; i < 256; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
        idt[i].offset_high = 0;
    }

    for (vector = 0; vector < 32; vector++) {
        idt_set_gate((uint8_t)vector, (uint32_t)idt_fault_handlers[vector]);
    }

    idt_descriptor.limit = (uint16_t)(sizeof(idt) - 1);
    idt_descriptor.base = (uint32_t)&idt;

    idt_load((uint32_t)&idt_descriptor);
}
