// XRShell — Spielordner der Bibliothek (xr.dosgames, xr.shooter). Siehe game_folders.h.

#include "game_folders.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace {

constexpr size_t kMaxTitle = 80;   // Bytes; das Menü kürzt sichtbar mit „…“

bool isDirectory(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::string folderGameTitle(const std::string& folder) {
    const size_t slash = folder.find_last_of('/');
    const std::string name = slash == std::string::npos ? folder : folder.substr(slash + 1);
    FILE* f = std::fopen((folder + "/xrshell.title").c_str(), "rb");
    if (!f) return name;
    std::string title;
    char line[256];
    bool first = true;
    while (title.empty() && std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        if (first && s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);   // UTF-8-BOM
        first = false;
        title = trim(s);
    }
    std::fclose(f);
    if (title.size() > kMaxTitle) {
        title.resize(kMaxTitle);
        // kein halbes UTF-8-Zeichen am Ende stehen lassen
        while (!title.empty() && (static_cast<unsigned char>(title.back()) & 0xC0) == 0x80) title.pop_back();
        if (!title.empty() && (static_cast<unsigned char>(title.back()) & 0xC0) == 0xC0) title.pop_back();
    }
    return title.empty() ? name : title;
}

std::vector<FolderGame> scanGameFolders(const std::string& dir) {
    std::vector<FolderGame> games;
    DIR* d = opendir(dir.c_str());
    if (!d) return games;
    while (dirent* entry = readdir(d)) {
        if (entry->d_name[0] == '.') continue;
        const std::string path = dir + "/" + entry->d_name;
        if (!isDirectory(path)) continue;
        games.push_back({path, folderGameTitle(path)});
    }
    closedir(d);
    // readdir-Reihenfolge ist zufällig — stabil sortieren.
    std::sort(games.begin(), games.end(), [](const FolderGame& a, const FolderGame& b) {
        const std::string la = lower(a.title), lb = lower(b.title);
        return la != lb ? la < lb : a.path < b.path;
    });
    return games;
}

// --- xr.shooter: Startdatei eines Spielordners --------------------------------------

namespace {

bool isFile(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

struct DirEntries {
    std::vector<std::string> files, dirs;   // Namen, sortiert
};

DirEntries listDir(const std::string& dir) {
    DirEntries out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* entry = readdir(d)) {
        if (entry->d_name[0] == '.') continue;
        const std::string path = dir + "/" + entry->d_name;
        if (isDirectory(path)) out.dirs.push_back(entry->d_name);
        else if (isFile(path)) out.files.push_back(entry->d_name);
    }
    closedir(d);
    std::sort(out.files.begin(), out.files.end());
    std::sort(out.dirs.begin(), out.dirs.end());
    return out;
}

// Erster Eintrag, dessen Name (Groß/klein egal) wantedLower ist; leer = keiner.
std::string findNoCase(const std::vector<std::string>& names, const std::string& wantedLower) {
    for (const std::string& n : names)
        if (lower(n) == wantedLower) return n;
    return {};
}

std::string extensionLower(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? std::string() : lower(name.substr(dot + 1));
}

std::string doomIwadIn(const std::string& dir) {
    static const char* const kIwads[] = {"doom2.wad", "doom.wad", "doomu.wad", "tnt.wad",
                                         "plutonia.wad", "doom1.wad", "freedoom2.wad",
                                         "freedoom1.wad", "freedm.wad"};
    const DirEntries e = listDir(dir);
    for (const char* iwad : kIwads) {
        const std::string hit = findNoCase(e.files, iwad);
        if (!hit.empty()) return dir + "/" + hit;
    }
    return {};
}

std::string startOverride(const std::string& folder) {
    FILE* f = std::fopen((folder + "/xrshell.start").c_str(), "rb");
    if (!f) return {};
    std::string rel;
    char line[512];
    bool first = true;
    while (rel.empty() && std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        if (first && s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);
        first = false;
        rel = trim(s);
    }
    std::fclose(f);
    for (auto& c : rel) if (c == '\\') c = '/';
    while (!rel.empty() && rel.front() == '/') rel.erase(0, 1);
    if (rel.empty() || rel.find("..") != std::string::npos) return {};
    const std::string path = folder + "/" + rel;
    return isFile(path) ? path : std::string();
}

} // namespace

std::string findStartFile(const std::string& folder, StartRule rule) {
    if (rule == StartRule::None || !isDirectory(folder)) return {};
    const std::string chosen = startOverride(folder);
    if (!chosen.empty()) return chosen;
    switch (rule) {
    case StartRule::DoomIwad: {
        std::string hit = doomIwadIn(folder);
        if (!hit.empty()) return hit;
        for (const std::string& sub : listDir(folder).dirs) {   // z. B. base/, rerelease/
            hit = doomIwadIn(folder + "/" + sub);
            if (!hit.empty()) return hit;
        }
        return {};
    }
    case StartRule::WolfData: {
        static const char* const kExts[] = {"wl6", "sod", "wl1", "sdm", "n3d"};
        const DirEntries e = listDir(folder);
        for (const char* ext : kExts) {
            const std::string vswap = findNoCase(e.files, std::string("vswap.") + ext);
            if (!vswap.empty()) return folder + "/" + vswap;
            for (const std::string& f : e.files)
                if (extensionLower(f) == ext) return folder + "/" + f;
        }
        return {};
    }
    case StartRule::QuakePak: {
        const std::string id1 = findNoCase(listDir(folder).dirs, "id1");
        if (id1.empty()) return {};
        const std::string pak = findNoCase(listDir(folder + "/" + id1).files, "pak0.pak");
        return pak.empty() ? std::string() : folder + "/" + id1 + "/" + pak;
    }
    case StartRule::ScummTarget:
        for (const std::string& f : listDir(folder).files)
            if (extensionLower(f) == "scummvm") return folder + "/" + f;
        return {};
    case StartRule::None:
        break;
    }
    return {};
}
