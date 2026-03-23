CC = gcc
LD = ld
ASM = nasm

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -Isrc/kernel/include
LDFLAGS = -m elf_i386 -T linker.ld

OBJ_C = build/kernel.o build/terminal.o build/keyboard.o
OBJ_ASM = build/boot.o

KERNEL = build/kernel.bin
ISO = build/termob-kernel.iso

all: $(KERNEL)

build/kernel.o: src/kernel/core/kernel.c
	$(CC) $(CFLAGS) -c src/kernel/core/kernel.c -o build/kernel.o

build/terminal.o: src/kernel/drivers/terminal.c
	$(CC) $(CFLAGS) -c src/kernel/drivers/terminal.c -o build/terminal.o

build/keyboard.o: src/kernel/drivers/keyboard.c
	$(CC) $(CFLAGS) -c src/kernel/drivers/keyboard.c -o build/keyboard.o

build/boot.o: src/boot/boot.asm
	$(ASM) -f elf32 src/boot/boot.asm -o build/boot.o

$(KERNEL): build/kernel.o build/terminal.o build/keyboard.o build/boot.o linker.ld
	$(LD) $(LDFLAGS) -o $(KERNEL) build/boot.o build/kernel.o build/terminal.o build/keyboard.o

iso: $(KERNEL)
	cp $(KERNEL) iso/boot/kernel.bin
	grub-mkrescue -o $(ISO) iso

run: iso
	qemu-system-x86_64 -boot d -cdrom $(ISO)

clean:
	rm -f build/*
	rm -f iso/boot/kernel.bin
