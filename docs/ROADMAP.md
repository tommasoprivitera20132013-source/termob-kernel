# Roadmap

## Phase 1: Stable Kernel Base

- robust interrupt and exception handling
- serial debug and kernel log
- panic / assert infrastructure
- cleaner subsystem boundaries

## Phase 2: Memory And Runtime

- physical memory discovery
- full kernel heap allocator
- dynamic paging and virtual memory growth beyond the bootstrap identity map
- stronger fault diagnostics

## Phase 3: Scheduling And Execution

- task abstraction
- context switching
- preemptive scheduling
- sleep / timer services

## Phase 4: Storage And Filesystems

- RAM-backed filesystem or simple FAT reader
- path handling
- basic file operations
- shell commands for file access

## Phase 5: User/Kernel Boundary

- syscall layer
- ring 3 support
- ELF loading
- initial user-space process

## Phase 6: Device Expansion

- richer keyboard handling
- mouse
- disk drivers
- PCI discovery
- first PCI-backed driver bindings
- virtio experimentation for VM-friendly devices
- richer sound beyond the PC speaker baseline
- real audio output beyond PC speaker tones
- PCM playback backend for AC'97 or Intel HDA
- network driver experimentation
