// Copyright 2025 Richard Russo
// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "memdisk.h"

#include <Protocol/DevicePath.h>
#include <Protocol/DevicePathToText.h>
#include <Protocol/DevicePathUtilities.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/RamDisk.h>
#include <Protocol/SimpleFileSystem.h>

#define FILE_LICENCE(X)
#include <ipxe/efi/efi_download.h>

bool gDownloading = false;
UINTN gDownloadSize = 0;
UINTN gDownloadProgress = 0;
UINTN gDownloadProgressAmount = 0;
// default memory type is Reserved so that the disk survives beyond OS start
EFI_MEMORY_TYPE gMemType = EfiReservedMemoryType;

EFI_PHYSICAL_ADDRESS gDownloadBuffer;
EFI_STATUS gDownloadStatus = EFI_SUCCESS;
EFI_SYSTEM_TABLE *ST;
EFI_BOOT_SERVICES *BS;

void * memset(void *dest, int c, size_t len) {
    BS->SetMem(dest, len, c);
    return dest;
}

int lstreq(const CHAR16 *s1, const CHAR16 *s2) {
    if (*s1 != *s2) {
        return 0;
    } else if (*s1 == 0 && *s2 == 0) {
        return 1;
    } else {
        return lstreq(s1 + 1, s2 + 1);
    }
}

EFI_STATUS print_str(CHAR16 * str) {
    return ST->ConOut->OutputString(ST->ConOut, str);
}

EFI_STATUS print_num(UINTN num) {
    if (!num) {
        return print_str(L"0");
    }  
    CHAR16 buf[256];
    CHAR16* cur = &buf[255];
    *cur = 0;
    while (num) {
        --cur;
        *cur = L'0' + num % 10;
        num /= 10;
    }
    
    return print_str(cur);
}

void print_dev(EFI_DEVICE_PATH_PROTOCOL* dev) {
    static EFI_DEVICE_PATH_TO_TEXT_PROTOCOL *protocol = NULL;
    if (protocol == NULL) {
        EFI_GUID guid = EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID;
        BS->LocateProtocol (&guid, NULL, (void**)&protocol);
    }
    if (protocol == NULL) {
        print_str(L"DEVICE_PATH_NOPROTOCOL");
    } else {
        CHAR16 * buf = protocol->ConvertDevicePathToText(dev, TRUE, TRUE);
        if (buf == NULL) {
            print_str(L"DEVICE_PATH_ERROR");
        } else {
            print_str(buf);
            BS->FreePool(buf);
        }
    }
}

EFI_STATUS download_data (IN VOID *Context, IN VOID *Buffer, IN UINTN BufferLength, IN UINTN FileOffset) {
    EFI_STATUS status = EFI_SUCCESS;
    if (gDownloadSize == 0 && BufferLength == 0) {
        gDownloadSize = FileOffset;
        gDownloadProgressAmount = FileOffset / 20;
        // Round size to an even number of 4096 Pages
        if (gDownloadSize & 4095) {
            gDownloadSize = (gDownloadSize & ~4095) + 4096;
        }

        UINTN pages = gDownloadSize >> 12;
        status = BS->AllocatePages(AllocateAnyPages, gMemType, pages, &gDownloadBuffer);
        if (EFI_ERROR (status)) {
            print_str(L"Couldn't allocate pages for download\r\n");
            gDownloadStatus = status;
            gDownloading = false;
        }
        BS->SetMem((void *)gDownloadBuffer, pages << 12, 0);
   } else if (FileOffset + BufferLength > gDownloadSize) {
        print_str(L"Download buffer extends beyond allocated buffer\r\n");
        print_str(L"FileOffset: "); print_num(FileOffset);
        print_str(L", BufferLength: "); print_num(BufferLength);
        print_str(L", gDownloadSize: "); print_num(gDownloadSize);
        print_str(L"\r\n");
        gDownloadStatus = status = -1;
        gDownloading = false;
   } else {
       CHAR8 *src = Buffer;
       CHAR8 *dst = (void*)(gDownloadBuffer + FileOffset);
   
       BS->CopyMem(dst, src, BufferLength);

       UINTN before = gDownloadProgress / gDownloadProgressAmount;
       gDownloadProgress += BufferLength;
       UINTN after = gDownloadProgress / gDownloadProgressAmount;
       if (after != before) {
           print_str(L".");
       }
   }
   return status;
}

void download_finish(IN VOID *Context, IN EFI_STATUS Status) {
    gDownloading = false;
    gDownloadStatus = Status;
}

int device_path_prefix_match_impl(UINT8 *prefix, UINT8 *full) {
    while (1) {
        UINT16 length = prefix[2] | (prefix[3] >> 8);
        if (prefix[0] == 0x7F && prefix[1] == 0xFF) {
            return 1;
        }
        while (length) {
            if (*prefix != *full) {
                return 0;
            }
            ++prefix;
            ++full;
            --length;
        }
    }
}    

int device_path_prefix_match(EFI_DEVICE_PATH_PROTOCOL *prefix, EFI_DEVICE_PATH_PROTOCOL *full) {
    return device_path_prefix_match_impl((UINT8 *) prefix, (UINT8 *) full);
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status = 0;
    EFI_INPUT_KEY Key;

    ST = SystemTable;
    BS = ST->BootServices;

    EFI_LOADED_IMAGE *loaded_image = NULL;                  /* image interface */
    EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;      /* image interface GUID */

    
    Status = BS->OpenProtocol (ImageHandle, &lipGuid, (void**)&loaded_image,
                               ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR (Status)) {
        print_str(L"Couldn't open Loaded Image protocol\r\n");
        return Status;
    }

    EFI_GUID ipxeGuid = IPXE_DOWNLOAD_PROTOCOL_GUID;
    IPXE_DOWNLOAD_PROTOCOL *ipxe_download;
    Status = BS->LocateProtocol (&ipxeGuid, NULL, (void**)&ipxe_download);
    if (EFI_ERROR (Status)) {
        print_str(L"Couldn't open IPXE Download protocol\r\n");
        return Status;
    }

    EFI_GUID ramdiskGuid = EFI_RAM_DISK_PROTOCOL_GUID;
    EFI_RAM_DISK_PROTOCOL *ram_disk;
    Status = BS->LocateProtocol (&ramdiskGuid, NULL, (void**)&ram_disk);
    if (EFI_ERROR (Status)) {
        print_str(L"Couldn't open Ram Disk protocol\r\n");
        return Status;
    }

    EFI_GUID devicepathutilsGuid = EFI_DEVICE_PATH_UTILITIES_PROTOCOL_GUID;
    EFI_DEVICE_PATH_UTILITIES_PROTOCOL *dev_path_utils;
    Status = BS->LocateProtocol (&devicepathutilsGuid, NULL, (void**)&dev_path_utils);
    if (EFI_ERROR (Status)) {
        print_str(L"Couldn't open Device Path Utilities protocol\r\n");
        return Status;
    }

    EFI_DEVICE_PATH_PROTOCOL *boot_file_node = dev_path_utils->CreateDeviceNode(
                MEDIA_DEVICE_PATH, MEDIA_FILEPATH_DP, SIZE_OF_FILEPATH_DEVICE_PATH + sizeof(EFI_REMOVABLE_MEDIA_FILE_NAME));
    BS->CopyMem((void *) boot_file_node + sizeof(EFI_DEVICE_PATH_PROTOCOL), (void *)EFI_REMOVABLE_MEDIA_FILE_NAME, sizeof(EFI_REMOVABLE_MEDIA_FILE_NAME));

    bool add_table = true;

    CHAR16* load_options = loaded_image->LoadOptions;
    if (load_options == NULL) {
        print_str(L"image LoadOptions is null\r\n");
        return -1;
    }
    
    if (load_options[loaded_image->LoadOptionsSize / sizeof(load_options[0]) -1] != 0) {
        print_str(L"image LoadOptions is not null terminated\r\n");
        return -1;
    }
    
    while (*load_options != L' ' && *load_options != 0)  {
      ++load_options;
    }
    if (*load_options == L' ') {
        ++load_options;
    } else {
        print_str(L"Did not find load_options; sad sad\r\n");
        return -1;
    }
    
    CHAR8 clean_uri[256] = {0};
    size_t i = 0;
    
    while (*load_options != L' ' && *load_options != 0 && i < sizeof(clean_uri) - 1) {
        if (*load_options > L' ' && *load_options <= L'~' && (*load_options & ~0x7f) == 0) {
            clean_uri[i] = *load_options & 0x7f;
        } else {
            print_str(L"invalid character in uri\r\n");
            return -1;
        }
        ++load_options;
        ++i;
    }

    bool pause_before_boot = false;
    EFI_GUID download_type = EFI_VIRTUAL_DISK_GUID;
    EFI_GUID virtual_disk_guid = EFI_VIRTUAL_DISK_GUID;
    EFI_GUID virtual_cd_guid = EFI_VIRTUAL_CD_GUID;

    while (*load_options != 0) {
        while (*load_options == L' ') { ++load_options; }
        CHAR16* current_option = load_options;
        while (*load_options != L' ' && *load_options != 0) { ++load_options;}
        int was_space = (*load_options == L' ');
        *load_options = 0;
        if (lstreq(L"harddisk", current_option)) {
            BS->CopyMem(&download_type, &virtual_disk_guid, sizeof(download_type));
        } else if (lstreq(L"iso", current_option)) {
            BS->CopyMem(&download_type, &virtual_cd_guid, sizeof(download_type));
        } else if (lstreq(L"pause", current_option)) {
            pause_before_boot = true;
        } else if (lstreq(L"bootonly", current_option)) {
            // The memdisk isn't desired beyond when the OS exits UEFI boot
            // services. If this memoryType doesn't work out, because the OS loader
            // trashes it before exiting boot services, EfiACPIReclaimMemory may work better.
            gMemType = EfiBootServicesData;
            add_table = false;
        } else if (*current_option != 0) {
            print_str(L"unknown argument: ");
            print_str(current_option);
            print_str(L"\r\n");
        }
        if (was_space) {
            ++load_options;
        }
    }

    EFI_ACPI_TABLE_PROTOCOL *acpi_table;
    if (add_table) {
        EFI_GUID acpitableGuid = EFI_ACPI_TABLE_PROTOCOL_GUID;
        Status = BS->LocateProtocol (&acpitableGuid, NULL, (void**)&acpi_table);
        if (EFI_ERROR (Status)) {
            print_str(L"Couldn't open ACPI Table protocol, NVDIMM will not work\r\n");
        } else {
            EFI_GUID acpiSDTGuid = EFI_ACPI_SDT_PROTOCOL_GUID;
            EFI_ACPI_SDT_PROTOCOL *acpi_sdt;
            Status = BS->LocateProtocol (&acpiSDTGuid, NULL, (void**)&acpi_sdt);
            if (EFI_ERROR (Status)) {
                add_table = true;
                print_str(L"Couldn't open ACPI SDT protocol, memdisk_uefi will add NFIT table\r\n");
            } else {
                print_str(L"Relying on MemDiskProtocol to add ACPI NFIT\r\n");
            }
        }
    }

    gDownloading = true;
    IPXE_DOWNLOAD_FILE token;
    
    Status = ipxe_download->Start(ipxe_download, clean_uri, download_data, download_finish, NULL, &token);
    if (EFI_ERROR(Status)) {
        print_str(L"Error starting download\r\n");
        return Status;
    }
    
    print_str(L"Downloading ");
    
    while (gDownloading) {
        ipxe_download->Poll(ipxe_download);
    }
    if (EFI_ERROR(gDownloadStatus)) {
        print_str(L"Download error\r\n");
        return gDownloadStatus;
    } else {
        print_str(L" success\r\n");
    }

    EFI_DEVICE_PATH_PROTOCOL *ram_disk_path;
    Status = ram_disk->Register(gDownloadBuffer, gDownloadSize, &download_type, NULL, &ram_disk_path);
    if (EFI_ERROR(Status)) {
        print_str(L"ram disk register failed\r\n");
        return Status;
    } else {
        print_str(L"ram disk registered: ");
        print_dev(ram_disk_path);
        print_str(L"\r\n");
    }
    
    if (add_table && acpi_table != NULL) {
        print_str(L"Adding ACPI NFIT (nvdimm) table, since RamDiskProtocol won't\r\n");
        setup_nvdimm_table(acpi_table, gDownloadBuffer, gDownloadSize, download_type);
    }
    
    EFI_HANDLE *handles;
    UINTN count = 0;
    
    EFI_GUID simplefsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    Status = BS->LocateHandleBuffer(ByProtocol, &simplefsGuid, NULL, &count, &handles);
    if (EFI_ERROR(Status)) {
        print_str(L"LocateHandle for Simple File System failed\r\n");
    } else {
        for (UINTN i = 0; i < count; ++i) {
            EFI_DEVICE_PATH_PROTOCOL *device_path, *boot_path;
            EFI_GUID devicepathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
            Status = BS->OpenProtocol (handles[i], &devicepathGuid, (void**)&device_path,
                                       ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
            if (EFI_ERROR(Status)) {
                print_str(L"Handle ");
                print_num(i);
                print_str(L": no device path");
                continue;
            } 

            if (!device_path_prefix_match(ram_disk_path, device_path)) {
                continue;
            }

            print_str(L"Handle ");
            print_num(i);
            print_str(L" ");
            print_dev(device_path);

            EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *simple_filesystem;
            Status = BS->OpenProtocol (handles[i], &simplefsGuid, (void**)&simple_filesystem,
                                       ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
            if (EFI_ERROR(Status)) {
                print_str(L" Open protocol error\r\n");
                continue;
            }
            
            EFI_FILE_PROTOCOL *root;
            Status = simple_filesystem->OpenVolume(simple_filesystem, &root);
            if (EFI_ERROR(Status)) {
                print_str(L" Open volume failed\r\n");
                continue;
            }
            
            boot_path = dev_path_utils->AppendDeviceNode(device_path, boot_file_node);
            print_str(L" trying to load\r\n");
            print_dev(boot_path);
            
            EFI_HANDLE boot_image_handle;
            Status = BS->LoadImage(FALSE, ImageHandle, boot_path, NULL, 0, &boot_image_handle);
            if (EFI_ERROR(Status)) {
                print_str(L"\r\nload image from boot_path failed");
                print_num(Status);
                print_str(L"\r\n");
                continue;
            }
            print_str(L"\r\nloaded\r\n");
            if (pause_before_boot) {
                print_str(L"Press a key to boot\r\n");
                Status = ST->ConIn->Reset(ST->ConIn, FALSE);
                if (EFI_ERROR(Status)) {
                    return Status;
                }

                /* Now wait until a key becomes available.  This is a simple
                   polling implementation.  You could try and use the WaitForKey
                   event instead if you like */
                while ((Status = ST->ConIn->ReadKeyStroke(ST->ConIn, &Key)) == EFI_NOT_READY) {
                }
            }

            Status = BS->StartImage(boot_image_handle, 0, NULL);
            if (EFI_ERROR(Status)) {
                print_str(L"StartImage error\r\n");
            } else {
                print_str(L"StartImage success?\r\n");
            }
        }
    }

    return gDownloadStatus;
}

