# TERMOB Kernel

TERMOB Kernel is a custom monolithic kernel written in C and x86 assembly.

## Features

- GRUB bootable kernel
- Multiboot2 support
- VGA text-mode terminal
- Interactive shell
- Keyboard input
- Modular architecture

## Build

make clean
make
cp build/kernel.bin iso/boot/kernel.bin
grub-mkrescue -o build/termob-kernel.iso iso

## Run

qemu-system-x86_64 -boot d -cdrom build/termob-kernel.iso

## Commands

help  
clear  
info  
version  
about  
echo <text>  

## Status

Early development (v0.1.0-dev)

## License

MIT
