#include "../include/fault.h"
#include "../include/panic.h"

static const char* const fault_names[32] = {
    "Divide Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "BOUND Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

void isr_fault_handler_c(const interrupt_frame_t* frame) {
    const char* fault_name;

    if (frame->vector == 14U) {
        uint32_t fault_address;

        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
        kernel_panic_page_fault(frame, fault_address);
    }

    fault_name = "Unknown Exception";
    if (frame->vector < 32) {
        fault_name = fault_names[frame->vector];
    }

    kernel_panic_exception(fault_name, frame);
}
