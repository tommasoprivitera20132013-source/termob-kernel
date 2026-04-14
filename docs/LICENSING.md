# Licensing Notes

## Source Code In This Repository

Unless a file states otherwise, the TERMOB Kernel source code in this repository is released under
the [MIT License](../LICENSE).

That covers the project source tree, including:
- kernel C code
- NASM bootstrap and interrupt code
- local build scripts and repository documentation

The canonical project license text is:
- [LICENSE](../LICENSE)

## External Tools Are Not Relicensed

Building and testing TERMOB typically involves external software such as:
- GCC
- NASM
- GRUB and `grub-mkrescue`
- QEMU
- `xorriso`

These tools are separate projects with their own licenses. This repository does not relicense
them, bundle their full license texts as project code, or change their original terms.

If you redistribute tool binaries, packages, or images produced from distribution packages, you
must follow the license terms of those upstream projects and of the packages provided by your OS
distribution.

## Generated Boot Media

When you generate a bootable ISO with:

```bash
make iso
```

the resulting image is not simply "MIT-only". The image contains:
- the TERMOB kernel binary from this repository
- GRUB boot components provided by your local GRUB installation

Those GRUB components remain under their own upstream licensing terms. If you plan to redistribute
generated ISO artifacts, keep the following in mind:
- the kernel source remains MIT-licensed
- GRUB files in the image are not relicensed under MIT
- the exact licensing notices may depend on the GRUB package installed on your system

## Third-Party Artifacts And Packaging

This repository does not currently vendor major third-party runtime libraries or firmware blobs as
source code. If that changes in the future, those components should carry their own notices and be
documented explicitly.

For distribution packaging or public binary releases, a good practice is to include:
- the project `LICENSE`
- a short note that the ISO also includes GRUB-provided files
- any additional notices required by your packaging or hosting environment

## No Legal Advice

These notes are practical repository guidance, not legal advice. For public redistribution beyond
development use, verify the exact licenses of the GRUB, QEMU, and toolchain packages installed on
your system.
