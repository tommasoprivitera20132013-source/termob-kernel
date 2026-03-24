# Contributing To TERMOB Kernel

Thanks for contributing to TERMOB Kernel.

## Project Direction

TERMOB is meant to be a small but serious kernel base that other developers can understand and
extend. Contributions should improve stability, clarity, diagnostics, or well-scoped kernel
capabilities without turning the codebase into an uncontrolled experiment.

## Development Principles

- Prefer small, reviewable changes.
- Preserve i386 and GRUB compatibility.
- Keep low-level code explicit and easy to audit.
- Improve diagnostics before adding complex behavior.
- Do not redesign large subsystems unless there is a documented reason.

## Local Setup

Recommended packages on Debian/Ubuntu-style systems:

```bash
sudo apt-get update
sudo apt-get install -y gcc-multilib nasm grub-pc-bin grub-common xorriso qemu-system-x86
```

Build and run locally:

```bash
make clean all
make iso
make run
```

Quick smoke test:

```bash
make smoke
```

## Coding Guidelines

- Use C for kernel logic and NASM for bootstrap / interrupt assembly.
- Favor clear names over clever code.
- Keep comments short and technical.
- Avoid unrelated refactors in feature or bug-fix changes.
- Add or update diagnostics when changing fault-prone code.

## Commit Style

Use scoped commit messages when possible:

- `boot: initialize dedicated kernel stack`
- `idt: fix selector used for interrupt gates`
- `keyboard: add dmesg shell command`
- `docs: document build and architecture`

## Pull Requests

Each pull request should include:

- a short problem statement
- a summary of the implementation
- exact local test commands used
- any known limitations or follow-up work

## Reporting Issues

When opening a bug report, include:

- host OS / distro
- toolchain versions if relevant
- exact build/run command
- QEMU version if applicable
- expected behavior
- actual behavior
- serial output or panic screen details when available
