# TERMOB Kernel

TERMOB Kernel is an early-stage, GRUB-booted, 32-bit monolithic kernel written in C and NASM.
The project is designed as an open-source kernel foundation that other developers can study,
extend, and use as a base for building a small operating system.

## Status

TERMOB Kernel is in active development.
It is already able to boot, handle CPU exceptions, remap the PIC, receive PIT and keyboard
interrupts, expose a simple shell, and provide VGA plus serial diagnostics.

It is not a production operating system and it is not yet suitable for real hardware use.

## Current Capabilities

- Multiboot2 / GRUB boot flow
- Dedicated kernel stack in early bootstrap
- VGA text console and shell prompt
- CPU exception handling with panic screen
- PIC remap and IRQ dispatch
- PIT timer on `IRQ0`
- Keyboard input on `IRQ1`
- Multiboot2 memory map parsing
- Physical memory manager with 4 KiB frame bitmap allocator
- Early 1 MiB kernel heap with `kmalloc` / `kcalloc`
- Initial 32-bit paging with identity-mapped low memory
- Serial debug output on `COM1`
- PC speaker alert and test tone support
- Live footer monitor for runtime metrics
- In-memory kernel log with `dmesg`
- Basic shell commands: `help`, `clear`, `echo`, `beep`, `melody`, `info`, `perf`, `status`, `meminfo`, `pmminfo`, `paging`, `memmap`, `lspci`, `drivers`, `virtio`, `sound`, `audio`, `uname`, `version`, `about`, `uptime`, `ticks`, `logsize`, `dmesg`, `clearlog`, `halt`, `reboot`, `panic`

## Repository Layout

```text
src/boot/           early assembly bootstrap and interrupt stubs
src/kernel/core/    kernel core subsystems
src/kernel/drivers/ text console, keyboard, serial
src/kernel/include/ shared kernel headers
iso/boot/grub/      GRUB configuration for local testing
docs/               architecture notes and roadmap
```

## Build Requirements

On a Debian/Ubuntu-style system:

```bash
sudo apt-get update
sudo apt-get install -y gcc-multilib nasm grub-pc-bin grub-common xorriso qemu-system-x86
```

## Build And Run

```bash
make clean all
make iso
make run
```

If you specifically want PC speaker audio in QEMU, you can also force the audio-enabled run target:

```bash
make run-audio
```

## Smoke Test

This project also ships with a lightweight boot smoke test:

```bash
make smoke
```

## Shell Commands

```text
help
clear
echo <text>
beep
melody
info
perf
status
meminfo
pmminfo
paging
memmap
lspci
drivers
virtio
sound
audio
uname
version
about
uptime
ticks
logsize
dmesg
clearlog
halt
reboot
panic
```

## Shell Shortcuts

TERMOB currently exposes shell-local shortcuts for the active input line:

- `Ctrl+C` copies the current input line into the kernel shell clipboard
- `Ctrl+V` pastes the shell clipboard back into the current input line
- `Ctrl+U` clears the current input line
- `Ctrl+L` redraws the shell UI

## Debugging

TERMOB currently exposes three useful debugging channels:

- VGA panic screen for fatal faults
- PC speaker alert during panic handling
- serial output on `COM1`
- in-memory kernel log via `dmesg`
- PCI audio detection for `AC'97`, `HDA`, `ES1370`, and future `virtio-snd` targets

The panic screen also includes a support contact line for this project build.

Serial output example:

```bash
timeout 5s qemu-system-x86_64 -cdrom build/termob-kernel.iso -boot d -display none -serial stdio
```

## Project Goals

- Provide a clean educational kernel base
- Stay small enough to understand end-to-end
- Be structured well enough for contributors to build an operating system on top of it
- Keep the code i386- and GRUB-compatible while the platform is still young

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Contributing](CONTRIBUTING.md)
- [Code of Conduct](CODE_OF_CONDUCT.md)
- [Security Policy](SECURITY.md)
- [Changelog](CHANGELOG.md)

## License

This project is released under the [MIT License](LICENSE).
