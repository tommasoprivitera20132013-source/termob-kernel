# TERMOB Kernel

TERMOB Kernel is an open-source, GRUB-booted, 32-bit monolithic kernel written in C and NASM for the i386 architecture. It is designed as a realistic kernel foundation for low-level systems work, not as a complete operating system.

## Project Status

TERMOB currently provides a stable boot path, a VGA text-mode shell, serial diagnostics, memory-management foundations, a driver model, storage diagnostics, FAT read-only support, basic audio paths, and cooperative kernel-thread foundations.

What it is:
- a kernel project
- a driver-oriented bare-metal foundation
- a good base for future OS development

What it is not:
- not a production operating system
- not ready for real hardware use without more validation
- not feature-complete

## Technical Specification

Current kernel profile:
- Architecture: `i386` / 32-bit x86
- Boot protocol: `Multiboot2`
- Bootloader: `GRUB`
- Kernel model: monolithic
- UI mode: VGA text mode `80x25`
- Address-space model: single kernel address space
- User mode: not implemented
- Scheduler: cooperative kernel threads, not preemptive multitasking
- Filesystem support: FAT read-only only
- Primary VM target: QEMU

Implemented subsystems:
- Multiboot2 boot info parsing
- dedicated early kernel stack
- IDT, exception handling, panic path
- PIC remap and IRQ handling
- PIT timer
- serial logging on `COM1`
- in-memory kernel log with `dmesg`
- physical memory manager
- bootstrap paging
- heap allocator with `kmalloc`, `kcalloc`, `kfree`, free-list, coalescing, diagnostics
- driver model with `device / driver / bus`
- PCI enumeration
- virtio transport and `virtio-blk`
- block layer
- FAT read-only mount / traversal / file read
- VGA shell and dashboard
- PS/2 keyboard
- PS/2 mouse with wheel-backed shell scroll
- PC speaker support
- AC'97 test path / audio PCI detection
- cooperative scheduler with basic kernel-thread execution

## Repository Layout

```text
src/boot/            assembly bootstrap, IDT stubs, scheduler switch path
src/kernel/core/     core kernel subsystems
src/kernel/drivers/  terminal, keyboard, mouse, serial, audio, virtio
src/kernel/fs/       FAT read-only implementation
src/kernel/include/  public kernel headers
iso/boot/grub/       GRUB config used for local boot media
docs/                architecture, roadmap, usage, licensing notes
```

## Build Requirements

On a Debian/Ubuntu-style environment:

```bash
sudo apt-get update
sudo apt-get install -y gcc-multilib nasm grub-pc-bin grub-common xorriso qemu-system-x86
```

Recommended environment:
- Linux or WSL
- QEMU for testing
- GCC with 32-bit freestanding support

## Build And Run

Basic flow:

```bash
make clean all
make iso
make run
```

Useful targets:

```text
make all
make iso
make run
make run-audio
make run-audio-pa
make run-audio-sdl
make run-audio-wav
make run-ac97
make audio-proof
make smoke
make ci
```

Notes:
- `make run` uses the default SDL-backed PC speaker path configured in the `Makefile`
- `make run-ac97` starts QEMU with an AC'97 device for the kernel audio test path
- `make run-audio-wav` writes PC speaker output to `build/termob-speaker.wav`
- `make smoke` performs a short headless boot check

## How To Use It

After boot, the kernel opens a VGA shell prompt:

```text
[termob /]#
```

Useful first commands:

```text
help
dashboard
status
meminfo
heapinfo
sched
lspci
lsblk
mouse
dmesg
```

Shell shortcuts:
- `Ctrl+C` copy current input line
- `Ctrl+V` paste copied input line
- `Ctrl+U` clear current input line
- `Ctrl+L` redraw the shell UI
- `Left/Right` move inside the input line
- `Delete` delete at cursor
- `PageUp/PageDown` scroll shell history
- mouse wheel scrolls shell history when PS/2 wheel support is active in QEMU

## Shell Command Groups

General:
- `help`
- `clear`
- `echo <text>`
- `beep`
- `melody`

System:
- `dashboard`
- `sysview`
- `sched`
- `info`
- `status`
- `perf`
- `uname`
- `version`
- `about`
- `uptime`
- `ticks`

Memory:
- `meminfo`
- `heapinfo`
- `heapcheck`
- `heaptest`
- `pmminfo`
- `paging`
- `memmap`

Drivers and buses:
- `lspci`
- `drivers`
- `virtio`
- `vblk`
- `lsblk`
- `mouse`
- `audio`
- `ac97`
- `ac97tone`

Storage raw:
- `blkread0`
- `blkread <lba>`
- `blkread <device> <lba>`
- `blkreadn <device> <lba> <count>`
- `blksig <device> <lba>`
- `blkfind <device> <lba> <count>`
- `bootchk <device> <lba>`

FAT read-only:
- `fatinfo <device> <lba>`
- `fatcheck <device> <lba>`
- `fatentry <device> <boot_lba> <cluster>`
- `fatchain <device> <boot_lba> <cluster>`
- `fatstat <device> <boot_lba>`
- `fatls <device> <boot_lba>`
- `fatlsroot <device> <boot_lba>`
- `fatlsdir <device> <boot_lba> <cluster>`
- `ls <device> <boot_lba> [</dir>]`
- `fatlookup <device> <boot_lba> </path>`
- `fatlspath <device> <boot_lba> </dir>`
- `fatcat <device> <boot_lba> </file>`
- `fatread <device> <boot_lba> </file> <offset> <count>`

Logs and control:
- `dmesg`
- `logsize`
- `clearlog`
- `halt`
- `reboot`
- `panic`

## FAT Usage Example

Boot the kernel with a FAT image attached to QEMU, then inside the shell:

```text
lsblk
fatstat 0 0
ls 0 0 /
fatlookup 0 0 /README.TXT
fatcat 0 0 /README.TXT
```

Important:
- `make run` by itself boots the kernel ISO only
- FAT commands need a block device containing a FAT volume

## Diagnostics

Available diagnostic channels:
- VGA panic screen
- serial output on `COM1`
- in-memory kernel log via `dmesg`
- heap diagnostics
- scheduler diagnostics
- FAT diagnostics
- block-device raw inspection

Good debug commands:

```text
dashboard
status
heapinfo
heapcheck
sched
mouse
dmesg
```

Headless serial boot example:

```bash
timeout 5s qemu-system-x86_64 -boot d -cdrom build/termob-kernel.iso -display none -serial stdio
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Usage Guide](docs/USAGE.md)
- [Licensing Notes](docs/LICENSING.md)
- [Contributing](CONTRIBUTING.md)
- [Code of Conduct](CODE_OF_CONDUCT.md)
- [Security Policy](SECURITY.md)
- [Changelog](CHANGELOG.md)

## License

The TERMOB Kernel source code in this repository is released under the [MIT License](LICENSE).

Important licensing note:
- the repository source code is MIT-licensed
- external build tools such as GRUB, QEMU, GCC, NASM, and `xorriso` are separate projects under their own licenses
- bootable media generated with `grub-mkrescue` includes third-party GRUB components that are not relicensed under MIT

For a practical breakdown, see [docs/LICENSING.md](docs/LICENSING.md).
