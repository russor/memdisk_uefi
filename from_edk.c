#include "memdisk.h"
#include "RamDisk.hex"

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


// This function taken from edk2/MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskProtocol.c
// Mostly taken from RamDiskPublishNfit and RamDiskPublishSsdt, and adjusted to suit the Memdisk Environment
//   Copyright (c) 2016 - 2019, Intel Corporation. All rights reserved.
//  (C) Copyright 2016 Hewlett Packard Enterprise Development LP
//   Copyright (c) Microsoft Corporation.
//  SPDX-License-Identifier: BSD-2-Clause-Patent

void setup_nvdimm_table(EFI_ACPI_TABLE_PROTOCOL *acpi_table,
    EFI_PHYSICAL_ADDRESS DownloadBuffer, UINTN DownloadSize, EFI_GUID DownloadType) {
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
    
    SpaRange->SystemPhysicalAddressRangeBase   = DownloadBuffer;
    SpaRange->SystemPhysicalAddressRangeLength = DownloadSize;
    BS->CopyMem(&SpaRange->AddressRangeTypeGUID, &DownloadType, sizeof(EFI_GUID));
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
