CFLAGS=-target x86_64-unknown-windows --std=c23 -ffreestanding -fshort-wchar -mno-red-zone -I../edk2/MdePkg/Include -I../edk2/MdePkg/Include/X64 -I../ipxe/src/include
LDFLAGS=-target x86_64-unknown-windows -nostdlib -Wl,-entry:efi_main -Wl,-subsystem:efi_application -fuse-ld=lld-link

memdisk_uefi.elf: memdisk_uefi.o
	clang $(LDFLAGS) -o $@ $^

memdisk_uefi.o: memdisk_uefi.c
	clang $(CFLAGS) -c -o $@ $<

