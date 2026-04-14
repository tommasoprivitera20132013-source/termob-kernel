CC = gcc
LD = ld
ASM = nasm
QEMU = qemu-system-x86_64
GRUB_MKRESCUE = grub-mkrescue

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -Isrc/kernel/include
LDFLAGS = -m elf_i386 -T linker.ld

BUILD_DIR = build
QEMU_PCSPEAKER_ARGS = -machine pcspk-audiodev=termobspk -audiodev sdl,id=termobspk
QEMU_PCSPEAKER_PA_ARGS = -machine pcspk-audiodev=termobspk -audiodev pa,id=termobspk,server=$(PULSE_SERVER)
QEMU_AC97_ARGS = -audiodev sdl,id=ac970 -device AC97,audiodev=ac970

OBJ_C = $(BUILD_DIR)/kernel.o $(BUILD_DIR)/terminal.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/serial.o $(BUILD_DIR)/sound.o $(BUILD_DIR)/audio.o $(BUILD_DIR)/mouse.o $(BUILD_DIR)/dummy_pci.o $(BUILD_DIR)/klog.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/pic.o $(BUILD_DIR)/irq.o $(BUILD_DIR)/fault.o $(BUILD_DIR)/panic.o $(BUILD_DIR)/timer.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/bootinfo.o $(BUILD_DIR)/pmm.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/driver.o $(BUILD_DIR)/device.o $(BUILD_DIR)/block.o $(BUILD_DIR)/pci.o $(BUILD_DIR)/virtio.o $(BUILD_DIR)/fat.o $(BUILD_DIR)/scheduler.o
OBJ_ASM = $(BUILD_DIR)/boot.o $(BUILD_DIR)/idt_asm.o $(BUILD_DIR)/scheduler_asm.o

KERNEL = $(BUILD_DIR)/kernel.bin
ISO = $(BUILD_DIR)/termob-kernel.iso

.PHONY: all iso run run-audio run-audio-pa run-audio-sdl run-audio-wav run-ac97 audio-proof smoke ci clean
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

$(BUILD_DIR)/sound.o: src/kernel/drivers/sound.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/drivers/sound.c -o $(BUILD_DIR)/sound.o

$(BUILD_DIR)/audio.o: src/kernel/drivers/audio.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/drivers/audio.c -o $(BUILD_DIR)/audio.o

$(BUILD_DIR)/mouse.o: src/kernel/drivers/mouse.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/drivers/mouse.c -o $(BUILD_DIR)/mouse.o

$(BUILD_DIR)/dummy_pci.o: src/kernel/drivers/dummy_pci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/drivers/dummy_pci.c -o $(BUILD_DIR)/dummy_pci.o

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

$(BUILD_DIR)/heap.o: src/kernel/core/heap.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/heap.c -o $(BUILD_DIR)/heap.o

$(BUILD_DIR)/bootinfo.o: src/kernel/core/bootinfo.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/bootinfo.c -o $(BUILD_DIR)/bootinfo.o

$(BUILD_DIR)/pmm.o: src/kernel/core/pmm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/pmm.c -o $(BUILD_DIR)/pmm.o

$(BUILD_DIR)/paging.o: src/kernel/core/paging.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/paging.c -o $(BUILD_DIR)/paging.o

$(BUILD_DIR)/driver.o: src/kernel/core/driver.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/driver.c -o $(BUILD_DIR)/driver.o

$(BUILD_DIR)/device.o: src/kernel/core/device.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/device.c -o $(BUILD_DIR)/device.o

$(BUILD_DIR)/block.o: src/kernel/core/block.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/block.c -o $(BUILD_DIR)/block.o

$(BUILD_DIR)/pci.o: src/kernel/core/pci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/pci.c -o $(BUILD_DIR)/pci.o

$(BUILD_DIR)/virtio.o: src/kernel/drivers/virtio.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/drivers/virtio.c -o $(BUILD_DIR)/virtio.o

$(BUILD_DIR)/fat.o: src/kernel/fs/fat.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/fs/fat.c -o $(BUILD_DIR)/fat.o

$(BUILD_DIR)/scheduler.o: src/kernel/core/scheduler.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/core/scheduler.c -o $(BUILD_DIR)/scheduler.o

$(BUILD_DIR)/boot.o: src/boot/boot.asm | $(BUILD_DIR)
	$(ASM) -f elf32 src/boot/boot.asm -o $(BUILD_DIR)/boot.o

$(BUILD_DIR)/idt_asm.o: src/boot/idt.asm | $(BUILD_DIR)
	$(ASM) -f elf32 src/boot/idt.asm -o $(BUILD_DIR)/idt_asm.o

$(BUILD_DIR)/scheduler_asm.o: src/boot/scheduler.asm | $(BUILD_DIR)
	$(ASM) -f elf32 src/boot/scheduler.asm -o $(BUILD_DIR)/scheduler_asm.o

$(KERNEL): $(OBJ_C) $(OBJ_ASM) linker.ld | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $(KERNEL) $(OBJ_ASM) $(OBJ_C)

iso: $(KERNEL)
	cp -f $(KERNEL) iso/boot/kernel.bin
	$(GRUB_MKRESCUE) -o $(ISO) iso

run: iso
	$(QEMU) $(QEMU_PCSPEAKER_ARGS) -boot d -cdrom $(ISO)

run-audio: iso
	$(QEMU) $(QEMU_PCSPEAKER_ARGS) -boot d -cdrom $(ISO)

run-audio-pa: iso
	$(QEMU) $(QEMU_PCSPEAKER_PA_ARGS) -boot d -cdrom $(ISO)

run-audio-sdl: iso
	$(QEMU) -machine pcspk-audiodev=termobspk -audiodev sdl,id=termobspk -boot d -cdrom $(ISO)

run-audio-wav: iso
	rm -f $(BUILD_DIR)/termob-speaker.wav
	$(QEMU) -machine pcspk-audiodev=termobspk -audiodev wav,id=termobspk,path=$(BUILD_DIR)/termob-speaker.wav -boot d -cdrom $(ISO)

run-ac97: iso
	$(QEMU) $(QEMU_PCSPEAKER_ARGS) $(QEMU_AC97_ARGS) -boot d -cdrom $(ISO)

audio-proof: iso
	rm -f $(BUILD_DIR)/termob-speaker.wav $(BUILD_DIR)/audio-proof-serial.log
	bash -lc '{ sleep 2; for k in b e e p ret; do echo "sendkey $$k"; done; sleep 2; echo quit; } | $(QEMU) -machine pcspk-audiodev=termobspk -audiodev wav,id=termobspk,path=$(BUILD_DIR)/termob-speaker.wav -boot d -cdrom $(ISO) -display none -monitor stdio -serial file:$(BUILD_DIR)/audio-proof-serial.log -no-reboot'

smoke: iso
	timeout 5s $(QEMU) -boot d -cdrom $(ISO) -display none -serial stdio || test $$? -eq 124

ci: clean all iso smoke

clean:
	rm -rf $(BUILD_DIR)
	rm -f iso/boot/kernel.bin
