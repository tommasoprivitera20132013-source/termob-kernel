#ifndef TERMOB_KERNEL_H
#define TERMOB_KERNEL_H

#define TERMOB_KERNEL_NAME "TERMOB Kernel"
#define TERMOB_KERNEL_VERSION "0.1.0-dev"
#define TERMOB_KERNEL_ARCH "i386"
#define TERMOB_KERNEL_PROFILE "Monolithic"
#define TERMOB_KERNEL_LOAD_ADDRESS 0x00100000U
#define TERMOB_KERNEL_SUPPORT_EMAIL "privitera.tommaso2013@gmail.com"

#include <stdint.h>

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr);
uint32_t kernel_housekeeping_run_count(void);
uint32_t kernel_telemetry_run_count(void);

#endif
