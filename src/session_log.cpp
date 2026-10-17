// XRShell — Sitzungslog auf dem Gerät. Siehe session_log.h (Ablauf, Grenzen, Rechte).

#include "session_log.h"

#include <android/log.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

// Echte Funktion hinter der Linker-Umleitung (unten).
extern "C" int __real___android_log_print(int prio, const char* tag, const char* fmt, ...);

namespace {

using Clock = std::chrono::steady_clock;

std::mutex gMutex;
FILE* gFile = nullptr;                   // unter gMutex
std::string gDir, gKey, gPath, gHeader;  // unter gMutex
int gPart = 1;
size_t gWritten = 0;                     // Bytes im aktuellen Teil
bool gStarted = false, gDirty = false;
bool gWorldReadable = false;   // Log-Ordner ohne Setgid → Dateien 0664 statt 0660
std::vector<std::string> gEarly;         // Zeilen vor start() (begrenzt)
constexpr size_t kMaxEarly = 400;
Clock::time_point gStart = Clock::now();
std::atomic<int64_t> gLastFlushMs{0};    // für flushIfDue ohne Mutex
char gBuffer[64 * 1024];                 // stdio-Puffer der Datei

int64_t msSinceStart() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - gStart).count();
}

// Zeitstempel „HH:MM:SS.mmm +Sekunden“ wie im Stratagus-Log.
std::string stamp() {
    timespec wall {};
    clock_gettime(CLOCK_REALTIME, &wall);
    tm local {};
    localtime_r(&wall.tv_sec, &local);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03ld +%.3f", local.tm_hour, local.tm_min,
                  local.tm_sec, wall.tv_nsec / 1000000L, static_cast<double>(msSinceStart()) / 1000.0);
    return buf;
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') names.push_back(e->d_name);
        closedir(d);
    }
    return names;
}

bool copyFile(const std::string& from, const std::string& to) {
    FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = std::fopen(to.c_str(), "wb");
    if (out) {
        char buf[8192];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) std::fwrite(buf, 1, n, out);
        std::fclose(out);
        ::chmod(to.c_str(), 0660);
    }
    std::fclose(in);
    return out != nullptr;
}

// Ordner ohne Setgid (z. B. von Hand per chmod verdorben): beiseitelegen, neu anlegen (erbt
// Setgid vom Elternordner), Logs hineinkopieren (erben dessen Gruppe). Muster XR-Stratagus.
void repairDir(const std::string& dir) {
    struct stat st {};
    if (::stat(dir.c_str(), &st) != 0 || (st.st_mode & S_ISGID)) return;
    struct stat parent {};
    const std::string up = dir.substr(0, dir.rfind('/'));
    if (::stat(up.c_str(), &parent) != 0 || !(parent.st_mode & S_ISGID)) return;   // nichts zu erben
    const std::string old = dir + ".alt";
    if (std::rename(dir.c_str(), old.c_str()) != 0) return;
    ::mkdir(dir.c_str(), 0770);
    for (const std::string& name : listDir(old)) {
        copyFile(old + "/" + name, dir + "/" + name);
        std::remove((old + "/" + name).c_str());
    }
    ::rmdir(old.c_str());
}

// Unter gMutex: Datei für Teil gPart öffnen.
bool openPart() {
    gPath = gDir + "/" + slog::partFileName(gKey, gPart);
    gFile = std::fopen(gPath.c_str(), "a");
    gWritten = 0;
    if (!gFile) {
        __real___android_log_print(ANDROID_LOG_ERROR, "XRShell", "Sitzungslog nicht anlegbar: %s (%s)",
                                   gPath.c_str(), std::strerror(errno));
        gPath.clear();
        return false;
    }
    std::setvbuf(gFile, gBuffer, _IOFBF, sizeof(gBuffer));
    // Gruppe (ext_data_rw) darf lesen → adb pull ohne root; ohne Setgid für alle lesbar.
    ::chmod(gPath.c_str(), gWorldReadable ? 0664 : 0660);
    return true;
}

// Unter gMutex: eine fertige Zeile (mit '\n') schreiben, ggf. in den nächsten Teil wechseln.
void writeRaw(const std::string& line) {
    if (!gFile) return;
    if (!slog::fits(gWritten, line.size())) {
        const std::string next = slog::partFileName(gKey, gPart + 1);
        std::fprintf(gFile, "%s [I] XRShell: Größengrenze %zu MB erreicht — weiter in %s\n", stamp().c_str(),
                     slog::kMaxFileBytes / (1024 * 1024), next.c_str());
        std::fclose(gFile);
        gFile = nullptr;
        ++gPart;
        for (int p : slog::partsToDelete(gPart)) std::remove((gDir + "/" + slog::partFileName(gKey, p)).c_str());
        if (!openPart()) return;
        const std::string cont = gHeader + "# Fortsetzung, Teil " + std::to_string(gPart) + "\n";
        std::fwrite(cont.data(), 1, cont.size(), gFile);
        gWritten += cont.size();
    }
    std::fwrite(line.data(), 1, line.size(), gFile);
    gWritten += line.size();
    gDirty = true;
}

// Unter gMutex.
void flushLocked() {
    if (gFile && gDirty) std::fflush(gFile);
    gDirty = false;
    gLastFlushMs.store(msSinceStart());
}

std::string formatLine(int prio, const char* tag, const char* text) {
    std::string line = stamp();
    line += " [";
    line += slog::levelLetter(prio);
    line += "] ";
    line += tag;
    line += ": ";
    line += text;
    while (!line.empty() && line.back() == '\n') line.pop_back();
    line += '\n';
    return line;
}

void record(int prio, const char* tag, const char* text) {
    if (!slog::ownTag(tag)) return;
    const std::string line = formatLine(prio, tag, text);
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gStarted) {
        if (gEarly.size() < kMaxEarly) gEarly.push_back(line);
        return;
    }
    writeRaw(line);
    if (prio >= ANDROID_LOG_ERROR ||
        msSinceStart() - gLastFlushMs.load() >= static_cast<int64_t>(slog::kFlushSeconds * 1000.0))
        flushLocked();
}

} // namespace

// Linker-Umleitung (CMakeLists: -Wl,--wrap=__android_log_print): jeder Aufruf aus libxrshell
// landet hier, geht unverändert nach logcat und — bei eigenen Tags — ins Sitzungslog.
extern "C" int __wrap___android_log_print(int prio, const char* tag, const char* fmt, ...) {
    char text[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    record(prio, tag, text);
    return __android_log_write(prio, tag, text);
}

namespace slog {

bool start(const std::string& logDir, const std::string& header) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gStarted) return gFile != nullptr;
    gStarted = true;
    gDir = logDir;
    // Kein chmod auf den Ordner (Setgid bleibt geerbt), siehe session_log.h.
    repairDir(gDir);
    ::mkdir(gDir.c_str(), 0770);
    // Ohne geerbtes Setgid (files/ per adb angelegt, z. B. von push-game.sh) gehört der Ordner
    // der App-Gruppe — adb (shell) dürfte dann nichts lesen. Dann für alle lesbar machen.
    struct stat dirStat {};
    gWorldReadable = ::stat(gDir.c_str(), &dirStat) == 0 && !(dirStat.st_mode & S_ISGID);
    if (gWorldReadable) {
        ::chmod(gDir.c_str(), 0775);
        for (const std::string& name : listDir(gDir)) ::chmod((gDir + "/" + name).c_str(), 0664);
    }
    for (const std::string& name : sessionsToDelete(listDir(gDir)))
        std::remove((gDir + "/" + name).c_str());
    const time_t now = time(nullptr);
    tm local {};
    localtime_r(&now, &local);
    gKey = sessionKey(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
                      local.tm_sec);
    gPart = 1;
    gHeader = header;
    if (!gHeader.empty() && gHeader.back() != '\n') gHeader += '\n';
    if (!openPart()) {
        gEarly.clear();
        return false;
    }
    std::fwrite(gHeader.data(), 1, gHeader.size(), gFile);
    gWritten += gHeader.size();
    for (const std::string& line : gEarly) writeRaw(line);
    gEarly.clear();
    gEarly.shrink_to_fit();
    flushLocked();
    return true;
}

void flush() {
    std::lock_guard<std::mutex> lock(gMutex);
    flushLocked();
}

void flushIfDue() {
    if (msSinceStart() - gLastFlushMs.load() < static_cast<int64_t>(kFlushSeconds * 1000.0)) return;
    flush();
}

std::string currentPath() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gPath;
}

} // namespace slog
