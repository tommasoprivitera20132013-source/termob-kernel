# Architecture

## Scope

TERMOB Kernel is an educational i386 monolithic kernel meant to act as a clean starting point
for a future operating system.

## Boot Flow

1. GRUB loads the Multiboot2 kernel image.
2. `src/boot/boot.asm` switches into the kernel entry path and installs a dedicated stack.
3. `kernel_main()` parses Multiboot2 boot info, initializes the early heap and PMM, enables initial paging, brings up the device model plus PCI discovery, initializes the PC speaker path, then enables serial debug, the UI, IDT, PIC, PIT, and keyboard IRQs.
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
- `src/kernel/core/bootinfo.c`
- `src/kernel/core/heap.c`
- `src/kernel/core/pmm.c`
- `src/kernel/core/paging.c`
- `src/kernel/core/device.c`
- `src/kernel/core/pci.c`

This layer owns initialization, interrupt routing, panic handling, timer ticks, kernel logging,
Multiboot2 memory discovery, the early bump allocator, and the bitmap-based physical frame
allocator, plus the initial identity-mapped paging setup, PCI enumeration, and the minimal
device / driver registry.

### Drivers

- `src/kernel/drivers/terminal.c`
- `src/kernel/drivers/keyboard.c`
- `src/kernel/drivers/serial.c`
- `src/kernel/drivers/virtio.c`
- `src/kernel/drivers/sound.c`
- `src/kernel/drivers/audio.c`

This layer provides VGA console output, interrupt-driven keyboard input, serial diagnostics, and
the first PCI-backed driver skeleton via `virtio-pci`, plus minimal PC speaker sound support and
a live footer monitor for core runtime metrics. It now also includes a first `audio-pci` detection
layer for AC'97 / HDA-class PCI controllers.

## Diagnostics

TERMOB currently exposes three diagnostic outputs:

- VGA panic output
- serial debug on `COM1`
- in-memory log buffer accessible via `dmesg`

## Current Execution Model

- single address space
- Multiboot2 memory map captured at boot
- bitmap-based physical frame allocator for 4 KiB frames
- initial 32-bit paging enabled with an identity-mapped low-memory window
- early bump-allocator heap only, no free list yet
- no user mode
- no scheduler yet
- single shell running inside the kernel context

## Near-Term Next Steps

- central kernel panic / assertion usage across more subsystems
- heap allocation beyond the early bump allocator
- dynamic page-table management beyond the initial identity map
- scheduler / task model
- filesystem foundation
- more PCI-backed drivers on top of the device model
