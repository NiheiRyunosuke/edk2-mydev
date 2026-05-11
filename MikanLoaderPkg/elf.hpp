#pragma once
#include <stdint.h>

using Elf64_Addr  = uint64_t;
using Elf64_Off   = uint64_t;
using Elf64_Half  = uint16_t;
using Elf64_Word  = uint32_t;
using Elf64_Xword = uint64_t;

#define EI_NIDENT 16
#define PT_LOAD 1


typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;
    Elf64_Off     e_phoff;
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
}   Elf64_Ehdr;

typedef struct {
    Elf64_Word  p_type;   // PHDR, LOAD などのセグメント種別
    Elf64_Word  p_flags;  // フラグ
    Elf64_Off   p_offset; // オフセット
    Elf64_Addr  p_vaddr;  // 仮想Addr
    Elf64_Addr  p_paddr;  // 物理Addr
    Elf64_Xword p_filesz; // ファイルサイズ
    Elf64_Xword p_memsz;  // メモリサイズ
    Elf64_Xword p_align;
}   Elf64_Phdr;