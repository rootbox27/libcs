#ifndef _ELF_H
#define _ELF_H
#include <stdint.h>
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Section;
#define EI_NIDENT 16
typedef struct {
	unsigned char e_ident[EI_NIDENT];
	Elf64_Half e_type, e_machine;
	Elf64_Word e_version;
	Elf64_Addr e_entry;
	Elf64_Off e_phoff, e_shoff;
	Elf64_Word e_flags;
	Elf64_Half e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;
typedef struct {
	Elf64_Word p_type, p_flags;
	Elf64_Off p_offset;
	Elf64_Addr p_vaddr, p_paddr;
	Elf64_Xword p_filesz, p_memsz, p_align;
} Elf64_Phdr;
typedef struct { Elf64_Sxword d_tag; union { Elf64_Xword d_val; Elf64_Addr d_ptr; } d_un; } Elf64_Dyn;
typedef struct { Elf64_Addr r_offset; Elf64_Xword r_info; Elf64_Sxword r_addend; } Elf64_Rela;
typedef struct { Elf64_Word st_name; unsigned char st_info, st_other; Elf64_Section st_shndx; Elf64_Addr st_value; Elf64_Xword st_size; } Elf64_Sym;
typedef Elf64_Xword Elf64_Relr;
#define ET_EXEC 2
#define ET_DYN 3
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define PT_TLS 7
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK 0x6474e551
#define PT_GNU_RELRO 0x6474e552
#define DT_NULL 0
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_RELRSZ 35
#define DT_RELR 36
#define DT_RELRENT 37
#define ELF64_R_TYPE(i) ((i) & 0xffffffff)
#define ELF64_R_SYM(i) ((i) >> 32)
#define R_X86_64_NONE 0
#define R_X86_64_RELATIVE 8
#endif
