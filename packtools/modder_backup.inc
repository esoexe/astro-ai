// modder_backup.inc - `um backup`: a C++ port of universal-modder's um/backup.py (Rehan and universal-modder
// contributors, MIT). Snapshots folders (saves, profiles, config) into zip files with a manifest (size + sha1 per
// file) under ~/.universal-modder/backups/<name>/, diffs a folder against its latest snapshot, and restores one
// (snapshotting the current state first, so a restore can itself be undone).
namespace packtools::modder::backup {
using namespace pyrt;
using namespace packtools::modder;

inline std::string root_of(const std::string& name) { const std::string d = path_join(path_join(data_dir(), "backups"), name); std::error_code ec; fs::create_directories(P(d), ec); return d; }
inline Val scan_dir(const std::string& src) {
    Val files = Val::dict();
    for (const auto& rel : rglob_files(src)) {
        const std::string full = path_join(src, rel);
        Val f = Val::dict(); f.d->set("size", Val::integer(fsize(full))); f.d->set("sha1", Val::str(sha1_file(full)));
        files.d->set(rel, f);
    }
    return files;
}
inline std::string stamp_now() {
    if (!env("UM_FAKE_STAMP").empty()) return env("UM_FAKE_STAMP");   // tests: several backups "in the same second"
    const std::time_t t = std::time(nullptr); std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32]; std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm); return b;
}
inline std::time_t mtime_t(const std::string& p) {
    std::error_code ec; const auto ft = fs::last_write_time(P(p), ec);
    if (ec) return std::time(nullptr);
    const auto sys = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now());
    return std::chrono::system_clock::to_time_t(sys);
}
// open(path, "x"): create the file only if it does not exist yet (false when it does)
inline bool create_exclusive(const std::string& p) {
#ifdef _WIN32
    const int fd = _wopen(P(p).c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) { if (errno == EEXIST) return false; throw OSError(errno, p); }
    _close(fd); return true;
#else
    const int fd = ::open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) { if (errno == EEXIST) return false; throw OSError(errno, p); }
    ::close(fd); return true;
#endif
}
inline std::string create(const std::string& src, std::string name = "", const std::string& note = "") {
    const std::string s = expanduser(to_posix(src));
    if (!isdir(s)) die("not a folder: " + s);
    if (name.empty()) name = lower(replace_all(u8native(P(s).filename()).empty() ? u8native(P(s).parent_path().filename()) : u8native(P(s).filename()), " ", "-"));
    const Val files = scan_dir(s);
    long long total = 0; for (const auto& [k, f] : files.d->items) total += get(f, "size").i;
    if (total > (20LL << 30)) die(fmt_f(static_cast<double>(total) / 1073741824.0, 1) + " GB - too big to snapshot casually; back up the specific subfolder you'll change");
    const std::string stamp = stamp_now(), root = root_of(name);
    // never reuse gaps left by deleted or failed backups: a new snapshot sorts after the existing ones
    long long sequence = 0;
    { std::error_code ec;
      for (fs::directory_iterator it(P(root), ec), end; !ec && it != end; it.increment(ec)) {
          const std::string n = u8native(it->path().filename());
          if (starts(n, stamp + "_") && ends(n, ".zip")) { const std::string mid = n.substr(stamp.size() + 1, n.size() - stamp.size() - 5); if (!mid.empty() && std::all_of(mid.begin(), mid.end(), [](char c) { return c >= '0' && c <= '9'; })) sequence = std::max(sequence, std::stoll(mid) + 1); }
      } }
    std::string out;
    for (;;) {
        char suf[32] = ""; if (sequence) std::snprintf(suf, sizeof suf, "_%06lld", sequence);
        out = path_join(root, stamp + suf + ".zip");
        // exclusive creation also keeps concurrent backups from overwriting each other
        if (create_exclusive(out)) break;
        ++sequence;
    }
    try {
        ZipWriter z(out);
        for (const auto& [rel, f] : files.d->items) { const std::string full = path_join(s, rel); z.add(rel, slurp(full), mtime_t(full)); }
        Val m = Val::dict(); m.d->set("source", Val::str(src)); m.d->set("created", Val::str(stamp)); m.d->set("note", Val::str(note)); m.d->set("files", files);
        DumpOpts o; o.indent = 1; z.add("_um_manifest.json", dumps(m, o), std::time(nullptr));
        z.close();
    } catch (...) { std::error_code ec; fs::remove(P(out), ec); throw; }   // never leave an incomplete archive as the latest snapshot
    print(out + "  (" + std::to_string(files.d->items.size()) + " files, " + fmt_f(static_cast<double>(total) / 1048576.0, 1) + " MB)");
    return out;
}
inline std::vector<std::string> snapshots(const std::string& name) {
    std::vector<std::string> v; std::error_code ec;
    for (fs::directory_iterator it(P(root_of(name)), ec), end; !ec && it != end; it.increment(ec)) if (ends(u8native(it->path().filename()), ".zip")) v.push_back(u8native(it->path()));
    std::sort(v.begin(), v.end()); return v;
}
inline Val manifest(const std::string& zp) { const std::string z = slurp(zp); if (z.empty()) throw OSError(2, zp); return loads(pyrt::zip::read(z, "_um_manifest.json")); }
inline std::vector<std::string> sorted_keys(const Val& d) { std::vector<std::string> v; for (const auto& [k, x] : d.d->items) v.push_back(k); std::sort(v.begin(), v.end()); return v; }
inline Val diff(const std::string& name, const std::string& target = "", const std::string& snapshot = "") {
    std::string zp = snapshot; if (zp.empty()) { const auto s = snapshots(name); if (!s.empty()) zp = s.back(); }
    if (zp.empty()) die("no snapshots for " + name);
    const Val m = manifest(zp);
    const std::string t = to_posix(!target.empty() ? target : get(m, "source").s);
    const Val now = isdir(t) ? scan_dir(t) : Val::dict(); const Val old = get(m, "files");
    Val added = Val::list(), removed = Val::list(), changed = Val::list();
    for (const auto& k : sorted_keys(now)) if (!old.d->has(k)) added.l->push_back(Val::str(k));
    for (const auto& k : sorted_keys(old)) if (!now.d->has(k)) removed.l->push_back(Val::str(k));
    for (const auto& k : sorted_keys(now)) if (old.d->has(k) && get(*now.d->get(k), "sha1").s != get(*old.d->get(k), "sha1").s) changed.l->push_back(Val::str(k));
    Val r = Val::dict(); r.d->set("snapshot", Val::str(zp)); r.d->set("target", Val::str(t)); r.d->set("added", added); r.d->set("removed", removed); r.d->set("changed", changed);
    return r;
}
inline void restore(const std::string& name, const std::string& to, const std::string& snapshot, bool clean, bool yes) {
    std::string zp = snapshot; if (zp.empty()) { const auto s = snapshots(name); if (!s.empty()) zp = s.back(); }
    if (zp.empty()) die("no snapshots for " + name);
    const Val m = manifest(zp);
    const std::string t = to_posix(!to.empty() ? to : get(m, "source").s);
    const Val d = diff(name, t, zp);
    print("restore " + u8native(P(zp).filename()) + " -> " + t + ": " + std::to_string(get(d, "changed").size()) + " changed, " + std::to_string(get(d, "removed").size()) + " missing, " +
          std::to_string(get(d, "added").size()) + " new since" + (clean ? " (new files will be deleted: --clean)" : " (new files kept)"));
    if (!yes) die("re-run with --yes to do it");
    if (isdir(t)) create(t, name + "-pre-restore", "automatic, before restoring " + u8native(P(zp).filename()));
    std::error_code ec; fs::create_directories(P(t), ec);
    const std::string z = slurp(zp);
    for (const auto& [rel, f] : get(m, "files").d->items) spit(path_join(t, rel), pyrt::zip::read(z, rel));
    if (clean) for (const auto& rel : *get(d, "added").l) fs::remove(P(path_join(t, rel.s)), ec);
    print("restored " + std::to_string(get(m, "files").d->items.size()) + " files");
}

inline const char* DOC =
    "Snapshot folders before touching them (saves, profiles, config, the game's data folder).\n\n"
    "    um backup create \"C:\\\\Users\\\\me\\\\Documents\\\\My Games\\\\Terraria\" --name terraria-saves\n"
    "    um backup list [name]\n"
    "    um backup diff terraria-saves \"C:\\\\Users\\\\me\\\\Documents\\\\My Games\\\\Terraria\"     # what changed since the last snapshot\n"
    "    um backup restore terraria-saves [--to DIR] [--snapshot FILE] [--yes]\n\n"
    "Snapshots are zip files + a manifest (size + sha1 per file) in ~/.universal-modder/backups/<name>/.\n"
    "Snapshots created in the same second get numbered suffixes so earlier backups are never overwritten.\n"
    "restore first snapshots the current state (so a restore can itself be undone), then puts every file\n"
    "back and removes files that weren't in the snapshot only with --clean.\n"
    "Habit that saved the Terraria showcase: keep a pristine copy of any world/scenario a scripted take\n"
    "destroys, and restore it before each take.\n";
inline void reg(Parser& sub) {
    Parser& p = sub.sub("backup", DOC); p.help_line = "snapshot / diff / restore save folders before you touch them"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("create"); q.help_line = "snapshot a folder"; q.pos("src"); q.opt({"--name"}); q.opt({"--note"}); }
    { Parser& q = p.sub("list"); q.help_line = "list snapshots"; q.pos("name").nargs("?"); }
    { Parser& q = p.sub("diff"); q.help_line = "what changed since the latest snapshot"; q.pos("name"); q.pos("target").nargs("?"); q.opt({"--snapshot"}); }
    { Parser& q = p.sub("restore"); q.help_line = "restore the latest (or --snapshot) snapshot"; q.pos("name"); q.opt({"--to"}); q.opt({"--snapshot"});
      q.opt({"--clean"}).store_true().help("also delete files created after the snapshot"); q.opt({"--yes"}).store_true(); }
}
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "create") create(S(a, "src"), S(a, "name"), S(a, "note"));
    else if (c == "list") {
        const std::string root = path_join(data_dir(), "backups");
        std::vector<std::string> names;
        if (!S(a, "name").empty()) names.push_back(S(a, "name"));
        else if (isdir(root)) { std::error_code ec; for (fs::directory_iterator it(P(root), ec), end; !ec && it != end; it.increment(ec)) if (it->is_directory(ec)) names.push_back(u8native(it->path().filename())); std::sort(names.begin(), names.end()); }
        for (const auto& n : names) for (const auto& zp : snapshots(n)) {
            const Val m = manifest(zp);
            print(ljust(n, 28) + " " + u8native(P(zp).filename()) + "  " + rjust(std::to_string(get(m, "files").size()), 5) + " files  " + pystr(get(m, "source")) + "  " + (has(m, "note") ? pystr(get(m, "note")) : std::string()));
        }
    }
    else if (c == "diff") { DumpOpts o; o.indent = 1; print(dumps(diff(S(a, "name"), S(a, "target"), S(a, "snapshot")), o)); }
    else if (c == "restore") restore(S(a, "name"), S(a, "to"), S(a, "snapshot"), truthy(get(a, "clean")), truthy(get(a, "yes")));
    return 0;
}
}  // namespace packtools::modder::backup
