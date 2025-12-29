/*
 * Ocean Kernel - ELF Executable Format
 *
 * Definitions for loading ELF64 executables.
 */

#ifndef _OCEAN_ELF_H
#define _OCEAN_ELF_H

#include <ocean/types.h>

/*
 * ELF64 Header
 */
typedef struct {
    u8  e_ident[16];        /* ELF identification */
    u16 e_type;             /* Object file type */
    u16 e_machine;          /* Machine type */
    u32 e_version;          /* Object file version */
    u64 e_entry;            /* Entry point address */
    u64 e_phoff;            /* Program header offset */
    u64 e_shoff;            /* Section header offset */
    u32 e_flags;            /* Processor-specific flags */
    u16 e_ehsize;           /* ELF header size */
    u16 e_phentsize;        /* Size of program header entry */
    u16 e_phnum;            /* Number of program header entries */
    u16 e_shentsize;        /* Size of section header entry */
    u16 e_shnum;            /* Number of section header entries */
    u16 e_shstrndx;         /* Section name string table index */
} Elf64_Ehdr;

/*
 * ELF64 Program Header
 */
typedef struct {
    u32 p_type;             /* Segment type */
    u32 p_flags;            /* Segment flags */
    u64 p_offset;           /* Offset in file */
    u64 p_vaddr;            /* Virtual address */
    u64 p_paddr;            /* Physical address */
    u64 p_filesz;           /* Size in file */
    u64 p_memsz;            /* Size in memory */
    u64 p_align;            /* Alignment */
} Elf64_Phdr;

/*
 * ELF64 Section Header
 */
typedef struct {
    u32 sh_name;            /* Section name (string table index) */
    u32 sh_type;            /* Section type */
    u64 sh_flags;           /* Section flags */
    u64 sh_addr;            /* Virtual address */
    u64 sh_offset;          /* Offset in file */
    u64 sh_size;            /* Section size */
    u32 sh_link;            /* Link to another section */
    u32 sh_info;            /* Additional info */
    u64 sh_addralign;       /* Alignment */
    u64 sh_entsize;         /* Entry size if table */
} Elf64_Shdr;

/*
 * ELF Identification Indices
 */
#define EI_MAG0         0   /* Magic number byte 0 */
#define EI_MAG1         1   /* Magic number byte 1 */
#define EI_MAG2         2   /* Magic number byte 2 */
#define EI_MAG3         3   /* Magic number byte 3 */
#define EI_CLASS        4   /* File class */
#define EI_DATA         5   /* Data encoding */
#define EI_VERSION      6   /* File version */
#define EI_OSABI        7   /* OS/ABI identification */
#define EI_ABIVERSION   8   /* ABI version */
#define EI_PAD          9   /* Start of padding bytes */
#define EI_NIDENT       16  /* Size of e_ident[] */

/*
 * ELF Magic Number
 */
#define ELFMAG0         0x7f
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'

/*
 * ELF Class (32/64 bit)
 */
#define ELFCLASSNONE    0   /* Invalid class */
#define ELFCLASS32      1   /* 32-bit objects */
#define ELFCLASS64      2   /* 64-bit objects */

/*
 * ELF Data Encoding
 */
#define ELFDATANONE     0   /* Invalid encoding */
#define ELFDATA2LSB     1   /* Little-endian */
#define ELFDATA2MSB     2   /* Big-endian */

/*
 * ELF Object File Types
 */
#define ET_NONE         0   /* No file type */
#define ET_REL          1   /* Relocatable file */
#define ET_EXEC         2   /* Executable file */
#define ET_DYN          3   /* Shared object file */
#define ET_CORE         4   /* Core file */

/*
 * ELF Machine Types
 */
#define EM_NONE         0   /* No machine */
#define EM_386          3   /* Intel 80386 */
#define EM_X86_64       62  /* AMD x86-64 */

/*
 * Program Header Types
 */
#define PT_NULL         0   /* Unused */
#define PT_LOAD         1   /* Loadable segment */
#define PT_DYNAMIC      2   /* Dynamic linking info */
#define PT_INTERP       3   /* Interpreter path */
#define PT_NOTE         4   /* Auxiliary info */
#define PT_SHLIB        5   /* Reserved */
#define PT_PHDR         6   /* Program header table */
#define PT_TLS          7   /* Thread-local storage */
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552

/*
 * Program Header Flags
 */
#define PF_X            0x1 /* Executable */
#define PF_W            0x2 /* Writable */
#define PF_R            0x4 /* Readable */

/*
 * Section Types
 */
#define SHT_NULL        0   /* Unused */
#define SHT_PROGBITS    1   /* Program data */
#define SHT_SYMTAB      2   /* Symbol table */
#define SHT_STRTAB      3   /* String table */
#define SHT_RELA        4   /* Relocation with addend */
#define SHT_HASH        5   /* Symbol hash table */
#define SHT_DYNAMIC     6   /* Dynamic linking info */
#define SHT_NOTE        7   /* Notes */
#define SHT_NOBITS      8   /* Uninitialized data (BSS) */
#define SHT_REL         9   /* Relocation without addend */

/*
 * Section Flags
 */
#define SHF_WRITE       0x1     /* Writable */
#define SHF_ALLOC       0x2     /* Occupies memory */
#define SHF_EXECINSTR   0x4     /* Executable */

/*
 * ELF validation result
 */
#define ELF_OK              0
#define ELF_ERR_MAGIC       -1  /* Bad magic number */
#define ELF_ERR_CLASS       -2  /* Not 64-bit */
#define ELF_ERR_ENDIAN      -3  /* Not little-endian */
#define ELF_ERR_TYPE        -4  /* Not executable */
#define ELF_ERR_MACHINE     -5  /* Not x86_64 */

/*
 * Validate an ELF header
 */
static inline int elf_validate(const Elf64_Ehdr *ehdr)
{
    /* Check magic number */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return ELF_ERR_MAGIC;
    }

    /* Check class (64-bit) */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        return ELF_ERR_CLASS;
    }

    /* Check endianness (little-endian) */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return ELF_ERR_ENDIAN;
    }

    /* Check type (executable) */
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        return ELF_ERR_TYPE;
    }

    /* Check machine (x86_64) */
    if (ehdr->e_machine != EM_X86_64) {
        return ELF_ERR_MACHINE;
    }

    return ELF_OK;
}

#endif /* _OCEAN_ELF_H */
