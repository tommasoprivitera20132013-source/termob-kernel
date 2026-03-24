CC = gcc
LD = ld
ASM = nasm
QEMU = qemu-system-x86_64
GRUB_MKRESCUE = grub-mkrescue

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -Isrc/kernel/include
LDFLAGS = -m elf_i386 -T linker.ld

BUILD_DIR = build
OBJ_C = $(BUILD_DIR)/kernel.o $(BUILD_DIR)/terminal.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/serial.o $(BUILD_DIR)/klog.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/pic.o $(BUILD_DIR)/irq.o $(BUILD_DIR)/fault.o $(BUILD_DIR)/panic.o $(BUILD_DIR)/timer.o
OBJ_ASM = $(BUILD_DIR)/boot.o $(BUILD_DIR)/idt_asm.o

KERNEL = $(BUILD_DIR)/kernel.bin
ISO = $(BUILD_DIR)/termob-kernel.iso

.PHONY: all iso run smoke ci clean
all: $(KERNEL)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/kernel.o: src/kernel/core/kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/kernel.c -o $(BUILD_DIR)/kernel.o

$(BUILD_DIR)/terminal.o: src/kernel/drivers/terminal.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/drivers/terminal.c -o $(BUILD_DIR)/terminal.o

$(BUILD_DIR)/keyboard.o: src/kernel/drivers/keyboard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/drivers/keyboard.c -o $(BUILD_DIR)/keyboard.o

$(BUILD_DIR)/serial.o: src/kernel/drivers/serial.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/drivers/serial.c -o $(BUILD_DIR)/serial.o

$(BUILD_DIR)/klog.o: src/kernel/core/klog.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/klog.c -o $(BUILD_DIR)/klog.o

$(BUILD_DIR)/idt.o: src/kernel/core/idt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/idt.c -o $(BUILD_DIR)/idt.o

$(BUILD_DIR)/pic.o: src/kernel/core/pic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/pic.c -o $(BUILD_DIR)/pic.o

$(BUILD_DIR)/irq.o: src/kernel/core/irq.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/irq.c -o $(BUILD_DIR)/irq.o

$(BUILD_DIR)/fault.o: src/kernel/core/fault.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/fault.c -o $(BUILD_DIR)/fault.o

$(BUILD_DIR)/panic.o: src/kernel/core/panic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/panic.c -o $(BUILD_DIR)/panic.o

$(BUILD_DIR)/timer.o: src/kernel/core/timer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/timer.c -o $(BUILD_DIR)/timer.o

$(BUILD_DIR)/boot.o: src/boot/boot.asm | $(BUILD_DIR)
	$(ASM) -f elf32 src/boot/boot.asm -o $(BUILD_DIR)/boot.o

$(BUILD_DIR)/idt_asm.o: src/boot/idt.asm | $(BUILD_DIR)
	$(ASM) -f elf32 src/boot/idt.asm -o $(BUILD_DIR)/idt_asm.o

$(KERNEL): $(OBJ_C) $(OBJ_ASM) linker.ld | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $(KERNEL) $(OBJ_ASM) $(OBJ_C)

iso: $(KERNEL)
	cp -f $(KERNEL) iso/boot/kernel.bin
	$(GRUB_MKRESCUE) -o $(ISO) iso

run: iso
	$(QEMU) -boot d -cdrom $(ISO)

smoke: iso
	timeout 5s $(QEMU) -boot d -cdrom $(ISO) -display none -serial stdio || test $$? -eq 124

ci: clean all iso smoke

clean:
	rm -rf $(BUILD_DIR)
	rm -f iso/boot/kernel.bin
