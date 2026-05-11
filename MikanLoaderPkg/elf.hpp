#pragma once
#include <stdint.h>

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;

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