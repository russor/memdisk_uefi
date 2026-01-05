#include <Uefi.h>
#include <IndustryStandard/Acpi61.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Protocol/AcpiTable.h>

void setup_nvdimm_table(EFI_ACPI_TABLE_PROTOCOL *acpi_table, EFI_PHYSICAL_ADDRESS DownloadBuffer, UINTN DownloadSize, EFI_GUID DownloadType);
EFI_STATUS print_str(CHAR16 * str);

extern EFI_BOOT_SERVICES *BS;
