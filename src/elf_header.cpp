#include "binscope/elf_header.hpp"

#include "binscope/byte_reader.hpp"
#include "binscope/elf_constants.hpp"

namespace binscope {

using namespace elf;

std::string elf_class_name(std::uint8_t file_class) {
    switch (file_class) {
        case ELFCLASSNONE: return "none";
        case ELFCLASS32: return "ELF32";
        case ELFCLASS64: return "ELF64";
        default: return "unknown (" + std::to_string(file_class) + ")";
    }
}

std::string data_encoding_name(std::uint8_t encoding) {
    switch (encoding) {
        case ELFDATANONE: return "none";
        case ELFDATA2LSB: return "little-endian (two's complement)";
        case ELFDATA2MSB: return "big-endian (two's complement)";
        default: return "unknown (" + std::to_string(encoding) + ")";
    }
}

std::string os_abi_name(std::uint8_t os_abi) {
    switch (os_abi) {
        case ELFOSABI_SYSV: return "UNIX - System V";
        case ELFOSABI_GNU: return "UNIX - GNU";
        case ELFOSABI_FREEBSD: return "UNIX - FreeBSD";
        default: return "unknown (" + std::to_string(os_abi) + ")";
    }
}

std::string elf_type_name(std::uint16_t type) {
    switch (type) {
        case ET_NONE: return "NONE";
        case ET_REL: return "REL";
        case ET_EXEC: return "EXEC";
        case ET_DYN: return "DYN";
        case ET_CORE: return "CORE";
        default: return "unknown (" + hex(type) + ")";
    }
}

std::string elf_type_description(std::uint16_t type) {
    switch (type) {
        case ET_NONE: return "no file type";
        case ET_REL: return "relocatable object file";
        case ET_EXEC: return "executable (fixed load address)";
        // ET_DYN covers both shared libraries and position-independent
        // executables; the header alone cannot tell them apart.
        case ET_DYN: return "shared object or position-independent executable";
        case ET_CORE: return "core dump";
        default: return "unrecognized file type";
    }
}

std::string machine_name(std::uint16_t machine) {
    switch (machine) {
        case EM_NONE: return "none";
        case EM_386: return "Intel 80386";
        case EM_ARM: return "ARM";
        case EM_X86_64: return "x86-64 (AMD64)";
        case EM_AARCH64: return "AArch64";
        case EM_CUDA: return "NVIDIA CUDA";
        case EM_AMDGPU: return "AMD GPU";
        case EM_RISCV: return "RISC-V";
        case EM_BPF: return "Linux BPF";
        default: return "unknown (" + std::to_string(machine) + ")";
    }
}

std::string segment_type_name(std::uint32_t type) {
    switch (type) {
        case PT_NULL: return "NULL";
        case PT_LOAD: return "LOAD";
        case PT_DYNAMIC: return "DYNAMIC";
        case PT_INTERP: return "INTERP";
        case PT_NOTE: return "NOTE";
        case PT_SHLIB: return "SHLIB";
        case PT_PHDR: return "PHDR";
        case PT_TLS: return "TLS";
        case PT_GNU_EH_FRAME: return "GNU_EH_FRAME";
        case PT_GNU_STACK: return "GNU_STACK";
        case PT_GNU_RELRO: return "GNU_RELRO";
        case PT_GNU_PROPERTY: return "GNU_PROPERTY";
        case PT_GNU_SFRAME: return "GNU_SFRAME";
        default: return hex(type);
    }
}

std::string segment_flags_string(std::uint32_t flags) {
    std::string out;
    out += (flags & PF_R) ? 'R' : ' ';
    out += (flags & PF_W) ? 'W' : ' ';
    out += (flags & PF_X) ? 'E' : ' ';
    return out;
}

}  // namespace binscope
