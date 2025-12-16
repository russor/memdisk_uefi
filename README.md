#memdisk_uefi
> Memdisk_UEFI allows PXE booting of disk images under UEFI

Memdisk_UEFI takes a single command line argument which is the path to the
image to boot.  Then it downloads the image, using iPXE's UEFI Download
protocol; Once the image is downloaded, it is registered using UEFI's
RamDiskProtocol (added in UEFI 2.6), and NFIT and SSDT ACPI tables are added
to expose the RamDisk as a NVDIMM (ACPI 6.0).  Then booting from the default
boot file (BOOTX64.EFI or similar) is attempted.

During pre-boot, the OS loader will be able to access the drive via UEFI
Protocols; however, once the OS calls UEFI's ExitBootServices(), it must
have a NVDIMM driver in order to find and use the RamDisk.

FreeBSD 12.0 (2018) added the nvdimm driver (/dev/spa), however it's not a
compiled in driver with the GENERIC kernel used in installers, so you need
to load the module manually if you boot the installer image; at the
bootloader, press 3 to ```Escape to loader prompt`` and then:

```
load boot/kernel/kernel
load boot/kernel/nvdimm.ko
boot
```

Linux 4.2 (2015) added support for NFIT tables (/dev/pmem). Some installer
images may not include the driver, but some do.

Configuring memdisk in iPXE is relatively straight forward:

```
boot memdisk_uefi.elf ${cwduri}FreeBSD-15.0-RELEASE-amd64-bootonly.iso
echo Error occured, press any key to reboot
prompt
reboot
```

iPXE will load the memdisk_uefi.efi binary from the current directory and
pass the image URI as a command line argument.  If there are any errors that
prevent booting, the prompt will allow you to read them.

For BIOS boot, PXE booting an installer image is commonly done with
[MEMDISK](https://wiki.syslinux.org/wiki/index.php?title=MEMDISK) from the
Syslinux Project

## Who did this

[Richard Russo](mailto:memdisk@enslaves.us)

## License
This project uses a header file and runtime services from
[iPXE](https://github.com/ipxe/ipxe), which are
marked GPLv2

Additionally, headers and code are used from [EDK
II](https://github.com/tianocore/edk2/), which have been marked
SPDX-License-Identifier: BSD-2-Clause-Patent

All original code is Copyright 2025 Richard Russo, BSD-2-Clause-Patent



