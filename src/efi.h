#ifndef ARGUS_EFI_H
#define ARGUS_EFI_H

#include <stdint.h>
#include <stddef.h>

#if defined(__x86_64__) && !defined(_WIN32)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t UINTN;
typedef uint32_t UINT32;
typedef uint16_t CHAR16;
typedef uint8_t BOOLEAN;
typedef void VOID;

#define EFI_SUCCESS 0
#define EFIERR(code) (0x8000000000000000ULL | (code))
#define EFI_LOAD_ERROR EFIERR(1)
#define EFI_INVALID_PARAMETER EFIERR(2)
#define EFI_BAD_BUFFER_SIZE EFIERR(4)
#define EFI_BUFFER_TOO_SMALL EFIERR(5)
#define EFI_OUT_OF_RESOURCES EFIERR(9)
#define EFI_NOT_FOUND EFIERR(14)
#define EFI_ERROR(x) (((x) & (1ULL << 63)) != 0)

#define EFI_TEXT_BLACK       0x00
#define EFI_TEXT_BLUE        0x01
#define EFI_TEXT_GREEN       0x02
#define EFI_TEXT_CYAN        0x03
#define EFI_TEXT_RED         0x04
#define EFI_TEXT_MAGENTA     0x05
#define EFI_TEXT_BROWN       0x06
#define EFI_TEXT_LIGHTGRAY   0x07
#define EFI_TEXT_DARKGRAY    0x08
#define EFI_TEXT_LIGHTBLUE   0x09
#define EFI_TEXT_LIGHTGREEN  0x0A
#define EFI_TEXT_LIGHTCYAN   0x0B
#define EFI_TEXT_LIGHTRED    0x0C
#define EFI_TEXT_LIGHTMAGENTA 0x0D
#define EFI_TEXT_YELLOW      0x0E
#define EFI_TEXT_WHITE       0x0F

#define EFI_RESET_COLD      0
#define EFI_RESET_WARM      1
#define EFI_RESET_SHUTDOWN  2

#define AllocateAnyPages 0

#define EfiReservedMemoryType      0
#define EfiLoaderCode              1
#define EfiLoaderData              2
#define EfiBootServicesCode        3
#define EfiBootServicesData        4
#define EfiRuntimeServicesCode     5
#define EfiRuntimeServicesData     6
#define EfiConventionalMemory      7
#define EfiUnusableMemory          8
#define EfiACPIReclaimMemory       9
#define EfiACPIMemoryNVS          10
#define EfiMemoryMappedIO         11
#define EfiMemoryMappedIOPortSpace 12
#define EfiPalCode                13
#define EfiPersistentMemory       14
#define EfiUnacceptedMemoryType   15

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} EFI_GUID;

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    uint16_t ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_BOOT_SERVICES;
struct EFI_RUNTIME_SERVICES;

typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, BOOLEAN);
typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, EFI_INPUT_KEY *);

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_INPUT_RESET Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    EFI_EVENT WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, BOOLEAN);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_TEST_STRING)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_QUERY_MODE)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN, UINTN *, UINTN *);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_MODE)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_ENABLE_CURSOR)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, BOOLEAN);

typedef struct {
    int32_t MaxMode;
    int32_t Mode;
    int32_t Attribute;
    int32_t CursorColumn;
    int32_t CursorRow;
    BOOLEAN CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET Reset;
    EFI_TEXT_STRING OutputString;
    EFI_TEXT_TEST_STRING TestString;
    EFI_TEXT_QUERY_MODE QueryMode;
    EFI_TEXT_SET_MODE SetMode;
    EFI_TEXT_SET_ATTRIBUTE SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION SetCursorPosition;
    EFI_TEXT_ENABLE_CURSOR EnableCursor;
    SIMPLE_TEXT_OUTPUT_MODE *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    uint16_t Year;
    uint8_t Month;
    uint8_t Day;
    uint8_t Hour;
    uint8_t Minute;
    uint8_t Second;
    uint8_t Pad1;
    uint32_t Nanosecond;
    int16_t TimeZone;
    uint8_t Daylight;
    uint8_t Pad2;
} EFI_TIME;

typedef struct {
    uint32_t Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_PHYSICAL_ADDRESS VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *, UINTN *, uint32_t *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(uint32_t, uint32_t, UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(uint32_t, UINTN, VOID **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(VOID *);
typedef EFI_STATUS (EFIAPI *EFI_WAIT_FOR_EVENT)(UINTN, EFI_EVENT *, UINTN *);
typedef EFI_STATUS (EFIAPI *EFI_STALL)(UINTN);
typedef EFI_STATUS (EFIAPI *EFI_SET_WATCHDOG_TIMER)(UINTN, uint64_t, UINTN, CHAR16 *);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE, EFI_GUID *, VOID **);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, VOID *, VOID **);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);

typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void *CreateEvent;
    void *SetTimer;
    EFI_WAIT_FOR_EVENT WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *GetNextMonotonicCount;
    EFI_STALL Stall;
    EFI_SET_WATCHDOG_TIMER SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef EFI_STATUS (EFIAPI *EFI_GET_TIME)(EFI_TIME *, void *);
typedef void (EFIAPI *EFI_RESET_SYSTEM)(uint32_t, EFI_STATUS, UINTN, void *);

typedef struct EFI_RUNTIME_SERVICES {
    EFI_TABLE_HEADER Hdr;
    EFI_GET_TIME GetTime;
    void *SetTime;
    void *GetWakeupTime;
    void *SetWakeupTime;
    void *SetVirtualAddressMap;
    void *ConvertPointer;
    void *GetVariable;
    void *GetNextVariableName;
    void *SetVariable;
    void *GetNextHighMonotonicCount;
    EFI_RESET_SYSTEM ResetSystem;
    void *UpdateCapsule;
    void *QueryCapsuleCapabilities;
    void *QueryVariableInfo;
} EFI_RUNTIME_SERVICES;

typedef struct {
    EFI_GUID VendorGuid;
    VOID *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    VOID *FilePath;
    VOID *Reserved;
    uint32_t LoadOptionsSize;
    VOID *LoadOptions;
    VOID *ImageBase;
    uint64_t ImageSize;
    uint32_t ImageCodeType;
    uint32_t ImageDataType;
    VOID *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    {0x5b1b31a1, 0x9562, 0x11d2, {0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

#define EFI_ACPI_TABLE_GUID \
    {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}}

#define EFI_ACPI_20_TABLE_GUID \
    {0x8868e871, 0xe4f1, 0x11d3, {0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}}

/* UEFI Graphics Output Protocol (GOP) */
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct {
    uint32_t RedMask;
    uint32_t GreenMask;
    uint32_t BlueMask;
    uint32_t ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3,
    PixelFormatMax = 4
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *, uint32_t, UINTN *, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *, uint32_t);
typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *, void *, uint32_t,
    UINTN, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN);

typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE SetMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    {0x9042a9de, 0x23dc, 0x4a38, {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}}

#endif
