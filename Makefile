CFLAGS=-target x86_64-unknown-windows --std=c23 -ffreestanding -fshort-wchar -mno-red-zone -I../edk2/MdePkg/Include -I../edk2/MdePkg/Include/X64 -I../ipxe/src/include
LDFLAGS=-target x86_64-unknown-windows -nostdlib -Wl,-entry:efi_main -Wl,-subsystem:efi_application -fuse-ld=lld-link

memdisk_uefi.elf: memdisk_uefi.o from_edk.o
	clang $(LDFLAGS) -o $@ $^

memdisk_uefi.o: memdisk_uefi.c memdisk.h
	clang $(CFLAGS) -c -o $@ $<

from_edk.o: from_edk.c RamDisk.hex
	clang $(CFLAGS) -c -o $@ $<

clean:
	rm *.o *.elf
