# Changelog

## 0.1.0-dev

- initial Multiboot2 / GRUB bootable kernel
- VGA text console and shell prompt
- dedicated kernel stack during bootstrap
- CPU exception handling with kernel panic screen
- PIC remap and stable hardware IRQ dispatch
- PIT timer on `IRQ0`
- keyboard input on `IRQ1`
- serial debug output on `COM1`
- in-memory kernel log with `dmesg`
- basic shell commands for system inspection
- shell commands for status, log control, halt, reboot, and panic testing
