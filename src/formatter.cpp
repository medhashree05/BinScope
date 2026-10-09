#include "binscope/formatter.hpp"

#include "binscope/byte_reader.hpp"
#include "binscope/elf_constants.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>

namespace binscope {

using namespace elf;

void TextTable::add_column(std::string title, Align align) {
    columns_.push_back({std::move(title), align});
}

void TextTable::add_row(std::vector<std::string> cells) {
    cells.resize(columns_.size());
    rows_.push_back(std::move(cells));
}

void TextTable::render(std::ostream& out, const std::string& indent) const {
    std::vector<std::size_t> widths(columns_.size());
    for (std::size_t c = 0; c < columns_.size(); ++c) {
        widths[c] = columns_[c].title.size();
        for (const auto& row : rows_) widths[c] = std::max(widths[c], row[c].size());
    }

    auto emit = [&](const std::vector<std::string>& cells) {
        std::string line = indent;
        for (std::size_t c = 0; c < columns_.size(); ++c) {
            const std::string& cell = cells[c];
            const std::size_t pad = widths[c] - cell.size();
            const bool last = c + 1 == columns_.size();
            if (columns_[c].align == Align::Right) {
                line.append(pad, ' ');
                line += cell;
            } else {
                line += cell;
                if (!last) line.append(pad, ' ');
            }
            if (!last) line += "  ";
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out << line << '\n';
    };

    std::vector<std::string> titles;
    for (const auto& col : columns_) titles.push_back(col.title);
    emit(titles);
    std::vector<std::string> rule;
    for (std::size_t w : widths) rule.emplace_back(w, '-');
    emit(rule);
    for (const auto& row : rows_) emit(row);
}

std::string hex_padded(std::uint64_t value, int digits) {
    std::ostringstream s;
    s << "0x" << std::hex << std::setw(digits) << std::setfill('0') << value;
    return s.str();
}

namespace {

void field(std::ostream& out, const std::string& label, const std::string& value) {
    out << "  " << std::left << std::setw(34) << (label + ":") << value << '\n';
}

std::string count_with_location(std::uint64_t count, std::uint64_t offset, std::uint16_t entsize) {
    std::ostringstream s;
    s << count << (count == 1 ? " entry" : " entries");
    if (offset != 0 || count != 0) {
        s << " at file offset " << hex(offset) << " (" << offset << "), " << entsize
          << " bytes each";
    }
    return s.str();
}

}  // namespace

void print_file_header(std::ostream& out, const ElfFile& file) {
    const ElfHeader& h = file.header();
    out << "ELF header\n";
    field(out, "Magic", "7f 45 4c 46 (valid ELF)");
    field(out, "Class", elf_class_name(h.file_class));
    field(out, "Data encoding", data_encoding_name(h.data_encoding));
    field(out, "Version", std::to_string(h.version) + " (current)");
    field(out, "OS/ABI", os_abi_name(h.os_abi));
    field(out, "ABI version", std::to_string(h.abi_version));
    field(out, "Type", elf_type_name(h.type) + " (" + elf_type_description(h.type) + ")");
    field(out, "Machine", machine_name(h.machine));
    field(out, "Entry point", hex(h.entry) + (h.entry == 0 ? " (none)" : ""));
    field(out, "Flags", hex(h.flags));
    field(out, "ELF header size", std::to_string(h.ehsize) + " bytes");

    std::string ph = count_with_location(h.program_header_count, h.phoff, h.phentsize);
    if (h.extended_program_numbering) ph += " [count from section 0 sh_info: PN_XNUM]";
    field(out, "Program headers", ph);

    std::string sh = count_with_location(h.section_count, h.shoff, h.shentsize);
    if (h.extended_section_numbering) sh += " [extended numbering via section 0]";
    field(out, "Section headers", sh);

    std::string names = std::to_string(h.section_name_table_index);
    if (const Section* s = file.section_at(h.section_name_table_index);
        s != nullptr && h.section_name_table_index != 0) {
        names += " (" + s->name + ")";
    } else {
        names += " (none)";
    }
    field(out, "Section-name string table index", names);
    field(out, "File size", std::to_string(file.file_size()) + " bytes");
}

void print_program_headers(std::ostream& out, const ElfFile& file) {
    const auto& segments = file.program_headers();
    out << "Program headers (segments)\n";
    if (segments.empty()) {
        out << "  none - relocatable object files are not loaded directly, so they have no\n"
               "  segments; the linker assigns addresses when it builds an executable.\n";
        return;
    }

    TextTable t;
    t.add_column("Idx", TextTable::Align::Right);
    t.add_column("Type");
    t.add_column("Flags");
    t.add_column("Offset");
    t.add_column("VirtAddr");
    t.add_column("FileSiz");
    t.add_column("MemSiz");
    t.add_column("Align");
    for (const ProgramHeader& p : segments) {
        t.add_row({std::to_string(p.index), segment_type_name(p.type),
                   segment_flags_string(p.flags), hex_padded(p.offset, 6),
                   hex_padded(p.vaddr), hex_padded(p.filesz, 6), hex_padded(p.memsz, 6),
                   hex(p.align)});
    }
    t.render(out);

    // PT_INTERP holds the path of the dynamic loader as a NUL-terminated string.
    for (const ProgramHeader& p : segments) {
        if (p.type != PT_INTERP) continue;
        if (file.bytes().contains(p.offset, p.filesz)) {
            const ByteView data = file.bytes().subview(p.offset, p.filesz, "PT_INTERP");
            if (const auto path = data.c_string(0)) {
                out << "  Program interpreter: " << *path << '\n';
            }
        }
    }
}

void print_sections(std::ostream& out, const ElfFile& file) {
    const auto& sections = file.sections();
    out << "Section headers: " << sections.size() << " entries\n";
    if (sections.empty()) {
        out << "  (this file has no section header table)\n";
        return;
    }

    TextTable t;
    t.add_column("Nr", TextTable::Align::Right);
    t.add_column("Name");
    t.add_column("Type");
    t.add_column("Flags");
    t.add_column("Address");
    t.add_column("Offset");
    t.add_column("Size");
    t.add_column("EntSize");
    t.add_column("Link", TextTable::Align::Right);
    t.add_column("Info", TextTable::Align::Right);
    t.add_column("Align", TextTable::Align::Right);
    for (const Section& s : sections) {
        std::string offset = hex_padded(s.offset, 6);
        if (s.type == SHT_NOBITS) offset += "*";
        if (!s.contents_in_bounds) offset += "!";
        t.add_row({std::to_string(s.index), s.name, section_type_name(s.type),
                   section_flags_string(s.flags), hex_padded(s.addr), offset,
                   hex_padded(s.size, 6), s.entsize ? hex(s.entsize) : "", std::to_string(s.link),
                   std::to_string(s.info), std::to_string(s.addralign)});
    }
    t.render(out);

    out << "\n  Flags: W=write A=alloc X=execute M=merge S=strings I=info-link L=link-order\n"
           "         O=OS-nonconforming G=group T=TLS C=compressed R=retain l=large E=exclude\n"
           "         x=unknown bits\n"
           "  *  NOBITS: occupies memory at run time but no bytes in the file\n";
    bool any_bad = false;
    for (const Section& s : sections) any_bad = any_bad || !s.contents_in_bounds;
    if (any_bad) out << "  !  contents lie outside the file and were not read\n";
    if (file.header().type == ET_REL) {
        out << "  Addresses are 0 because a relocatable object has not been placed in memory yet;\n"
               "  the linker assigns virtual addresses when it produces an executable.\n";
    }

    // Short explanations for the well-known sections present in this file.
    std::vector<std::pair<std::string, std::string>> notes;
    for (const Section& s : sections) {
        const std::string purpose = section_purpose(s.name);
        if (!purpose.empty()) notes.emplace_back(s.name, purpose);
    }
    if (!notes.empty()) {
        out << "\nWhat these sections hold\n";
        std::size_t width = 0;
        for (const auto& n : notes) width = std::max(width, n.first.size());
        for (const auto& [name, purpose] : notes) {
            out << "  " << std::left << std::setw(static_cast<int>(width)) << name << "  " << purpose
                << '\n';
        }
    }
}

void print_symbol_table(std::ostream& out, const ElfFile& file, const SymbolTable& table,
                        const SymbolReportOptions& options) {
    std::size_t shown = 0;
    for (const Symbol& s : table.symbols) {
        if (options.filter.matches(s)) ++shown;
    }

    out << "Symbol table '" << table.section_name << "' (section [" << table.section_index << "], "
        << table.symbols.size() << " entries";
    if (const Section* strtab = file.section_at(table.string_table_index)) {
        out << ", names in [" << table.string_table_index << "] '" << strtab->name << "'";
    }
    out << ")\n";
    if (options.filter.active()) {
        std::string criteria;
        auto add = [&](bool on, const char* label) {
            if (!on) return;
            if (!criteria.empty()) criteria += ", ";
            criteria += label;
        };
        add(options.filter.defined_only, "defined");
        add(options.filter.undefined_only, "undefined");
        add(options.filter.functions_only, "functions");
        out << "  showing " << shown << " of " << table.symbols.size()
            << " entries (filter: " << criteria << ")\n";
    }

    if (shown > 0) {
        TextTable t;
        t.add_column("Num", TextTable::Align::Right);
        t.add_column("Value");
        t.add_column("Size", TextTable::Align::Right);
        t.add_column("Type");
        t.add_column("Bind");
        t.add_column("Vis");
        t.add_column("Ndx", TextTable::Align::Right);
        t.add_column("Section");
        t.add_column("Status");
        t.add_column("Name");
        for (const Symbol& s : table.symbols) {
            if (!options.filter.matches(s)) continue;
            const std::string name = options.demangle ? demangle(s.name) : s.name;
            t.add_row({std::to_string(s.index), hex_padded(s.value), std::to_string(s.size),
                       symbol_type_name(s.type), symbol_binding_name(s.binding),
                       symbol_visibility_name(s.visibility), symbol_index_label(s.section_index),
                       s.section_name, symbol_status(s), name});
        }
        t.render(out);
    }

    std::size_t defined = 0, undefined = 0, local = 0, global = 0, weak = 0;
    for (const Symbol& s : table.symbols) {
        if (s.is_null()) continue;
        (s.is_undefined() ? undefined : defined)++;
        if (s.binding == STB_LOCAL) ++local;
        if (s.binding == STB_GLOBAL) ++global;
        if (s.binding == STB_WEAK) ++weak;
    }
    out << "  Summary: " << defined << " defined, " << undefined << " undefined; " << local
        << " local, " << global << " global, " << weak << " weak (null entry 0 excluded)\n";
    if (file.header().type == ET_REL) {
        out << "  Value is an offset within the symbol's section (the object is not yet linked).\n";
    } else {
        out << "  Value is a virtual address" << (file.header().type == ET_DYN ? " relative to the load base" : "")
            << "; 0 for undefined symbols.\n";
    }
}

std::string signed_hex(std::int64_t value) {
    // Negate in unsigned arithmetic so INT64_MIN is handled without overflow.
    const auto magnitude = value < 0 ? (~static_cast<std::uint64_t>(value) + 1U)
                                     : static_cast<std::uint64_t>(value);
    return (value < 0 ? "-" : "+") + hex(magnitude);
}

void print_relocations(std::ostream& out, const ElfFile& file,
                       const std::vector<RelocationSection>& sections, bool demangle_names) {
    const std::uint16_t machine = file.header().machine;
    const bool relocatable = file.header().type == ET_REL;

    if (sections.empty()) {
        out << "No REL/RELA relocation sections in this file.\n";
    }

    std::map<std::uint32_t, std::size_t> type_counts;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const RelocationSection& rs = sections[i];
        if (i != 0) out << '\n';
        out << "Relocation section '" << rs.section_name << "' [" << rs.section_index << "]: "
            << rs.entries.size() << (rs.entries.size() == 1 ? " entry, " : " entries, ")
            << relocation_encoding_name(rs.encoding) << '\n';
        if (rs.target_section_index != 0) {
            out << "  patches section [" << rs.target_section_index << "] '"
                << rs.target_section_name << "'";
        } else {
            out << "  not tied to one section: applied at load time to addresses across the image";
        }
        if (rs.symbol_table_index != 0) {
            out << "; symbols from [" << rs.symbol_table_index << "] '" << rs.symbol_table_name
                << "'";
        }
        out << '\n';
        if (rs.entries.empty()) continue;

        TextTable t;
        t.add_column("Idx", TextTable::Align::Right);
        t.add_column("Offset");
        t.add_column("Type");
        t.add_column("Sym#", TextTable::Align::Right);
        t.add_column("Sym.Value");
        t.add_column("Symbol");
        t.add_column("Addend", TextTable::Align::Right);
        for (const Relocation& r : rs.entries) {
            ++type_counts[r.type];
            std::string name = demangle_names ? demangle(r.symbol_name) : r.symbol_name;
            if (r.symbol_index == 0) name = "-";
            t.add_row({std::to_string(r.index), hex_padded(r.offset, 12),
                       relocation_type_name(machine, r.type), std::to_string(r.symbol_index),
                       r.symbol_index == 0 ? "" : hex_padded(r.symbol_value),
                       name, r.addend ? signed_hex(*r.addend) : "(implicit)"});
        }
        t.render(out);
    }

    if (!sections.empty()) {
        out << "\n  Offset is " <<
            (relocatable ? "a byte offset within the patched section (the object is not linked yet)."
                         : "the virtual address of the location to patch (relative to the load\n"
                           "  base for position-independent files).")
            << '\n';
    }

    if (!type_counts.empty()) {
        out << "\nRelocation types used\n";
        TextTable legend;
        legend.add_column("Type");
        legend.add_column("Count", TextTable::Align::Right);
        legend.add_column("Calculation");
        legend.add_column("Meaning");
        for (const auto& [type, count] : type_counts) {
            const std::string formula = relocation_formula(machine, type);
            const std::string meaning = relocation_explanation(machine, type);
            legend.add_row({relocation_type_name(machine, type), std::to_string(count),
                            formula.empty() ? "?" : formula,
                            meaning.empty() ? "not decoded by BinScope" : meaning});
        }
        legend.render(out);
        out << "  Calculation symbols (x86-64 psABI): S=symbol value, A=addend, P=address of the\n"
               "  patched location, B=load base, G=GOT entry offset, GOT=GOT address, L=PLT entry,\n"
               "  Z=symbol size. BinScope reports these formulas; it does not apply them.\n";
        if (machine != EM_X86_64) {
            out << "  Note: relocation names are only decoded for x86-64; this file targets "
                << machine_name(machine) << ".\n";
        }
    }

    if (const std::size_t relr = count_relr_sections(file)) {
        out << "\nNote: " << relr
            << " SHT_RELR section(s) of packed relative relocations are present but not decoded.\n";
    }
}

void print_summary(std::ostream& out, const FileSummary& s) {
    out << "Summary\n";
    out << "  Sections: " << s.section_count << ", segments: " << s.segment_count << '\n';

    if (s.type == ET_REL) {
        out << "  Kind: relocatable object - input to the linker; addresses are not assigned yet\n";
    } else if (s.has_interpreter) {
        out << "  Kind: dynamically linked executable (loaded via its program interpreter)\n";
    } else if (s.type == ET_DYN && s.has_dynamic_section) {
        out << "  Kind: shared object (no program interpreter)\n";
    } else if (s.type == ET_EXEC) {
        out << "  Kind: statically linked executable (no program interpreter)\n";
    }

    if (s.symbol_error) {
        out << "  Symbol tables: could not be decoded (" << *s.symbol_error << ")\n";
    } else if (s.symbol_tables.empty()) {
        out << "  Symbol tables: none (stripped)\n";
    } else {
        for (const auto& t : s.symbol_tables) {
            out << "  Symbol table " << t.name << ": " << t.entries << " entries (" << t.defined
                << " defined, " << t.undefined << " undefined)\n";
        }
        if (!s.has_symtab) out << "  No .symtab: the static symbol table has been stripped\n";
    }

    if (s.relocation_error) {
        out << "  Relocations: could not be decoded (" << *s.relocation_error << ")\n";
    } else if (s.relocation_sections.empty()) {
        out << "  Relocation sections: none\n";
    } else {
        for (const auto& r : s.relocation_sections) {
            out << "  Relocation section " << r.name << ": " << r.entries
                << (r.entries == 1 ? " entry, " : " entries, ")
                << r.encoding;
            if (!r.target.empty()) out << ", patches " << r.target;
            out << '\n';
        }
    }
    if (s.relr_sections) out << "  Packed relative relocation (RELR) sections: " << s.relr_sections << '\n';
    out << "  Debug information (.debug_*): " << (s.has_debug_info ? "present" : "absent") << '\n';
}

namespace {

std::string describe_side(const std::optional<SymbolSide>& side, bool relocatable) {
    if (!side) return "-";
    std::string s;
    if (!side->defined) {
        s = "UND";
        if (!side->version.empty()) s += " @" + side->version;
    } else if (relocatable) {
        s = side->section + "+" + hex(side->value);  // section-relative offset
    } else {
        s = hex(side->value) + " in " + side->section;  // virtual address
    }
    if (side->defined) s += " (" + symbol_type_name(side->type) + ", " + std::to_string(side->size) + " B)";
    if (side->in_dynsym) s += " [dynsym]";
    return s;
}

std::string match_note(const SymbolMatch& m) {
    switch (m.category) {
        case MatchCategory::DefinedInBoth: {
            std::string note;
            if (m.values_comparable) {
                note = m.before->value == m.after->value ? "same value" : "value changed";
            } else {
                note = "values in different address spaces";
            }
            if (m.before->size != m.after->size) note += "; size changed";
            return note;
        }
        case MatchCategory::DeferredToDynamicLoader:
            return m.after->version.empty() ? "bound at load/run time"
                                            : "bound at load/run time to version " + m.after->version;
        case MatchCategory::ResolvedByStaticLinker:
            return m.after->in_dynsym ? "defined and exported dynamically" : "definition linked in";
        case MatchCategory::DefinedOnlyAfter:
            return "the after file provides a definition";
        case MatchCategory::OnlyInBefore:
            return m.before->defined ? "not present after (e.g. inlined, discarded or renamed)"
                                     : "reference no longer present";
        case MatchCategory::OnlyInAfter:
            return m.after->defined ? "defined only after" : "new undefined reference";
        case MatchCategory::Ambiguous:
            return "several symbols share this name";
        default:
            return "";
    }
}

std::string optional_count(const std::optional<std::size_t>& n) { return n ? std::to_string(*n) : "-"; }

// Prints `text` as a bullet, word-wrapped to `width` columns.
void print_bullet(std::ostream& out, const std::string& text, std::size_t width = 92) {
    std::istringstream words(text);
    std::string word;
    std::string line = "  -";
    while (words >> word) {
        if (line.size() + 1 + word.size() > width && line.size() > 4) {
            out << line << '\n';
            line = "   ";
        }
        line += ' ' + word;
    }
    out << line << '\n';
}

}  // namespace

void print_comparison(std::ostream& out, const ComparisonReport& r, bool demangle_names) {
    out << "Comparing\n";
    out << "  before: " << r.before.name << "  [" << elf_type_name(r.before.type) << ": "
        << elf_type_description(r.before.type) << "]\n";
    out << "  after:  " << r.after.name << "  [" << elf_type_name(r.after.type) << ": "
        << elf_type_description(r.after.type) << "]\n\n";

    out << "File structure\n";
    {
        TextTable t;
        t.add_column("Property");
        t.add_column("Before");
        t.add_column("After");
        t.add_row({"ELF type", elf_type_name(r.before.type), elf_type_name(r.after.type)});
        t.add_row({"Machine", machine_name(r.before.machine), machine_name(r.after.machine)});
        t.add_row({"Entry point", hex(r.before.entry), hex(r.after.entry)});
        t.add_row({"Sections", std::to_string(r.before.sections), std::to_string(r.after.sections)});
        t.add_row({"Segments", std::to_string(r.before.segments), std::to_string(r.after.segments)});
        t.add_row({"Relocation entries (REL/RELA)", std::to_string(r.relocations_before),
                   std::to_string(r.relocations_after)});
        t.render(out);
    }

    out << "\nSections (matched by name)\n";
    {
        TextTable t;
        t.add_column("Name");
        t.add_column("Type");
        t.add_column("Size before", TextTable::Align::Right);
        t.add_column("Size after", TextTable::Align::Right);
        t.add_column("Address after");
        t.add_column("Change");
        for (const SectionDiff& d : r.sections) {
            const Section& any = d.before ? *d.before : *d.after;
            std::string change;
            if (!d.after) change = "only before";
            else if (!d.before) change = "only after";
            else if (d.before->size != d.after->size) change = "size changed";
            t.add_row({d.name, section_type_name(any.type), d.before ? hex(d.before->size) : "-",
                       d.after ? hex(d.after->size) : "-",
                       d.after && d.after->addr ? hex(d.after->addr) : "-", change});
        }
        t.render(out);
    }

    out << "\nSymbols (matched by name; @version suffixes are ignored for matching)\n";
    if (r.symbols.empty()) {
        out << "  no symbols to compare\n";
    }
    const bool before_rel = r.before.type == ET_REL;
    const bool after_rel = r.after.type == ET_REL;
    for (std::size_t i = 0; i < r.symbols.size();) {
        const MatchCategory category = r.symbols[i].category;
        std::size_t end = i;
        while (end < r.symbols.size() && r.symbols[end].category == category) ++end;

        out << "\n  " << match_category_title(category) << ": " << (end - i) << '\n';
        TextTable t;
        t.add_column("Name");
        t.add_column("Before");
        t.add_column("After");
        t.add_column("Note");
        for (; i < end; ++i) {
            const SymbolMatch& m = r.symbols[i];
            t.add_row({demangle_names ? demangle(m.name) : m.name, describe_side(m.before, before_rel),
                       describe_side(m.after, after_rel), match_note(m)});
        }
        t.render(out, "    ");
    }

    if (!r.placements.empty()) {
        out << "\nWhere the linker placed this object's input sections (inferred)\n";
        TextTable t;
        t.add_column("Input section");
        t.add_column("Placed at");
        t.add_column("Supporting symbols", TextTable::Align::Right);
        for (const PlacementEstimate& p : r.placements) {
            t.add_row({p.section, hex(p.address), std::to_string(p.symbols)});
        }
        t.render(out);
        out << "  Every matched symbol from the section moved by the same amount, so the input\n"
               "  section starts at that address in the output; symbol address = placement +\n"
               "  section offset.\n";
    }

    out << "\nRelocation sections (entry counts)\n";
    if (r.relocations.empty()) {
        out << "  neither file has REL/RELA relocation sections\n";
    } else {
        TextTable t;
        t.add_column("Section");
        t.add_column("Before", TextTable::Align::Right);
        t.add_column("After", TextTable::Align::Right);
        for (const RelocationDiff& d : r.relocations) {
            t.add_row({d.name, optional_count(d.before), optional_count(d.after)});
        }
        t.add_row({"total", std::to_string(r.relocations_before), std::to_string(r.relocations_after)});
        t.render(out);
    }

    if (!r.notes.empty()) {
        out << "\nNotes\n";
        for (const std::string& n : r.notes) print_bullet(out, n);
    }
    if (!r.incomplete.empty()) {
        out << "\nIncomplete comparison\n";
        for (const std::string& n : r.incomplete) print_bullet(out, n);
    }
}

void print_warnings(std::ostream& err, const ElfFile& file) {
    for (const std::string& w : file.warnings()) {
        err << "binscope: warning: " << file.name() << ": " << w << '\n';
    }
}

void print_extra_warnings(std::ostream& err, const ElfFile& file,
                          const std::vector<std::string>& warnings) {
    for (const std::string& w : warnings) {
        err << "binscope: warning: " << file.name() << ": " << w << '\n';
    }
}

}  // namespace binscope
