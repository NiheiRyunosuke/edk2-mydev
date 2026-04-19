#include  <Uefi.h>
#include  <Library/UefiLib.h>
#include  <Library/UefiBootServicesTableLib.h>
#include  <Library/PrintLib.h>
#include  <Library/BaseMemoryLib.h>
#include  <Protocol/LoadedImage.h>
#include  <Protocol/SimpleFileSystem.h>
#include  <Protocol/DiskIo2.h>
#include  <Protocol/BlockIo.h>
#include  <Guid/FileInfo.h>
#include  "frame_buffer_config.hpp"

struct MemoryMap {
  UINTN buffer_size;
  VOID* buffer;
  UINTN map_size;
  UINTN map_key;
  UINTN descriptor_size;
  UINT32 descriptor_version;
};

EFI_STATUS GetMemoryMap(struct MemoryMap* map) {
  if (map->buffer == NULL) {
    return EFI_BUFFER_TOO_SMALL;
  }

  map->map_size = map->buffer_size;
  return gBS->GetMemoryMap(
      &map->map_size,
      (EFI_MEMORY_DESCRIPTOR*)map->buffer,
      &map->map_key,
      &map->descriptor_size,
      &map->descriptor_version);
}

const CHAR16* GetMemoryTypeUnicode(EFI_MEMORY_TYPE type) {
  switch (type) {
    case EfiReservedMemoryType: return L"EfiReservedMemoryType";
    case EfiLoaderCode: return L"EfiLoaderCode";
    case EfiLoaderData: return L"EfiLoaderData";
    case EfiBootServicesCode: return L"EfiBootServicesCode";
    case EfiBootServicesData: return L"EfiBootServicesData";
    case EfiRuntimeServicesCode: return L"EfiRuntimeServicesCode";
    case EfiRuntimeServicesData: return L"EfiRuntimeServicesData";
    case EfiConventionalMemory: return L"EfiConventionalMemory";
    case EfiUnusableMemory: return L"EfiUnusableMemory";
    case EfiACPIReclaimMemory: return L"EfiACPIReclaimMemory";
    case EfiACPIMemoryNVS: return L"EfiACPIMemoryNVS";
    case EfiMemoryMappedIO: return L"EfiMemoryMappedIO";
    case EfiMemoryMappedIOPortSpace: return L"EfiMemoryMappedIOPortSpace";
    case EfiPalCode: return L"EfiPalCode";
    case EfiPersistentMemory: return L"EfiPersistentMemory";
    case EfiMaxMemoryType: return L"EfiMaxMemoryType";
    default: return L"InvalidMemoryType";
  }
}

EFI_STATUS SaveMemoryMap(struct MemoryMap* map, EFI_FILE_PROTOCOL* file) {
  CHAR8 buf[256];
  UINTN len;

  CHAR8* header =
    "Index, Type, Type(name), PhysicalStart, NumberOfPages, Attribute\n";
  len = AsciiStrLen(header);
  file->Write(file, &len, header);

  Print(L"map->buffer = %08lx, map->map_size = %08lx\n",
      map->buffer, map->map_size);

  EFI_PHYSICAL_ADDRESS iter;
  int i;
  for (iter = (EFI_PHYSICAL_ADDRESS)map->buffer, i = 0;
       iter < (EFI_PHYSICAL_ADDRESS)map->buffer + map->map_size;
       iter += map->descriptor_size, i++) {
    EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)iter;
    len = AsciiSPrint(
        buf, sizeof(buf),
        "%u, %x, %-ls, %08lx, %lx, %lx\n",
        i, desc->Type, GetMemoryTypeUnicode(desc->Type),
        desc->PhysicalStart, desc->NumberOfPages,
        desc->Attribute & 0xffffflu);
    file->Write(file, &len, buf);
  }

  return EFI_SUCCESS;
}

EFI_STATUS OpenRootDir(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL** root) {
  EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;

  gBS->OpenProtocol(
      image_handle,
      &gEfiLoadedImageProtocolGuid,
      (VOID**)&loaded_image,
      image_handle,
      NULL,
      EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

  gBS->OpenProtocol(
      loaded_image->DeviceHandle,
      &gEfiSimpleFileSystemProtocolGuid,
      (VOID**)&fs,
      image_handle,
      NULL,
      EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

  fs->OpenVolume(fs, root);

  return EFI_SUCCESS;
}

void Halt(void) {
  while (1) __asm__("hlt");
}

/* -------------------------
 * Minimal ELF64 definitions
 * ------------------------- */
typedef struct {
  unsigned char e_ident[16];
  UINT16 e_type;
  UINT16 e_machine;
  UINT32 e_version;
  UINT64 e_entry;
  UINT64 e_phoff;
  UINT64 e_shoff;
  UINT32 e_flags;
  UINT16 e_ehsize;
  UINT16 e_phentsize;
  UINT16 e_phnum;
  UINT16 e_shentsize;
  UINT16 e_shnum;
  UINT16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
  UINT32 p_type;
  UINT32 p_flags;
  UINT64 p_offset;
  UINT64 p_vaddr;
  UINT64 p_paddr;
  UINT64 p_filesz;
  UINT64 p_memsz;
  UINT64 p_align;
} Elf64_Phdr;

#define PT_LOAD 1


static EFI_STATUS ReadFile(
    EFI_FILE_PROTOCOL* file, UINT64 offset, UINTN size, VOID* buffer) {
  EFI_STATUS status = file->SetPosition(file, offset);
  if (EFI_ERROR(status)) {
    return status;
  }
  UINTN read_size = size;
  return file->Read(file, &read_size, buffer);
}

static EFI_STATUS LoadKernelELF(
    EFI_FILE_PROTOCOL* kernel_file, UINT64* entry_addr) {
  EFI_STATUS status;

  Elf64_Ehdr ehdr;
  status = ReadFile(kernel_file, 0, sizeof(ehdr), &ehdr);
  if (EFI_ERROR(status)) {
    Print(L"Failed to read ELF header: %r\n", status);
    return status;
  }

  if (!(ehdr.e_ident[0] == 0x7f &&
        ehdr.e_ident[1] == 'E' &&
        ehdr.e_ident[2] == 'L' &&
        ehdr.e_ident[3] == 'F')) {
    Print(L"Kernel file is not ELF\n");
    return EFI_LOAD_ERROR;
  }

  if (ehdr.e_phentsize != sizeof(Elf64_Phdr)) {
    Print(L"Unexpected program header size: %u\n", ehdr.e_phentsize);
    return EFI_LOAD_ERROR;
  }

  Elf64_Phdr phdrs[16];
  if (ehdr.e_phnum > 16) {
    Print(L"Too many program headers: %u\n", ehdr.e_phnum);
    return EFI_LOAD_ERROR;
  }

  status = ReadFile(
      kernel_file, ehdr.e_phoff,
      ehdr.e_phnum * sizeof(Elf64_Phdr), phdrs);
  if (EFI_ERROR(status)) {
    Print(L"Failed to read program headers: %r\n", status);
    return status;
  }

  for (UINTN i = 0; i < ehdr.e_phnum; ++i) {
    if (phdrs[i].p_type != PT_LOAD) {
      continue;
    }

    UINT64 segm_first = phdrs[i].p_vaddr & 0xfffffffffffff000ULL;
    UINT64 segm_last  = (phdrs[i].p_vaddr + phdrs[i].p_memsz + 0xfff) &
                        0xfffffffffffff000ULL;
    UINTN num_pages = (segm_last - segm_first) / 0x1000;

    EFI_PHYSICAL_ADDRESS alloc_addr = segm_first;
    status = gBS->AllocatePages(
        AllocateAddress, EfiLoaderData, num_pages, &alloc_addr);
    if (EFI_ERROR(status)) {
      Print(L"Failed to allocate pages for segment %u: %r\n", i, status);
      return status;
    }

    SetMem((VOID*)segm_first, num_pages * 0x1000, 0);

    status = ReadFile(
        kernel_file, phdrs[i].p_offset,
        (UINTN)phdrs[i].p_filesz, (VOID*)phdrs[i].p_vaddr);
    if (EFI_ERROR(status)) {
      Print(L"Failed to read segment %u: %r\n", i, status);
      return status;
    }

    Print(
        L"Loaded segment %u: offset=0x%lx vaddr=0x%lx filesz=0x%lx memsz=0x%lx\n",
        i, phdrs[i].p_offset, phdrs[i].p_vaddr,
        phdrs[i].p_filesz, phdrs[i].p_memsz);
  }

  *entry_addr = ehdr.e_entry;
  return EFI_SUCCESS;
}



void CalcLoadAddressRange(Elf64_Ehdr* ehdr, UINT64* first, UINT64* last) {
  Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
  *first = MAX_UINT64;
  *last = 0;
  for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
    if (phdr[i].p_type != PT_LOAD) continue;
    *first = MIN(*first, phdr[i].p_vaddr);
    *last = MAX(*last, phdr[i].p_vaddr + phdr[i].p_memsz);
  }
}

void CopyLoadSegments(Elf64_Ehdr* ehdr) {
  Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
  for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
    if (phdr[i].p_type != PT_LOAD) continue;

    UINT64 segm_in_file = (UINT64)ehdr + phdr[i].p_offset;
    CopyMem((VOID*)phdr[i].p_vaddr, (VOID*)segm_in_file, phdr[i].p_filesz);

    UINTN remain_bytes = phdr[i].p_memsz - phdr[i].p_filesz;
    SetMem((VOID*)(phdr[i].p_vaddr + phdr[i].p_filesz), remain_bytes, 0);
  }
}

UINT64 entry_addr = *(UINT64*)(kernel_first_addr + 24);

CopyLoadSegments(kernel_ehdr);
Print(L"Kernel: 0x%0lx - 0x%0lx\n", kernel_first_addr, kernel_last_addr);

status = gBS->FreePool(kernel_buffer);
if (EFI_ERROR(status)){
  Print(L"failed to free pool: %r\n", status);
  Halt();
}


EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
UINTN kernel_file_size = file_info->FileSize;

VOID* kernel_buffer;
status = gBS->AllocatePool(EfiLoaderData, kernel_file_size, &kernel_buffer);
if (EFI_ERROR(status)) {
  Print(L"failed to allocate pool: %r\n", status);
  Halt();
}
status = kernel_file->Read(kernel_file, &kernel_file_size, kernel_buffer);
if (EFI_ERROR(status)) {
  Print(L"error: %r", status);
  Halt();
}


Elf64_Ehdr* kernel_ehdr = (Elf64_Ehdr*)kernel_buffer;
UINT64 kernel_first_addr, kernel_last_addr;
CalcLoadAddressRange(kernel_ehdr, &kernel_first_addr, &kernel_last_addr);

UINTN num_pages = (kernel_last_addr - kernel_first_addr + 0xfff) / 0x1000;
status = gBS ->AllocatePages(AllocateAddress, EfiLoaderData, num_pages, &kernel_first_addr);
if (EFI_ERROR(status)) {
  Print(L"failed to allocate pages: %r\n", status);
  Halt();
}


EFI_STATUS EFIAPI UefiMain(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE* system_table) {
  Print(L"Hello, Mikan World!\n");

  CHAR8 memmap_buf[4096 * 8];
  struct MemoryMap memmap = {sizeof(memmap_buf), memmap_buf, 0, 0, 0, 0};
  EFI_STATUS status = GetMemoryMap(&memmap);

  EFI_FILE_PROTOCOL* root_dir;
  OpenRootDir(image_handle, &root_dir);

  EFI_FILE_PROTOCOL* memmap_file;
  root_dir->Open(
      root_dir, &memmap_file, L"\\memmap",
      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  SaveMemoryMap(&memmap, memmap_file);
  memmap_file->Close(memmap_file);

  EFI_FILE_PROTOCOL* kernel_file;
  status = root_dir->Open(
      root_dir, &kernel_file, L"\\kernel.elf",
      EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(status)) {
    Print(L"Could not open kernel.elf: %r\n", status);
    Halt();
  }

  UINT64 entry_addr;
  status = LoadKernelELF(kernel_file, &entry_addr);
  if (EFI_ERROR(status)) {
    Print(L"Failed to load kernel ELF: %r\n", status);
    Halt();
  }
  kernel_file->Close(kernel_file);

  typedef void __attribute__((ms_abi)) EntryPointType(
      const struct FrameBufferConfig*);
  EntryPointType* entry_point = (EntryPointType*)entry_addr;
  Print(L"Jumping to Kernel at: 0x%lx\n", entry_addr);

  EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
  status = gBS->LocateProtocol(
      &gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&gop);
  if (EFI_ERROR(status)) {
    Print(L"Failed to locate GOP: %r\n", status);
    Halt();
  }

  struct FrameBufferConfig config = {
    (UINT8*)gop->Mode->FrameBufferBase,
    gop->Mode->Info->PixelsPerScanLine,
    gop->Mode->Info->HorizontalResolution,
    gop->Mode->Info->VerticalResolution,
    0
  };

  switch (gop->Mode->Info->PixelFormat) {
    case PixelRedGreenBlueReserved8BitPerColor:
      config.pixel_format = kPixelRGBResv8BitPerColor;
      break;
    case PixelBlueGreenRedReserved8BitPerColor:
      config.pixel_format = kPixelBGRResv8BitPerColor;
      break;
    default:
      Print(L"Unimplemented pixel format: %d\n", gop->Mode->Info->PixelFormat);
      Halt();
  }

  status = GetMemoryMap(&memmap);
  if (EFI_ERROR(status)) {
    Print(L"failed to get memory map: %r\n", status);
    Halt();
  }

  status = gBS->ExitBootServices(image_handle, memmap.map_key);
  if (EFI_ERROR(status)) {
    status = GetMemoryMap(&memmap);
    if (EFI_ERROR(status)) {
      Print(L"failed to get memory map for retry: %r\n", status);
      Halt();
    }

    status = gBS->ExitBootServices(image_handle, memmap.map_key);
    if (EFI_ERROR(status)) {
      Print(L"Could not exit boot service: %r\n", status);
      Halt();
    }
  }

  entry_point(&config);

  while (1);
  return EFI_SUCCESS;
}