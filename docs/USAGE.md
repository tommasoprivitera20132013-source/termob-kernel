# Usage Guide

## Scope

TERMOB Kernel is a bare-metal `i386` monolithic kernel intended for QEMU-based development and
diagnostic work. It is not a general-purpose operating system and it does not provide a user-space
environment.

## Build Requirements

Recommended packages on Debian/Ubuntu-style systems:

```bash
sudo apt-get update
sudo apt-get install -y gcc-multilib nasm grub-pc-bin grub-common xorriso qemu-system-x86
```

Recommended host setup:
- Linux or WSL
- QEMU for boot tests
- GCC with 32-bit freestanding support

## Build Targets

Basic build flow:

```bash
make clean all
make iso
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

Target notes:
- `make run` boots the ISO in QEMU with the default SDL-backed PC speaker path.
- `make run-ac97` adds an AC'97 device for the kernel audio test path.
- `make run-audio-wav` records the PC speaker path to `build/termob-speaker.wav`.
- `make smoke` runs a short headless boot validation.
- `make ci` performs a clean build, ISO generation, and smoke test.

## First Boot

Start the kernel:

```bash
make run
```

After boot, the shell prompt looks like:

```text
[termob /]#
```

Good first commands:

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

## Shell Controls

Input controls:
- `Ctrl+C` copies the current input line inside the shell
- `Ctrl+V` pastes the copied line
- `Ctrl+U` clears the current line
- `Ctrl+L` redraws the shell UI
- `Left/Right` moves the cursor within the current line
- `Delete` deletes at the cursor
- `PageUp/PageDown` scroll shell history

Mouse:
- PS/2 mouse support is enabled during boot
- mouse wheel scrolls shell history when the QEMU mouse path exposes wheel packets
- if wheel scrolling is unavailable, `PageUp/PageDown` remains the fallback

## Audio Usage

PC speaker path:

```bash
make run
```

Inside the shell:

```text
beep
melody
panic
```

AC'97 test path:

```bash
make run-ac97
```

Inside the shell:

```text
audio
ac97
ac97tone
```

Offline audio proof:

```bash
make audio-proof
```

This produces:
- `build/termob-speaker.wav`

## FAT And Storage Usage

FAT commands require an attached block device. `make run` alone boots only the kernel ISO, so FAT
commands will fail unless QEMU is started with a data disk.

Example QEMU command with a raw FAT image:

```bash
qemu-system-x86_64 \
  -boot d \
  -cdrom build/termob-kernel.iso \
  -drive if=none,id=vblk,file=build/virtio-fat-file-test.img,format=raw \
  -device virtio-blk-pci,drive=vblk
```

Inside the shell:

```text
lsblk
fatstat 0 0
ls 0 0 /
fatlookup 0 0 /README.TXT
fatcat 0 0 /README.TXT
fatread 0 0 /README.TXT 0 64
```

Notes:
- `device` is the block-device index shown by `lsblk`
- `boot_lba` is `0` only when the FAT volume starts at LBA 0 of the attached image
- if you attach a partitioned image, the FAT filesystem may start at a later LBA

## Diagnostics

Main diagnostic paths:
- VGA panic screen
- serial output on `COM1`
- kernel log via `dmesg`
- heap integrity tools
- scheduler status tools
- FAT diagnostic commands
- raw block inspection commands

Useful commands:

```text
dashboard
status
heapinfo
heapcheck
heaptest
sched
mouse
dmesg
```

Headless serial boot example:

```bash
timeout 5s qemu-system-x86_64 -boot d -cdrom build/termob-kernel.iso -display none -serial stdio
```

## Common Problems

`FAT mount failed`
- no FAT block device is attached
- wrong `device` index was used
- wrong `boot_lba` was used
- the attached image is not a valid FAT volume

`fatcat` prints binary-looking refusal
- the file contents do not look like printable text
- use `fatread` for controlled offset-based inspection instead

No wheel scroll in QEMU
- click inside the QEMU window to capture input first
- if the host/QEMU path does not expose wheel packets, use `PageUp/PageDown`

No audio from PC speaker
- confirm you are using `make run` or `make run-audio-sdl`
- use `make audio-proof` to confirm that the kernel produced an actual audio waveform
