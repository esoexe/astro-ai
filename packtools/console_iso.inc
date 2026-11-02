// console_iso.inc - console-reverse/iso: a C++ port of console-skill/console-reverse/iso.py (Jake Schincariol, MIT).
// Reads an ISO 9660 image of your own PS2 disc: info, ls, extract, plain (2352 -> 2048-byte sectors), boot (SYSTEM.CNF).
// Streams the image (64-bit seeks, 1 MiB copies); plain .iso and raw .bin (Mode 1, Mode 2 Form 1). Same output and exit codes.
namespace packtools::console_iso {
using namespace pyrt;
using namespace packtools::console_common;

constexpr long long BLOCK = 2048;
constexpr long long COPY_BLOCKS = 512;
constexpr int MAX_DEPTH = 64;
constexpr std::size_t MAX_ENTRIES = 2000000;
const char* const PROG = "iso";

inline std::string region_of(const std::string& pre) {
    static const std::map<std::string, std::string> R{{"SLUS", "USA"}, {"SCUS", "USA"}, {"SLES", "Europe"}, {"SCES", "Europe"},
        {"SLPS", "Japan"}, {"SLPM", "Japan"}, {"SCPS", "Japan"}, {"SLKA", "Korea"}, {"SCKA", "Korea"}, {"SCAJ", "Asia"}};
    const auto it = R.find(pre); return it == R.end() ? "unknown region prefix" : it->second;
}

struct Extent { long long lba, size; };
struct Entry { std::string name, raw_name, path; bool is_dir = false; std::vector<Extent> extents; long long size = 0; };
struct Rec { long long lba, size; bool is_dir, multi_extent; std::string raw_name; };

inline std::optional<Rec> parse_record(const std::string& buf, std::size_t pos) {
    if (pos + 33 > buf.size()) return std::nullopt;
    const std::size_t rec_len = static_cast<unsigned char>(buf[pos]);
    if (rec_len < 34 || pos + rec_len > buf.size()) return std::nullopt;
    const std::size_t name_len = static_cast<unsigned char>(buf[pos + 32]);
    if (name_len == 0 || 33 + name_len > rec_len) return std::nullopt;
    const auto flags = static_cast<unsigned char>(buf[pos + 25]);
    return Rec{u32le(buf, pos + 2), u32le(buf, pos + 10), (flags & 0x02) != 0, (flags & 0x80) != 0, buf.substr(pos + 33, name_len)};
}

inline bool str_isdigit(const std::string& s) {   // str.isdigit(): non-empty, every character a Unicode digit
    if (s.empty()) return false;
    for (const auto& c : cps(s)) if (!uprop::digit(cp_of(c))) return false;
    return true;
}
inline std::string clean_name_s(std::string name) {   // clean_name on the latin-1 text
    const std::size_t semi = name.rfind(';');
    if (semi != std::string::npos && str_isdigit(name.substr(semi + 1))) name = name.substr(0, semi);
    if (!name.empty() && name.back() == '.' && cps(name).size() > 1) name.pop_back();
    return name;
}
inline std::string clean_name(const std::string& raw) { return clean_name_s(latin1(raw)); }
inline std::string latin1_replace(const std::string& s) {   // s.encode("latin-1", "replace").decode("latin-1")
    std::string o; for (const auto& c : cps(s)) o += cp_of(c) > 0xFF ? std::string("?") : c; return o;
}

inline std::vector<std::string> split_disc_path(const std::string& path) {
    std::string p = strip(path);
    const std::string low = lower(p);
    for (const std::string pre : {"cdrom0:", "cdrom:"}) if (low.rfind(pre, 0) == 0) { p = cpslice(p, static_cast<long>(pre.size())); break; }
    p = replace_all(p, "\\", "/");
    std::vector<std::string> parts;
    for (const auto& part : split(p, "/")) { if (part.empty()) continue; parts.push_back(clean_name_s(latin1_replace(part))); }
    return parts;
}

inline bool safe_component(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (const auto& c : cps(name)) { const uint32_t x = cp_of(c); if (x == '/' || x == '\\' || x == ':' || x < 0x20) return false; }
    return true;
}

// ---------------------------------------------------------------- image access
class IsoImage {
public:
    std::string path, layout, volume_id, system_id;
    long long image_size = 0, sector_size = 0, data_offset = 0, volume_blocks = 0;
    Entry root;

    explicit IsoImage(const std::string& p) : path(p) {
        if (is_dir(p)) {
#if defined(_WIN32)
            throw InputError("cannot open " + p + ": " + strerror_of(EACCES));
#else
            throw InputError("cannot open " + p + ": " + strerror_of(EISDIR));
#endif
        }
        f_ = open_file(p, "rb");
        if (!f_) throw InputError("cannot open " + p + ": " + strerror_of(errno));
        try {
            seek(0, SEEK_END); image_size = tell();
            detect_layout();
            const std::string pvd = find_pvd();
            parse_pvd(pvd);
        } catch (...) { std::fclose(f_); f_ = nullptr; throw; }
    }
    ~IsoImage() { if (f_) std::fclose(f_); }
    IsoImage(const IsoImage&) = delete;
    IsoImage& operator=(const IsoImage&) = delete;

    std::optional<std::string> raw(long long offset, long long size) {
        if (offset < 0 || offset + size > image_size) return std::nullopt;
        seek(offset, SEEK_SET);
        std::string d(static_cast<std::size_t>(size), '\0');
        if (std::fread(d.data(), 1, d.size(), f_) != d.size()) return std::nullopt;
        return d;
    }
    long long block_offset(long long lba) const { return lba * sector_size + data_offset; }
    std::string read_block(long long lba) {
        auto d = raw(block_offset(lba), BLOCK);
        if (!d) throw InputError(cfmt("sector %lld is past the end of the image (truncated dump?)", lba));
        return *d;
    }
    bool extent_fits(long long lba, long long size) const {
        if (size == 0) return true;
        const long long blocks = (size + BLOCK - 1) / BLOCK, last = lba + blocks - 1;
        return block_offset(last) + std::min(BLOCK, size - (blocks - 1) * BLOCK) <= image_size;
    }
    template <class F> void iter_extent(long long lba, long long size, F&& sink) {   // the extent's bytes, in chunks
        long long remaining = size, cur = lba;
        while (remaining > 0) {
            if (sector_size == BLOCK) {
                const long long chunk = std::min(remaining, COPY_BLOCKS * BLOCK);
                auto d = raw(block_offset(cur), chunk);
                if (!d) throw InputError(cfmt("data at sector %lld is past the end of the image (truncated dump?)", cur));
                cur += (chunk + BLOCK - 1) / BLOCK; remaining -= chunk;
                sink(*d);
            } else {
                std::string parts; long long count = 0;
                while (remaining > 0 && count < COPY_BLOCKS) {
                    const long long take = std::min(BLOCK, remaining);
                    parts += read_block(cur).substr(0, static_cast<std::size_t>(take));
                    ++cur; remaining -= take; ++count;
                }
                sink(parts);
            }
        }
    }
    long long expected_size() const { return volume_blocks * sector_size; }

    std::vector<Entry> list_dir(const Entry& entry) {
        std::vector<Entry> children;
        const auto [lba, size] = entry.extents[0];
        if (!extent_fits(lba, size)) throw InputError("directory /" + entry.path + " points past the end of the image (truncated dump?)");
        const long long blocks = (size + BLOCK - 1) / BLOCK;
        long pending = -1;   // index into children of a multi-extent file still collecting extents
        for (long long b = 0; b < blocks; ++b) {
            const std::string block = read_block(lba + b);
            std::size_t pos = 0;
            while (pos < static_cast<std::size_t>(BLOCK)) {
                const std::size_t rec_len = static_cast<unsigned char>(block[pos]);
                if (rec_len == 0) break;
                const auto rec = parse_record(block, pos);
                if (!rec) throw InputError(cfmt("damaged directory record in /%s (sector %lld)", entry.path.c_str(), lba + b));
                pos += rec_len;
                if (rec->raw_name == std::string(1, '\0') || rec->raw_name == "\x01") continue;
                const std::string name = clean_name(rec->raw_name);
                if (pending >= 0 && children[static_cast<std::size_t>(pending)].name == name && !children[static_cast<std::size_t>(pending)].is_dir) {
                    Entry& pe = children[static_cast<std::size_t>(pending)];
                    pe.extents.push_back({rec->lba, rec->size}); pe.size += rec->size;
                    if (!rec->multi_extent) pending = -1;
                    continue;
                }
                Entry child; child.name = name; child.raw_name = rec->raw_name; child.path = entry.path.empty() ? name : entry.path + "/" + name;
                child.is_dir = rec->is_dir; child.extents = {{rec->lba, rec->size}}; child.size = rec->size;
                children.push_back(std::move(child));
                pending = rec->multi_extent ? static_cast<long>(children.size()) - 1 : -1;
                if (children.size() > MAX_ENTRIES) throw InputError("directory /" + entry.path + " has too many entries");
            }
        }
        return children;
    }
    void walk(const Entry& entry, std::vector<Entry>& out, int depth, std::set<long long>& seen) {
        if (depth > MAX_DEPTH) throw InputError(cfmt("directory tree deeper than %d levels (damaged image?)", MAX_DEPTH));
        const long long key = entry.extents[0].lba;
        if (!seen.insert(key).second) return;
        for (const auto& child : list_dir(entry)) {
            out.push_back(child);
            if (child.is_dir) walk(child, out, depth + 1, seen);
        }
    }
    std::vector<Entry> walk_all(const Entry& entry) { std::vector<Entry> v; std::set<long long> seen; walk(entry, v, 0, seen); return v; }
    std::optional<Entry> find(const std::string& p) {
        Entry cur = root;
        for (const auto& part : split_disc_path(p)) {
            if (!cur.is_dir) return std::nullopt;
            const auto kids = list_dir(cur);
            const Entry* match = nullptr;
            for (const auto& k : kids) if (k.name == part) { match = &k; break; }
            if (!match) { const std::string low = lower(part); for (const auto& k : kids) if (lower(k.name) == low) { match = &k; break; } }
            if (!match) return std::nullopt;
            cur = *match;
        }
        return cur;
    }

private:
    std::FILE* f_ = nullptr;
    void seek(long long off, int whence) {
#if defined(_WIN32)
        _fseeki64(f_, off, whence);
#else
        fseeko(f_, static_cast<off_t>(off), whence);
#endif
    }
    long long tell() {
#if defined(_WIN32)
        return _ftelli64(f_);
#else
        return static_cast<long long>(ftello(f_));
#endif
    }
    void detect_layout() {
        const struct { long long ss, off; const char* name; } cands[] = {
            {2048, 0, "2048-byte sectors (plain ISO)"}, {2352, 16, "2352-byte raw sectors, Mode 1"}, {2352, 24, "2352-byte raw sectors, Mode 2 Form 1"}};
        static const std::string SYNC = std::string(1, '\0') + std::string(10, '\xff') + std::string(1, '\0');
        for (const auto& c : cands) {
            const auto head = raw(16 * c.ss + c.off, 8);
            if (head && head->compare(1, 5, "CD001") == 0) {
                if (c.ss == 2352) { const auto sync = raw(16 * c.ss, 12); if (!sync || *sync != SYNC) continue; }
                sector_size = c.ss; data_offset = c.off; layout = c.name; return;
            }
        }
        if (image_size < 17 * BLOCK) throw InputError(cfmt("%s is too small to be a disc image (%lld bytes)", path.c_str(), image_size));
        throw InputError(path + " is not an ISO 9660 image (no CD001 volume descriptor at sector 16)");
    }
    std::string find_pvd() {
        for (long long i = 16; i < 16 + 64; ++i) {
            std::string block;
            try { block = read_block(i); } catch (const InputError&) { break; }
            if (block.compare(1, 5, "CD001") != 0) break;
            const auto t = static_cast<unsigned char>(block[0]);
            if (t == 1) return block;
            if (t == 255) break;
        }
        throw InputError(path + " has no ISO 9660 primary volume descriptor");
    }
    void parse_pvd(const std::string& p) {
        volume_id = strip(latin1(p.substr(40, 32)));
        system_id = strip(latin1(p.substr(8, 32)));
        volume_blocks = u32le(p, 80);
        const uint32_t lbs = u16le(p, 128);
        if (lbs != BLOCK) throw InputError(cfmt("unsupported logical block size %u (expected 2048)", lbs));
        const auto r = parse_record(p, 156);
        if (!r || !r->is_dir) throw InputError("the root directory record is damaged");
        root = Entry{}; root.is_dir = true; root.extents = {{r->lba, r->size}}; root.size = r->size;
    }
};

// ---------------------------------------------------------------- SYSTEM.CNF
inline std::vector<std::pair<std::string, std::string>> parse_system_cnf(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> fields;
    for (auto line : splitlines(text)) {
        line = strip(line);
        const std::size_t eq = line.find('=');
        if (line.empty() || eq == std::string::npos) continue;
        const std::string key = upper(strip(line.substr(0, eq))), value = strip(line.substr(eq + 1));
        bool found = false;
        for (auto& kv : fields) if (kv.first == key) { kv.second = value; found = true; }
        if (!found) fields.emplace_back(key, value);
    }
    return fields;
}
inline Val field(const std::vector<std::pair<std::string, std::string>>& f, const std::string& k) {
    for (const auto& kv : f) if (kv.first == k) return Val::str(kv.second);
    return Val::none();
}
inline std::optional<std::string> serial_from_elf_name(const std::string& name) {
    const std::string pat = R"(^([A-Za-z]{4})_(\d{3})\.(\d{2})$)";
    const auto m = re::match(pat, name);
    if (!m) return std::nullopt;
    return upper(m.group(1)) + "-" + m.group(2) + m.group(3);
}
inline Val boot_info_from_cnf(const std::string& text) {
    const auto f = parse_system_cnf(text);
    Val info = dict_of({{"boot2", field(f, "BOOT2")}, {"ver", field(f, "VER")}, {"vmode", field(f, "VMODE")},
                        {"elf", Val::none()}, {"serial", Val::none()}, {"prefix", Val::none()}, {"region", Val::none()}});
    if (!truthy(get(info, "boot2"))) {
        if (truthy(field(f, "BOOT"))) throw InputError("this is a PlayStation 1 disc (SYSTEM.CNF has BOOT, not BOOT2); PS2Recomp ports PS2 games only");
        throw InputError("SYSTEM.CNF has no BOOT2 line");
    }
    const auto parts = re::split(R"([\\/:])", get(info, "boot2").s);
    const std::string elf = clean_name_s(latin1_replace(parts.back()));
    info.d->set("elf", Val::str(elf));
    if (const auto ser = serial_from_elf_name(elf)) {
        info.d->set("serial", Val::str(*ser));
        const std::string pre = cpslice(*ser, 0, 4);
        info.d->set("prefix", Val::str(pre)); info.d->set("region", Val::str(region_of(pre)));
    }
    return info;
}

inline std::optional<std::string> find_in_dir_ci(const std::string& dir, const std::string& name) {
    std::vector<std::string> names;
    try { names = listdir(dir); } catch (const std::exception&) { return std::nullopt; }
    for (const auto& n : names) if (n == name) return path_join(dir, name);
    const std::string low = lower(name);
    for (const auto& n : names) if (lower(n) == low) return path_join(dir, n);
    return std::nullopt;
}
inline long long file_size_of(const std::string& p) { std::error_code ec; const auto n = std::filesystem::file_size(fspath(p), ec); return ec ? 0 : static_cast<long long>(n); }

inline Val boot_from_dir(const std::string& dir) {
    const auto cnf = find_in_dir_ci(dir, "SYSTEM.CNF");
    if (!cnf || !is_file(*cnf)) throw InputError("no SYSTEM.CNF in " + dir + ": is this the extracted disc folder?");
    std::string raw = read_bytes(*cnf); if (raw.size() > 65536) raw.resize(65536);
    Val info = boot_info_from_cnf(latin1(raw));
    std::optional<std::string> cur = dir;
    for (const auto& part : split_disc_path(get(info, "boot2").s)) cur = cur ? find_in_dir_ci(*cur, part) : std::nullopt;
    info.d->set("source", Val::str(abspath(dir)));
    if (cur && is_file(*cur)) {
        info.d->set("elf_found", Val::boolean(true)); info.d->set("elf_path", Val::str(abspath(*cur))); info.d->set("elf_size", Val::integer(file_size_of(*cur)));
    } else {
        info.d->set("elf_found", Val::boolean(false)); info.d->set("elf_path", Val::none()); info.d->set("elf_size", Val::none());
    }
    return info;
}
inline Val boot_from_iso(IsoImage& img) {
    const auto entry = img.find("SYSTEM.CNF");
    if (!entry || entry->is_dir) throw InputError("no SYSTEM.CNF on this disc: not a PS2 game disc, or not its data track");
    std::string data; img.iter_extent(entry->extents[0].lba, std::min<long long>(entry->size, 65536), [&](const std::string& c) { data += c; });
    Val info = boot_info_from_cnf(latin1(data));
    const auto elf = img.find(get(info, "boot2").s);
    info.d->set("source", Val::str(abspath(img.path)));
    if (elf && !elf->is_dir) {
        info.d->set("elf_found", Val::boolean(true)); info.d->set("elf_path", Val::str("/" + elf->path));
        info.d->set("elf_size", Val::integer(elf->size)); info.d->set("elf_lba", Val::integer(elf->extents[0].lba));
    } else {
        info.d->set("elf_found", Val::boolean(false)); info.d->set("elf_path", Val::none()); info.d->set("elf_size", Val::none());
    }
    return info;
}

// ---------------------------------------------------------------- commands
inline Val entry_json(const Entry& e) {
    return dict_of({{"path", Val::str("/" + e.path)}, {"name", Val::str(e.name)}, {"type", Val::str(e.is_dir ? "dir" : "file")},
                    {"size", e.is_dir ? Val::none() : Val::integer(e.size)}, {"lba", Val::integer(e.extents[0].lba)}});
}

inline int cmd_info(const Val& a) {
    IsoImage img(S(a, "image"));
    const bool complete = img.image_size >= img.expected_size();
    if (B(a, "json")) {
        out(dumps2(dict_of({{"image", Val::str(abspath(img.path))}, {"layout", Val::str(img.layout)}, {"volume_id", Val::str(img.volume_id)},
            {"system_id", Val::str(img.system_id)}, {"volume_blocks", Val::integer(img.volume_blocks)}, {"image_bytes", Val::integer(img.image_size)},
            {"expected_bytes", Val::integer(img.expected_size())}, {"complete", Val::boolean(complete)}})));
    } else {
        out("Image        " + abspath(img.path));
        out("Layout       " + img.layout);
        out("Volume       " + (img.volume_id.empty() ? std::string("(no name)") : img.volume_id) + " (system " + (img.system_id.empty() ? std::string("?") : img.system_id) + ")");
        out(cfmt("Size         %lld bytes, volume says %lld blocks (%lld bytes)", img.image_size, img.volume_blocks, img.expected_size()));
        if (complete) out("Dump         complete (the image is at least as big as the volume)");
        else out(cfmt("Dump         SHORT by %lld bytes: re-dump the disc, some files will be missing", img.expected_size() - img.image_size));
    }
    return complete ? 0 : 1;
}

inline int cmd_ls(const Val& a) {
    IsoImage img(S(a, "image"));
    const std::string path = S(a, "path");
    std::optional<Entry> target = path.empty() ? std::optional<Entry>(img.root) : img.find(path);
    if (!target) throw InputError("not found on the disc: " + path);
    std::vector<Entry> entries;
    if (!target->is_dir) entries = {*target};
    else if (B(a, "recursive")) entries = img.walk_all(*target);
    else entries = img.list_dir(*target);
    if (B(a, "json")) { List l; for (const auto& e : entries) l.push_back(entry_json(e)); out(dumps2(Val::list(l))); return 0; }
    out((img.volume_id.empty() ? std::string("(no volume name)") : img.volume_id) + "  (" + img.layout + ")");
    long long files = 0, bytes = 0;
    for (const auto& e : entries) {
        if (e.is_dir) out(cfmt("  dir  %12s  lba %-8lld /%s/", "-", e.extents[0].lba, e.path.c_str()));
        else { out(cfmt("  file %12lld  lba %-8lld /%s", e.size, e.extents[0].lba, e.path.c_str())); ++files; bytes += e.size; }
    }
    out(cfmt("%zu entries, %lld files, %lld bytes", entries.size(), files, bytes));
    return 0;
}

inline std::vector<Entry> collect_targets(IsoImage& img, const std::vector<std::string>& only) {
    if (only.empty()) return img.walk_all(img.root);
    std::vector<Entry> out_; std::set<std::string> seen;
    for (const auto& p : only) {
        const auto e = img.find(p);
        if (!e) throw InputError("not found on the disc: " + p);
        std::vector<Entry> group{*e};
        if (e->is_dir) { const auto w = img.walk_all(*e); group.insert(group.end(), w.begin(), w.end()); }
        for (const auto& g : group) if (seen.insert(g.path).second) out_.push_back(g);
        auto parts = split(e->path, "/"); if (!parts.empty()) parts.pop_back();   // parent folders keep the tree
        for (std::size_t i = 1; i <= parts.size(); ++i) {
            const std::string parent = join(std::vector<std::string>(parts.begin(), parts.begin() + static_cast<long>(i)), "/");
            if (seen.insert(parent).second) { Entry d; d.name = parts[i - 1]; d.path = parent; d.is_dir = true; d.extents = {{0, 0}}; out_.push_back(d); }
        }
    }
    return out_;
}

inline long long count_char(const std::string& s, char c) { return static_cast<long long>(std::count(s.begin(), s.end(), c)); }

inline int cmd_extract(const Val& a) {
    const std::string out_root = abspath(S(a, "out"));
    IsoImage img(S(a, "image"));
    std::vector<std::string> only; if (!get(a, "only").is_none()) for (const auto& v : *get(a, "only").l) only.push_back(v.s);
    auto targets = collect_targets(img, only);
    for (const auto& e : targets) {   // 1. every name and every extent before writing anything
        for (const auto& comp : split(e.path, "/"))
            if (!safe_component(comp)) throw InputError("refusing unsafe path on the disc: " + str_repr(e.path) + " (nothing written)");
        if (!e.is_dir) for (const auto& x : e.extents)
            if (!img.extent_fits(x.lba, x.size)) throw InputError("/" + e.path + " points past the end of the image: the dump is truncated (nothing written; re-dump the disc)");
    }
    try { makedirs(out_root); } catch (const OSError& e) { throw InputError("cannot create " + out_root + ": " + strerror_of(e.err)); }
    const std::string real_root = realpath(out_root);
    long long written = 0, kept = 0, total = 0;
    std::stable_sort(targets.begin(), targets.end(), [](const Entry& x, const Entry& y) {
        const long long cx = count_char(x.path, '/'), cy = count_char(y.path, '/');
        return cx != cy ? cx < cy : x.path < y.path;
    });
    for (const auto& e : targets) {
        std::string dest = out_root; for (const auto& c : split(e.path, "/")) dest = path_join(dest, c);
        const std::string real_parent = realpath(dirname(dest));
        if (real_parent != real_root && real_parent.rfind(real_root + ossep(), 0) != 0) throw InputError("refusing to write outside " + out_root + ": " + e.path);
        if (e.is_dir) { makedirs(dest); continue; }
        makedirs(dirname(dest));
        if (lexists(dest) && !B(a, "force")) { ++kept; continue; }
        const std::string tmp = dest + ".part";
        std::FILE* f = open_file(tmp, "wb");
        if (!f) throw OSError(errno, tmp);
        try {
            for (const auto& x : e.extents) img.iter_extent(x.lba, x.size, [&](const std::string& c) { std::fwrite(c.data(), 1, c.size(), f); });
        } catch (...) { std::fclose(f); throw; }
        std::fclose(f);
        replace_file(tmp, dest);
        ++written; total += e.size;
    }
    std::optional<Val> boot;
    try { boot = boot_from_dir(out_root); } catch (const InputError&) { boot.reset(); }
    if (B(a, "json")) {
        out(dumps2(dict_of({{"out", Val::str(out_root)}, {"written", Val::integer(written)}, {"kept_existing", Val::integer(kept)}, {"bytes", Val::integer(total)},
            {"boot_elf", boot ? get(*boot, "elf_path") : Val::none()}, {"serial", boot ? get(*boot, "serial") : Val::none()}})));
    } else {
        out(cfmt("Extracted %lld files (%lld bytes) into %s", written, total, out_root.c_str()));
        if (kept) out(cfmt("Kept %lld files that already existed (your edits stay; pass --force to overwrite them)", kept));
        if (boot && truthy(get(*boot, "elf_found"))) {
            out("Boot ELF     " + get(*boot, "elf_path").s + "  (" + (truthy(get(*boot, "serial")) ? get(*boot, "serial").s : std::string("no serial in the name")) + ")");
            out("Run the port with that path as argv[1]: the runtime reads cdrom0: from this folder.");
        }
    }
    return 0;
}

inline int cmd_plain(const Val& a) {
    IsoImage img(S(a, "image"));
    if (img.sector_size == BLOCK) { out(img.path + " already has 2048-byte sectors: use it as it is."); return 0; }
    const std::string o = abspath(S(a, "out"));
    if (exists(o) && !B(a, "force")) { std::fprintf(stderr, "%s: %s already exists (pass --force to replace it)\n", PROG, S(a, "out").c_str()); return 1; }
    if (abspath(img.path) == o) throw InputError("--out must be a new file, not the source image");
    const long long total = img.image_size / img.sector_size;
    const std::string tmp = o + ".part";
    std::FILE* f = open_file(tmp, "wb");
    if (!f) throw InputError("cannot write " + S(a, "out") + ": " + strerror_of(errno));
    for (long long lba = 0; lba < total;) {
        const long long n = std::min(COPY_BLOCKS, total - lba);
        std::string buf;
        try { for (long long i = 0; i < n; ++i) buf += img.read_block(lba + i); } catch (...) { std::fclose(f); throw; }
        if (std::fwrite(buf.data(), 1, buf.size(), f) != buf.size()) { std::fclose(f); throw InputError("cannot write " + S(a, "out") + ": " + std::strerror(errno)); }
        lba += n;
    }
    std::fclose(f);
    try { replace_file(tmp, o); } catch (const OSError& e) { throw InputError("cannot write " + S(a, "out") + ": " + strerror_of(e.err)); }
    out(cfmt("Wrote %s: %lld sectors of 2048 bytes (%lld bytes)", S(a, "out").c_str(), total, total * BLOCK));
    out("It is a copy of your own disc: keep it on this machine.");
    return 0;
}

inline int cmd_boot(const Val& a) {
    const std::string src = S(a, "source");
    Val info;
    if (is_dir(src)) info = boot_from_dir(src);
    else if (is_file(src)) { IsoImage img(src); info = boot_from_iso(img); }
    else throw InputError("no such file or folder: " + src);
    if (B(a, "json")) { out(dumps2(info)); }
    else {
        auto or_ = [&](const char* k, const char* dflt) { const Val v = get(info, k); return truthy(v) ? v.s : std::string(dflt); };
        out("BOOT2        " + get(info, "boot2").s);
        out("Boot ELF     " + get(info, "elf").s);
        out("Serial       " + or_("serial", "(the ELF name is not a serial)"));
        out("Region       " + or_("region", "unknown"));
        out("VER          " + or_("ver", "-"));
        out("VMODE        " + or_("vmode", "-"));
        if (truthy(get(info, "elf_found"))) out(cfmt("Found        %s (%lld bytes)", get(info, "elf_path").s.c_str(), get(info, "elf_size").i));
        else out("Found        NO: the boot ELF named in SYSTEM.CNF is not there");
    }
    return truthy(get(info, "elf_found")) ? 0 : 1;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Read a PS2 disc image you dumped yourself (ISO 9660). Streams; never loads the whole disc.");
    p.subcommands("cmd", true); p.sub_metavar = "COMMAND";
    const auto json = [](Parser& s) { s.opt({"--json"}).store_true().help("machine-readable output"); };
    { Parser& s = p.sub("info"); s.help_line = "volume name, sector layout, and whether the dump is complete";
      s.pos("image").help("disc image (.iso, or raw 2352-byte .bin)"); json(s); }
    { Parser& s = p.sub("ls"); s.help_line = "list a directory on the disc";
      s.pos("image").help("disc image"); s.pos("path").nargs("?").def("").help("folder or file on the disc (default: the root)");
      s.opt({"-r", "--recursive"}).store_true().help("list everything below the folder"); json(s); }
    { Parser& s = p.sub("extract"); s.help_line = "copy the disc (or parts of it) into a folder, keeping the tree";
      s.pos("image").help("disc image"); s.opt({"--out"}).required().help("destination folder (convention: console/disc)");
      s.opt({"--only"}).nargs("+").metavar("PATH").help("only these files or folders (disc paths)");
      s.opt({"--force"}).store_true().help("overwrite files that already exist (default keeps them, so your edits survive)"); json(s); }
    { Parser& s = p.sub("plain"); s.help_line = "convert a raw 2352-byte .bin into a plain 2048-byte-sector .iso";
      s.pos("image").help("raw disc image (.bin, Mode 1 or Mode 2 Form 1)"); s.opt({"--out"}).required().help("the new .iso (refuses to overwrite)");
      s.opt({"--force"}).store_true().help("replace an existing --out file"); }
    { Parser& s = p.sub("boot"); s.help_line = "read SYSTEM.CNF: boot ELF, serial, region, VER, VMODE";
      s.pos("source").help("disc image, or the extracted disc folder"); json(s); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    return guard(PROG, [&]() -> int {
        if (cmd == "info") return cmd_info(a);
        if (cmd == "ls") return cmd_ls(a);
        if (cmd == "extract") return cmd_extract(a);
        if (cmd == "plain") return cmd_plain(a);
        return cmd_boot(a);
    });
}
PACKTOOL("console-reverse/iso", main);
}  // namespace packtools::console_iso
