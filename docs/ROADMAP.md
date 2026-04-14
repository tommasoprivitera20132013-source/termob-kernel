# Roadmap

## Current Baseline

Already in place:
- stable GRUB / Multiboot2 boot path
- VGA shell and diagnostics
- serial logging
- PMM and bootstrap paging
- heap allocator with free-list support
- driver model and PCI enumeration
- virtio and block layer foundation
- FAT read-only file access
- PS/2 keyboard and mouse wheel shell scroll
- cooperative kernel-thread foundation

## Next Kernel-Only Priorities

### 1. Synchronization

- spinlocks
- interrupt-safe lock variants
- lock discipline for scheduler, heap, and shared device structures

### 2. Scheduler Maturity

- explicit blocked/sleeping task states
- sleep by timer ticks
- simple wakeup path
- stronger run-queue invariants
- preparation for future preemption

### 3. Internal API Cleanup

- cleaner subsystem boundaries
- less shell-to-subsystem coupling
- stronger public/internal header separation
- consistent error patterns

### 4. Diagnostics And Robustness

- subsystem-level tagged logging
- stronger scheduler diagnostics
- stronger FAT corruption reporting
- stronger block-layer validation
- more assert-style defensive checks

## Storage And Filesystem Direction

Near-term:
- FAT read-only hardening
- richer file diagnostics
- cleaner file API

Later:
- VFS-like abstraction
- additional filesystem support
- eventual write support only after stronger validation paths

## Scheduling Direction

Current scheduler is intentionally limited:
- kernel-only
- cooperative
- no user processes
- no full preemption claims

Planned evolution:
- sleep / wake
- blocking primitives
- safer context lifecycle
- future preemption groundwork

## Device Direction

Near-term driver work that fits the current kernel:
- stronger mouse and input handling
- better storage-driver maturity
- additional virtio coverage
- more robust audio path

Not immediate priorities:
- GUI
- user-space desktop features
- large unrelated drivers without architectural need

## Long-Term OS Transition

Only after the kernel base is more solid:
- syscall layer
- user/kernel boundary
- ELF loading
- first user-space task
- broader operating-system features
