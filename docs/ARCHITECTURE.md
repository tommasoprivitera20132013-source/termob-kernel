# Architecture

## Scope

TERMOB Kernel is an educational i386 monolithic kernel meant to act as a clean starting point
for a future operating system.

## Boot Flow

1. GRUB loads the Multiboot2 kernel image.
2. `src/boot/boot.asm` switches into the kernel entry path and installs a dedicated stack.
3. `kernel_main()` initializes serial debug, logging, the UI, IDT, PIC, PIT, and keyboard IRQs.
4. The kernel enters an interrupt-driven loop using `hlt`.

## Major Components

### Boot

- `src/boot/boot.asm`
- `src/boot/idt.asm`

These files contain the early entry path and assembly interrupt / exception stubs.

### Core

- `src/kernel/core/kernel.c`
- `src/kernel/core/idt.c`
- `src/kernel/core/pic.c`
- `src/kernel/core/irq.c`
- `src/kernel/core/timer.c`
- `src/kernel/core/fault.c`
- `src/kernel/core/panic.c`
- `src/kernel/core/klog.c`

This layer owns initialization, interrupt routing, panic handling, timer ticks, and kernel logging.

### Drivers

- `src/kernel/drivers/terminal.c`
- `src/kernel/drivers/keyboard.c`
- `src/kernel/drivers/serial.c`

This layer provides VGA console output, interrupt-driven keyboard input, and serial diagnostics.

## Diagnostics

TERMOB currently exposes three diagnostic outputs:

- VGA panic output
- serial debug on `COM1`
- in-memory log buffer accessible via `dmesg`

## Current Execution Model

- single address space
- no user mode
- no scheduler yet
- single shell running inside the kernel context

## Near-Term Next Steps

- central kernel panic / assertion usage across more subsystems
- heap allocation (`kmalloc`)
- paging
- scheduler / task model
- filesystem foundation
