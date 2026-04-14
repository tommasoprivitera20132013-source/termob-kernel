# Architecture

## Scope

TERMOB Kernel is a monolithic `i386` kernel intended to act as a clean low-level foundation for future operating-system work. It currently stays in kernel space only and focuses on correctness, observability, and subsystem structure rather than user-space features.

## Boot And Runtime Model

Boot path:
1. GRUB loads the Multiboot2 kernel image.
2. `src/boot/boot.asm` enters 32-bit kernel code with a dedicated stack.
3. `src/kernel/core/kernel.c` initializes boot info, heap, PMM, paging, driver model, block layer, virtio, audio detection, PCI, sound, scheduler, and text UI.
4. IDT, PIC, timer, keyboard, and mouse IRQs are enabled.
5. The kernel enters an interrupt-driven loop using `hlt`.

Current runtime model:
- single kernel address space
- no user mode
- no process model
- cooperative kernel-thread scheduling
- VGA text shell in kernel context
- diagnostic-first block and FAT read-only path

## Subsystem Layout

### Boot / Low-Level Assembly

- `src/boot/boot.asm`
- `src/boot/idt.asm`
- `src/boot/scheduler.asm`

Responsibilities:
- kernel entry
- fault and IRQ stubs
- minimal cooperative scheduler context switch

### Core

- `src/kernel/core/kernel.c`
- `src/kernel/core/idt.c`
- `src/kernel/core/pic.c`
- `src/kernel/core/irq.c`
- `src/kernel/core/timer.c`
- `src/kernel/core/fault.c`
- `src/kernel/core/panic.c`
- `src/kernel/core/klog.c`

Responsibilities:
- boot sequencing
- interrupt routing
- panic/fault handling
- timer ticks
- kernel logging
- top-level runtime UI handoff

### Memory Management

- `src/kernel/core/bootinfo.c`
- `src/kernel/core/heap.c`
- `src/kernel/core/pmm.c`
- `src/kernel/core/paging.c`

Current state:
- Multiboot2 memory map parsing
- physical frame accounting
- bootstrap paging
- free-list heap allocator with `kfree`, coalescing, and diagnostics

### Scheduling

- `src/kernel/include/scheduler.h`
- `src/kernel/core/scheduler.c`
- `src/boot/scheduler.asm`

Current state:
- cooperative kernel threads
- per-task dedicated stack
- explicit task state
- timer-driven reschedule request
- explicit `scheduler_yield()`

Not implemented yet:
- preemptive context switch
- blocking/wait queues
- user processes
- process isolation

### Driver Model And Hardware Discovery

- `src/kernel/core/driver.c`
- `src/kernel/core/device.c`
- `src/kernel/core/pci.c`
- `src/kernel/drivers/virtio.c`

Current state:
- `device / driver / bus` registry
- PCI enumeration
- virtio transport integration
- `virtio-blk` registration into block layer

### Console / Input / Debug

- `src/kernel/drivers/terminal.c`
- `src/kernel/drivers/keyboard.c`
- `src/kernel/drivers/mouse.c`
- `src/kernel/drivers/serial.c`

Current state:
- VGA text shell
- shell line editor
- shell scrollback
- keyboard shortcuts and cursor movement
- PS/2 mouse with wheel-backed shell scroll
- serial mirror and diagnostics

### Audio

- `src/kernel/drivers/sound.c`
- `src/kernel/drivers/audio.c`

Current state:
- PC speaker tone path
- audio PCI detection
- AC'97 test path

### Storage And Filesystems

- `src/kernel/core/block.c`
- `src/kernel/fs/fat.c`

Current state:
- block device registry
- raw sector reads
- FAT read-only mount, traversal, and file reads
- no write path
- no VFS layer yet

## Execution Invariants

Current assumptions:
- kernel runs in one address space
- shell and services stay inside kernel mode
- scheduler is cooperative, not preemptive
- FAT is read-only
- block I/O is synchronous
- diagnostics are preferred over silent failure

## Present Limitations

Architecturally important current limits:
- no synchronization primitive layer yet
- no wait/sleep/task blocking model
- no VFS
- no networking stack
- no user/kernel boundary
- no mature storage driver stack beyond current diagnostic-friendly foundation
