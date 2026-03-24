# Security Policy

## Supported Status

TERMOB Kernel is an early-stage educational kernel. At this stage, security hardening is limited
and no release branch should be considered suitable for production systems or real hardware
deployment.

## Reporting A Vulnerability

If you discover a security issue, please avoid posting full exploit details in a public issue
immediately. Instead, contact the maintainer privately first and include:

- affected commit or branch
- reproduction steps
- impact summary
- whether the issue is specific to QEMU or also affects real hardware

After the issue is understood, a coordinated public fix can be prepared.

## Current Security Limits

- no user/kernel isolation yet
- no paging-based protection yet
- no hardened allocator
- no process permission model

Security work is planned, but the current focus remains kernel stability, diagnostics, and
foundational architecture.
