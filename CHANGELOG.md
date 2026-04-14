# Changelog

## 0.1.0-dev

- initial Multiboot2 / GRUB bootable kernel
- VGA text console and shell prompt
- dedicated kernel stack during bootstrap
- CPU exception handling with kernel panic screen
- PIC remap and stable hardware IRQ dispatch
- PIT timer on `IRQ0`
- keyboard input on `IRQ1`
- Multiboot2 memory map parsing
- bitmap-based physical frame allocator for 4 KiB pages
- early 1 MiB kernel heap with `kmalloc` and `kcalloc`
- initial 32-bit paging with identity-mapped low memory
- serial debug output on `COM1`
- in-memory kernel log with `dmesg`
- basic shell commands for system inspection
- memory inspection command via `meminfo`
- PMM and memory-map inspection commands via `pmminfo` and `memmap`
- paging inspection command via `paging`
- minimal device / driver model
- PCI enumeration command via `lspci`
- initial `virtio-pci` driver skeleton and inspection command
- minimal PC speaker driver with `beep` and `sound`
- improved panic screen layout with richer register output
- live footer performance monitor
- support contact shown in the panic screen
- new shell commands `perf` and `melody`
- PCI audio detection command via `audio`
- initial `audio-pci` driver skeleton for AC'97 / HDA-class devices
- shell-local shortcuts for copy/paste, clear-line, and redraw
- shell commands for status, log control, halt, reboot, and panic testing
