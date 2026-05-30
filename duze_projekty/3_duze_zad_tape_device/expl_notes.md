# Address Space
Address space is the complete set of memory addresses that a processor (or bus) can reference.
Each address holds one byte of data.

A 64-bit address means each address is represented by 64 binary digits.

# The PCI Address Space
The PCI bus has its own 64-bit address space — shared among all devices on the bus. When your CPU wants to talk to a i.e. GPU, it doesn't talk to the device directly; it **writes to an address, and the PCI bus routes that write to whichever device owns that address** range

# Configuration Space
Every PCI device exposes a small (256-byte standard, 4 KB extended) configuration space — a special set of registers readable by the OS/BIOS at boot time, used to identify and configure the device.

It contains things like:
- Vendor ID / Device ID
- Device class (GPU, NIC, storage, etc.)
- **BARs**

# What is a BAR (Base Address Registers)
Each PCI device has up to 6 BARs (BAR0–BAR5) in its configuration space. A BAR is how a device says:

*"I need a chunk of address space. Please assign me a base address for it."*

**A BAR is a 32-bit register** sitting inside the device's configuration space. 
It serves a dual purpose depending on when you read it:

- Before configuration — the device uses it to advertise what it needs
    How it works:

    - The BIOS/OS writes all-1s (0xFFFFFFFF) to a BAR
    - The device masks out the bits it doesn't care about — revealing how much space it needs
    - The OS reads back the result, calculates the size, then assigns a free base address
    - From that point on, any CPU access to that address range is routed by the PCI bus to that device

- After configuration — it holds the base address the OS assigned, which the device uses to respond to bus transactions

```text
PCI Configuration Space Header (first 64 bytes)
┌──────────┬──────────┬──────────┬──────────┐  Offset
│ Vendor ID│ Device ID│          │          │  0x00
├──────────┴──────────┤  Status  │ Command  │  0x04
│    Class Code       │ Revision │          │  0x08
├─────────────────────┴──────────┴──────────┤
│              ...other fields...           │  0x0C–0x0F
├───────────────────────────────────────────┤
│                   BAR0                    │  0x10  ← start of BARs
├───────────────────────────────────────────┤
│                   BAR1                    │  0x14
├───────────────────────────────────────────┤
│                   BAR2                    │  0x18
├───────────────────────────────────────────┤
│                   BAR3                    │  0x1C
├───────────────────────────────────────────┤
│                   BAR4                    │  0x20
├───────────────────────────────────────────┤
│                   BAR5                    │  0x24
└───────────────────────────────────────────┘
```

## The Lower Bits Are Not Part of the Address
A BAR is not a plain integer. Its lower bits are flags, not address bits. The device hardwires these — the OS cannot change them.

```text
Bit 31 ──────────────────── Bit 4 │ Bit 3 │ Bit 2-1 │ Bit 0
        Base Address (aligned)    │Prefet-│  Type   │  0
                                  │chable │         │
```