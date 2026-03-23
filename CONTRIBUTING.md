# Contributing

Thanks for your interest in contributing to TERMOB Kernel.

## Guidelines

- Keep the code simple and readable.
- Follow the current project structure.
- Do not introduce large changes without first stabilizing the current code.
- Prefer small, isolated commits.
- Test changes in QEMU before submitting.

## Coding Style

- C for kernel logic
- x86 assembly for low-level boot code
- clear naming
- minimal but useful comments
- keep modules focused on a single responsibility

## Development Workflow

1. build the kernel
2. run it in QEMU
3. verify the feature works
4. commit changes with a clear message

## Commit Style

Examples:

- `boot: fix multiboot header`
- `terminal: improve prompt rendering`
- `keyboard: add basic backspace handling`
