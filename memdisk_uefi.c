// Copyright 2025 Richard Russo
// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <IndustryStandard/Acpi61.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DevicePathToText.h>
#include <Protocol/DevicePathUtilities.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/RamDisk.h>
#include <Protocol/SimpleFileSystem.h>

#define FILE_LICENCE(X)
#include <ipxe/efi/efi_download.h>

#include "RamDisk.hex"

bool gDownloading = false;
UINTN gDownloadSize = 0;
UINTN gDownloadProgress = 0;
UINTN gDownloadProgressAmount = 0;
EFI_GUID gDownloadType = EFI_VIRTUAL_DISK_GUID; // ??? EFI_VIRTUAL_CD_GUID;


EFI_PHYSICAL_ADDRESS gDownloadBuffer;
EFI_STATUS gDownloadStatus = EFI_SUCCESS;
EFI_SYSTEM_TABLE *ST;
EFI_BOOT_SERVICES *BS;

void * memset(void *dest, int c, size_t len) {
    BS->SetMem(dest, len, c);
    return dest;
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
        status = BS->AllocatePages(AllocateAnyPages, EfiReservedMemoryType, pages, &gDownloadBuffer);
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

// CalculateSum8 and CalculateCheckSum8 taken from edk2/MdePkg/Library/BaseLib/CheckSum.c
//  Copyright (c) 2007 - 2018, Intel Corporation. All rights reserved.
//  Copyright (c) 2022, Pedro Falcato. All rights reserved.<BR>
//  SPDX-License-Identifier: BSD-2-Clause-Patent
UINT8
EFIAPI
CalculateSum8 (
  IN      CONST UINT8  *Buffer,
  IN      UINTN        Length
  )
{
  UINT8  Sum;
  UINTN  Count;

  for (Sum = 0, Count = 0; Count < Length; Count++) {
    Sum = (UINT8)(Sum + *(Buffer + Count));
  }

  return Sum;
}
UINT8
EFIAPI
CalculateCheckSum8 (
  IN      CONST UINT8  *Buffer,
  IN      UINTN        Length
  )
{
  UINT8  CheckSum;

  CheckSum = CalculateSum8 (Buffer, Length);

  //
  // Return the checksum based on 2's complement.
  //
  return (UINT8)(0x100 - CheckSum);
}


// This function purloined from edk2/MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskProtocol.c
// Mostly taken from RamDiskPublishNfit and RamDiskPublishSsdt
//   Copyright (c) 2016 - 2019, Intel Corporation. All rights reserved.
//  (C) Copyright 2016 Hewlett Packard Enterprise Development LP
//   Copyright (c) Microsoft Corporation.
//  SPDX-License-Identifier: BSD-2-Clause-Patent

void setup_nvdimm_table(EFI_ACPI_TABLE_PROTOCOL *acpi_table) {
    // The System Descriptor Table Protocol is part of the Platform Initialization (PI) spec
    // it's not available by the time we're running, so we're just going to skip that and assume
    // there's no NVDIMM root device.
    
    UINTN TableKey;
    EFI_STATUS Status;

    // Add SSDT for NVDIMM root device
    Status = acpi_table->InstallAcpiTable (acpi_table,
                                           ramdisk_aml_code, sizeof(ramdisk_aml_code),
                                           &TableKey);
    if (EFI_ERROR(Status)) {
        print_str(L"Couldn't add SSDT table\r\n");
    } else {
        print_str(L"SSDT Table added!\r\n");
    }

    EFI_ACPI_DESCRIPTION_HEADER *NfitHeader;
    EFI_ACPI_6_1_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE *SpaRange;
    VOID *Nfit;
    UINT32 NfitLen;
    UINT64 CurrentData;
    UINT8 Checksum;
    
    NfitLen = sizeof (EFI_ACPI_6_1_NVDIMM_FIRMWARE_INTERFACE_TABLE) +
              sizeof (EFI_ACPI_6_1_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE);
    Status = BS->AllocatePool (EfiACPIReclaimMemory, NfitLen, &Nfit);
    if (EFI_ERROR(Status)) {
        print_str(L"setup_nvdimm_table: couldn't allocate\r\n");
        return;
    }
    BS->SetMem(Nfit, NfitLen, 0);

    SpaRange = (EFI_ACPI_6_1_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE *)
               ((UINT8 *)Nfit + sizeof (EFI_ACPI_6_1_NVDIMM_FIRMWARE_INTERFACE_TABLE));

    NfitHeader                  = (EFI_ACPI_DESCRIPTION_HEADER *)Nfit;
    NfitHeader->Signature       = EFI_ACPI_6_1_NVDIMM_FIRMWARE_INTERFACE_TABLE_STRUCTURE_SIGNATURE;
    NfitHeader->Length          = NfitLen;
    NfitHeader->Revision        = EFI_ACPI_6_1_NVDIMM_FIRMWARE_INTERFACE_TABLE_REVISION;
    NfitHeader->Checksum        = 0;
    NfitHeader->OemRevision     = 0;
    NfitHeader->CreatorId       = 0;
    NfitHeader->CreatorRevision = 0;
    CurrentData                 = 0x204b5349444d454d; // "MEMDISK "
    BS->CopyMem (NfitHeader->OemId, "MEMDSK", sizeof (NfitHeader->OemId));
    BS->CopyMem (&NfitHeader->OemTableId, &CurrentData, sizeof (UINT64));

    //
    // Fill in the content of the SPA Range Structure.
    //
    SpaRange->Type                             = EFI_ACPI_6_1_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE_TYPE;
    SpaRange->Length                           = sizeof (EFI_ACPI_6_1_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE);
    
    SpaRange->SystemPhysicalAddressRangeBase   = gDownloadBuffer;
    SpaRange->SystemPhysicalAddressRangeLength = gDownloadSize;
    BS->CopyMem(&SpaRange->AddressRangeTypeGUID, &gDownloadType, sizeof(EFI_GUID));
    SpaRange->AddressRangeMemoryMappingAttribute = EFI_MEMORY_WB;

    Checksum             = CalculateCheckSum8 ((UINT8 *)Nfit, NfitHeader->Length);
    NfitHeader->Checksum = Checksum;
    
    //
    // Publish the NFIT to the ACPI table.
    // Note, since the NFIT might be modified by other driver, therefore, we
    // do not track the returning TableKey from the InstallAcpiTable().
    //
    Status = acpi_table->InstallAcpiTable (acpi_table,
                                           Nfit, NfitHeader->Length,
                                           &TableKey);
    if (EFI_ERROR(Status)) {
        print_str(L"Couldn't add NFIT table\r\n");
    } else {
        print_str(L"NFIT Table added!\r\n");
    }
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


    int add_table = 0;
    EFI_GUID acpitableGuid = EFI_ACPI_TABLE_PROTOCOL_GUID;
    EFI_ACPI_TABLE_PROTOCOL *acpi_table;
    Status = BS->LocateProtocol (&acpitableGuid, NULL, (void**)&acpi_table);
    if (EFI_ERROR (Status)) {
        print_str(L"Couldn't open ACPI Table protocol, NVDIMM will not work\r\n");
    } else {
        EFI_GUID acpiSDTGuid = EFI_ACPI_SDT_PROTOCOL_GUID;
        EFI_ACPI_SDT_PROTOCOL *acpi_sdt;
        Status = BS->LocateProtocol (&acpiSDTGuid, NULL, (void**)&acpi_sdt);
        if (EFI_ERROR (Status)) {
            add_table = 1;
            print_str(L"Couldn't open ACPI SDT protocol, memdisk_uefi will add NFIT table\r\n");
        } else {
            print_str(L"Relying on MemDiskProtocol to add ACPI NFIT\r\n");
        }
    }

    CHAR16* uri = loaded_image->LoadOptions;
    if (uri == NULL) {
        print_str(L"image LoadOptions is null\r\n");
        return -1;
    }
    
    if (uri[loaded_image->LoadOptionsSize / sizeof(uri[0]) -1] != 0) {
        print_str(L"image LoadOptions is not null terminated\r\n");
        return -1;
    }
    
    while (*uri != L' ' && *uri != 0)  {
      ++uri;
    }
    if (*uri == L' ') {
        ++uri;
    } else {
        print_str(L"Did not find uri; sad sad\r\n");
        return -1;
    }
    
    CHAR8 clean_uri[256] = {0};
    size_t i = 0;
    
    while (*uri != L' ' && *uri != 0 && i < sizeof(clean_uri) - 1) {
        if (*uri > L' ' && *uri <= L'~' && (*uri & ~0x7f) == 0) {
            clean_uri[i] = *uri & 0x7f;
        } else {
            print_str(L"invalid character in uri\r\n");
            return -1;
        }
        ++uri;
        ++i;
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
    EFI_GUID virtualdiskGuid = EFI_VIRTUAL_DISK_GUID;
//    EFI_GUID virtualdiskGuid = EFI_VIRTUAL_CD_GUID;
    Status = ram_disk->Register(gDownloadBuffer, gDownloadSize, &gDownloadType, NULL, &ram_disk_path);
    if (EFI_ERROR(Status)) {
        print_str(L"ram disk register failed\r\n");
        return Status;
    } else {
        print_str(L"ram disk registered: ");
        print_dev(ram_disk_path);
        print_str(L"\r\n");
    }
    
    if (add_table) {
        print_str(L"Adding ACPI NFIT (nvdimm) table, since RamDiskProtocol won't\r\n");
        setup_nvdimm_table(acpi_table);
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
            Status = BS->StartImage(boot_image_handle, 0, NULL);
            if (EFI_ERROR(Status)) {
                print_str(L"StartImage error\r\n");
            } else {
                print_str(L"StartImage success?\r\n");
            }
        }
    }


    Status = ST->ConIn->Reset(ST->ConIn, FALSE);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    
    /* Now wait until a key becomes available.  This is a simple
       polling implementation.  You could try and use the WaitForKey
       event instead if you like */
    while ((Status = ST->ConIn->ReadKeyStroke(ST->ConIn, &Key)) == EFI_NOT_READY) {
    
    }
    return gDownloadStatus;
}

