// Host-Test für src/key_profile.cpp (xrshell.keys): Parser, Profilsuche und
// Tastentiming. Adapter und Android-Log sind Stubs (tests/stubs/), gebaut und
// ausgeführt von tests/run-host-tests.sh — aus dem Repo-Stamm starten.
//
// Sweeps: jede Eingabe der mitgelieferten Profile; Kombinationen und kurze Tipps
// bei 72/90/120 Hz × Haltedauer 1 Frame … 0,3 s; Zeilenenden LF/CRLF.

#include "key_profile.h"
#include "libretro_adapter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int gChecks = 0, gFailures = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("FEHLER: %s\n", what.c_str());
    }
}

struct TimedEvent { enum retro_key key; bool pressed; double t; };

// Hält eine Eingabe `hold` Sekunden bei `hz` Bildrate und läuft danach weiter,
// bis alle Tasten los sind (höchstens 1 s). Liefert die Tastenereignisse mit Zeit.
std::vector<TimedEvent> simulate(KeyProfile& profile, KeyProfile::Input input,
                                 double hold, double hz) {
    LibretroAdapter adapter;
    std::vector<TimedEvent> out;
    const double dt = 1.0 / hz;
    size_t seen = 0;
    for (int frame = 0; frame < static_cast<int>(hz * (hold + 1.0)); ++frame) {
        const double t = frame * dt;
        bool pressed[KeyProfile::kInputCount] = {};
        pressed[input] = t < hold;
        profile.update(pressed, 1000.0 + t, adapter);   // beliebiger Zeitursprung
        for (; seen < adapter.events.size(); ++seen)
            out.push_back({adapter.events[seen].first, adapter.events[seen].second, t});
    }
    return out;
}

std::string repoFile(const char* rel) { return std::string(rel); }

void writeFile(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary);
    f << text;
}

// --- 1. Mitgelieferte Profile: jede belegte Eingabe erzeugt genau ihre Tasten ---
void testShippedProfiles(const fs::path& tmp) {
    struct Expect { KeyProfile::Input input; std::vector<enum retro_key> keys; };
    struct Case { const char* file; std::vector<Expect> expect; std::vector<KeyProfile::Input> free; };
    const Case cases[] = {
        {"profiles/dungeon-keeper-1.keys",
         {{KeyProfile::kLeftStickLeft, {RETROK_DELETE}},
          {KeyProfile::kLeftStickRight, {RETROK_PAGEDOWN}},
          {KeyProfile::kLeftStickUp, {RETROK_HOME}},
          {KeyProfile::kLeftStickDown, {RETROK_END}},
          {KeyProfile::kA, {RETROK_LALT, RETROK_r}},
          {KeyProfile::kX, {RETROK_LSHIFT}},
          {KeyProfile::kLeftTrigger, {RETROK_h}},
          {KeyProfile::kRightStickClick, {RETROK_ESCAPE}},
          {KeyProfile::kHires, {RETROK_LALT, RETROK_r}}},
         {KeyProfile::kY}},   // Y bleibt Bildschirmtastatur
        {"profiles/broken-sword-1.keys",
         {{KeyProfile::kA, {RETROK_LCTRL, RETROK_F5}},
          {KeyProfile::kRightStickClick, {RETROK_ESCAPE}},
          {KeyProfile::kGameMenu, {RETROK_LCTRL, RETROK_F5}}},
         {KeyProfile::kX, KeyProfile::kY, KeyProfile::kLeftTrigger}},
        {"profiles/broken-sword-2.keys",
         {{KeyProfile::kA, {RETROK_LCTRL, RETROK_F5}},
          {KeyProfile::kRightStickClick, {RETROK_ESCAPE}},
          {KeyProfile::kGameMenu, {RETROK_LCTRL, RETROK_F5}}},
         {KeyProfile::kX, KeyProfile::kY, KeyProfile::kLeftTrigger}},
        {"profiles/theme-park.keys",
         {{KeyProfile::kA, {RETROK_r}}, {KeyProfile::kHires, {RETROK_r}}},
         {KeyProfile::kX, KeyProfile::kY, KeyProfile::kLeftTrigger, KeyProfile::kRightStickClick,
          KeyProfile::kLeftStickLeft, KeyProfile::kLeftStickRight, KeyProfile::kLeftStickUp,
          KeyProfile::kLeftStickDown}},
        // xr.dosgames (docs/DOS-SPIELE.md): X = Bildzoom und Y = Bildschirmtastatur bleiben überall frei.
        {"profiles/pirates-gold.keys",
         {{KeyProfile::kLeftStickLeft, {RETROK_LEFT}},
          {KeyProfile::kLeftStickRight, {RETROK_RIGHT}},
          {KeyProfile::kLeftStickUp, {RETROK_UP}},
          {KeyProfile::kLeftStickDown, {RETROK_DOWN}},
          {KeyProfile::kA, {RETROK_RETURN}},
          {KeyProfile::kLeftTrigger, {RETROK_SPACE}},
          {KeyProfile::kRightStickClick, {RETROK_ESCAPE}}},
         {KeyProfile::kX, KeyProfile::kY}},
        {"profiles/pirates.keys",
         {{KeyProfile::kLeftStickLeft, {RETROK_LEFT}},
          {KeyProfile::kLeftStickRight, {RETROK_RIGHT}},
          {KeyProfile::kLeftStickUp, {RETROK_UP}},
          {KeyProfile::kLeftStickDown, {RETROK_DOWN}},
          {KeyProfile::kA, {RETROK_RETURN}},
          {KeyProfile::kLeftTrigger, {RETROK_RETURN}},   // Feuerknopf = ENTER (DISKP)
          {KeyProfile::kRightStickClick, {RETROK_ESCAPE}}},
         {KeyProfile::kX, KeyProfile::kY}},
        {"profiles/populous-2.keys",
         {{KeyProfile::kLeftStickUp, {RETROK_KP_MINUS}},
          {KeyProfile::kLeftStickDown, {RETROK_KP_PLUS}},
          {KeyProfile::kLeftStickLeft, {RETROK_MINUS}},
          {KeyProfile::kLeftStickRight, {RETROK_LSHIFT, RETROK_EQUALS}},
          {KeyProfile::kA, {RETROK_F7}},
          {KeyProfile::kLeftTrigger, {RETROK_F10}}},
         {KeyProfile::kX, KeyProfile::kY, KeyProfile::kRightStickClick}},
        {"profiles/simcity-2000.keys",
         {{KeyProfile::kLeftTrigger, {RETROK_LSHIFT}},
          {KeyProfile::kA, {RETROK_LCTRL}},
          {KeyProfile::kRightStickClick, {RETROK_ESCAPE}}},
         {KeyProfile::kX, KeyProfile::kY, KeyProfile::kLeftStickLeft, KeyProfile::kLeftStickRight,
          KeyProfile::kLeftStickUp, KeyProfile::kLeftStickDown}},
        {"profiles/syndicate-wars.keys",
         {{KeyProfile::kLeftStickLeft, {RETROK_DELETE}},
          {KeyProfile::kLeftStickRight, {RETROK_PAGEDOWN}},
          {KeyProfile::kLeftStickUp, {RETROK_HOME}},
          {KeyProfile::kLeftStickDown, {RETROK_END}},
          {KeyProfile::kA, {RETROK_PAGEUP}},
          {KeyProfile::kLeftTrigger, {RETROK_INSERT}},
          {KeyProfile::kRightStickClick, {RETROK_ESCAPE}},
          {KeyProfile::kHires, {RETROK_F8}}},
         {KeyProfile::kX, KeyProfile::kY}},
    };
    for (const Case& c : cases) {
        // Profil wie auf dem Gerät als xrshell.keys in einen Spielordner legen.
        const fs::path game = tmp / "shipped" / fs::path(c.file).stem();
        fs::create_directories(game);
        fs::copy_file(repoFile(c.file), game / "xrshell.keys", fs::copy_options::overwrite_existing);
        KeyProfile profile;
        check(profile.load(game.generic_string()), std::string(c.file) + ": lädt");
        for (const Expect& e : c.expect) {
            check(profile.bound(e.input), std::string(c.file) + ": Eingabe belegt");
            const auto ev = simulate(profile, e.input, 0.2, 90.0);
            std::vector<enum retro_key> downs;
            for (const auto& x : ev) if (x.pressed) downs.push_back(x.key);
            check(downs == e.keys, std::string(c.file) + ": Tasten/Reihenfolge der Eingabe " +
                                   std::to_string(e.input));
            check(ev.size() == 2 * e.keys.size(), std::string(c.file) + ": jede Taste genau einmal ab und auf");
        }
        for (KeyProfile::Input in : c.free)
            check(!profile.bound(in), std::string(c.file) + ": Eingabe bleibt frei " + std::to_string(in));
    }
    // Populous: Mausspiel, Controller-Tasten frei; nur die Scroll-Tasten fürs Karte ziehen
    // (Ziffernblock 4/6/8/2 — die grauen Pfeiltasten ←/→ scrollen dort nicht zuverlässig).
    const fs::path pop = tmp / "shipped" / "populous";
    fs::create_directories(pop);
    fs::copy_file("profiles/populous.keys", pop / "xrshell.keys", fs::copy_options::overwrite_existing);
    KeyProfile p;
    check(p.load(pop.generic_string()), "profiles/populous.keys: lädt");
    for (int i = 0; i < KeyProfile::kScrollLeft; ++i)
        check(!p.bound(static_cast<KeyProfile::Input>(i)), "profiles/populous.keys: Controller-Taste frei " + std::to_string(i));
    const enum retro_key pad[4] = {RETROK_KP4, RETROK_KP6, RETROK_KP8, RETROK_KP2};
    for (int i = 0; i < 4; ++i) {
        const auto in = static_cast<KeyProfile::Input>(KeyProfile::kScrollLeft + i);
        const auto ev = simulate(p, in, 0.1, 90.0);
        check(p.bound(in) && ev.size() == 2 && ev[0].key == pad[i], "profiles/populous.keys: Scroll-Taste Ziffernblock");
    }
}

// --- 1c. Scroll-Tasten (Karte ziehen, map_drag.h): Standard Pfeiltasten, Profil überschreibt ---
void testScrollKeys(const fs::path& tmp) {
    const enum retro_key arrows[4] = {RETROK_LEFT, RETROK_RIGHT, RETROK_UP, RETROK_DOWN};
    for (int withFile = 0; withFile < 2; ++withFile) {
        const fs::path game = tmp / "scroll" / (withFile ? "mit" : "ohne");
        fs::create_directories(game);
        if (withFile) writeFile(game / "xrshell.keys", "a = R\n");
        KeyProfile p;
        check(p.load(game.generic_string()) == (withFile != 0), "Scroll: Laden");
        check(p.empty() == (withFile == 0), "Scroll: Standard-Pfeiltasten zählen nicht als Profil");
        for (int i = 0; i < 4; ++i) {
            const auto in = static_cast<KeyProfile::Input>(KeyProfile::kScrollLeft + i);
            check(!p.bound(in) && p.bindingText(in).empty(), "Scroll: Standard gilt nicht als belegt");
            // Takt wie map_drag (60 ms Druck) und ganz kurze Tipps: immer Pfeiltaste, ≥ 50 ms
            for (double hold : {0.001, 0.06, 0.5}) {
                const auto ev = simulate(p, in, hold, 72.0);
                check(ev.size() == 2 && ev[0].key == arrows[i] && ev[0].pressed && !ev[1].pressed &&
                          ev[1].t - ev[0].t >= 0.05 - 1e-9,
                      "Scroll: Pfeiltaste " + std::to_string(i) + " ab/auf, ≥ 50 ms");
            }
        }
    }
    // Profil überschreibt (auch Kombination), Spielwechsel stellt den Standard wieder her.
    const fs::path game = tmp / "scroll" / "eigen";
    fs::create_directories(game);
    writeFile(game / "xrshell.keys", "scroll_left = KP4\nscroll_up = LCTRL+UP\n");
    KeyProfile p;
    check(p.load(game.generic_string()) && p.bound(KeyProfile::kScrollLeft) && !p.bound(KeyProfile::kScrollRight),
          "Scroll: eigene Belegung");
    check(simulate(p, KeyProfile::kScrollLeft, 0.1, 90.0)[0].key == RETROK_KP4, "Scroll: scroll_left = KP4");
    check(simulate(p, KeyProfile::kScrollRight, 0.1, 90.0)[0].key == RETROK_RIGHT, "Scroll: rechts bleibt Standard");
    const auto up = simulate(p, KeyProfile::kScrollUp, 0.1, 90.0);
    check(up.size() == 4 && up[0].key == RETROK_LCTRL && up[1].key == RETROK_UP, "Scroll: Kombination STRG+HOCH");
    p.load((tmp / "scroll" / "ohne").generic_string());
    check(simulate(p, KeyProfile::kScrollLeft, 0.1, 90.0)[0].key == RETROK_LEFT, "Scroll: Spielwechsel → Standard");

    // releaseNow: sofort los (ohne Mindest-Haltezeit), holding() folgt.
    LibretroAdapter a;
    bool in[KeyProfile::kInputCount] = {};
    in[KeyProfile::kScrollDown] = true;
    p.update(in, 5.0, a);
    check(p.holding(KeyProfile::kScrollDown) && a.events.size() == 1, "releaseNow: Taste unten");
    p.releaseNow(KeyProfile::kScrollDown, a);
    check(!p.holding(KeyProfile::kScrollDown) && a.events.size() == 2 && !a.events[1].second &&
              a.events[1].first == RETROK_DOWN,
          "releaseNow: sofort los");
    in[KeyProfile::kScrollDown] = false;
    p.update(in, 5.01, a);
    check(a.events.size() == 2, "releaseNow: kein doppeltes Loslassen");
    p.releaseNow(KeyProfile::kScrollDown, a);
    check(a.events.size() == 2, "releaseNow ohne gehaltene Taste: nichts");
}

// --- 1b. Spielwechsel (xr.dosgames): neues Profil ersetzt das alte vollständig ---
void testReload(const fs::path& tmp) {
    const fs::path a = tmp / "wechsel" / "A", b = tmp / "wechsel" / "B", c = tmp / "wechsel" / "C";
    fs::create_directories(a);
    fs::create_directories(b);
    fs::create_directories(c);
    writeFile(a / "xrshell.keys", "a = R\nx = LSHIFT\nleft_stick_up = HOME\n");
    writeFile(b / "xrshell.keys", "left_trigger = F10\n");
    KeyProfile p;
    check(p.load(a.generic_string()) && p.bound(KeyProfile::kX), "Wechsel: Profil A geladen");
    check(p.load(b.generic_string()), "Wechsel: Profil B geladen");
    check(!p.bound(KeyProfile::kA) && !p.bound(KeyProfile::kX) && !p.bound(KeyProfile::kLeftStickUp),
          "Wechsel: Belegung von A verworfen");
    check(p.bound(KeyProfile::kLeftTrigger), "Wechsel: Belegung von B aktiv");
    check(!p.load(c.generic_string()) && p.empty(), "Wechsel: Spiel ohne Profil → leer");
    // Menü-Aktion „Hohe Auflösung“: Belegung samt Text für die Knopfbeschriftung, nur sie belegt.
    writeFile(c / "xrshell.keys", std::string("hires =  lalt+R   # 320x200 <-> 640x480") + '\r' + '\n');
    check(p.load(c.generic_string()) && p.bound(KeyProfile::kHires) && p.bindingText(KeyProfile::kHires) == "lalt+R",
          "hires: belegt, Text ohne Kommentar/Leerraum");
    check(!p.bound(KeyProfile::kA) && p.bindingText(KeyProfile::kA).empty(), "hires: belegt keine Controller-Taste");
    check(p.load(b.generic_string()) && p.bindingText(KeyProfile::kHires).empty(), "hires: Spielwechsel verwirft den Text");
    // game_menu: eingebauter Standard (ScummVM Strg+F5) nur, wenn das Profil nichts sagt.
    p.setDefault(KeyProfile::kGameMenu, "LCTRL+F5");
    check(p.bindingText(KeyProfile::kGameMenu) == "LCTRL+F5" && !p.bound(KeyProfile::kGameMenu),
          "game_menu: Standard gesetzt, zählt nicht als Profilbelegung");
    writeFile(c / "xrshell.keys", std::string("game_menu = F5") + '\n');
    check(p.load(c.generic_string()), "game_menu: Profil geladen");
    p.setDefault(KeyProfile::kGameMenu, "LCTRL+F5");
    check(p.bindingText(KeyProfile::kGameMenu) == "F5" && p.bound(KeyProfile::kGameMenu), "game_menu: Profil hat Vorrang");
    p.setDefault(KeyProfile::kX, "QUATSCH");
    check(!p.bound(KeyProfile::kX) && p.bindingText(KeyProfile::kX).empty(), "setDefault: unbekannte Taste ignoriert");
}

// --- 2. Timing-Sweep: Kombination und Einzeltaste, Bildraten × Haltedauer ---
void testTimingSweep(const fs::path& tmp) {
    const fs::path game = tmp / "timing";
    fs::create_directories(game);
    writeFile(game / "xrshell.keys", "a = LALT+R\nx = LSHIFT\n");
    const double rates[] = {72.0, 90.0, 120.0};
    const double holds[] = {0.0001, 0.01, 0.03, 0.049, 0.05, 0.051, 0.08, 0.1, 0.15, 0.3};
    for (double hz : rates) {
        for (double hold : holds) {
            const double frame = 1.0 / hz;
            const std::string tag = " @" + std::to_string(static_cast<int>(hz)) + " Hz, " +
                                    std::to_string(hold) + " s";
            {   // Kombination LALT+R
                KeyProfile p; p.load(game.generic_string());
                const auto ev = simulate(p, KeyProfile::kA, hold, hz);
                const bool shape = ev.size() == 4 &&
                    ev[0].key == RETROK_LALT && ev[0].pressed &&
                    ev[1].key == RETROK_r && ev[1].pressed &&
                    ev[2].key == RETROK_r && !ev[2].pressed &&
                    ev[3].key == RETROK_LALT && !ev[3].pressed;
                check(shape, "Kombi: ALT ab, R ab, R auf, ALT auf" + tag);
                if (!shape) continue;
                check(ev[1].t - ev[0].t >= 0.05 - 1e-9, "Kombi: R frühestens 50 ms nach ALT" + tag);
                check(ev[1].t - ev[0].t < 0.05 + frame + 1e-9, "Kombi: R höchstens 1 Frame zu spät" + tag);
                check(ev[2].t - ev[1].t >= 0.05 - 1e-9, "Kombi: R mindestens 50 ms gehalten" + tag);
                // Nie hängen bleiben: spätestens 2 Frames nach max(Loslassen, 100 ms) (je Schritt ≤ 1 Frame Verzug).
                check(ev[3].t <= std::max(hold, 0.1) + 2 * frame + 1e-9, "Kombi: löst rechtzeitig" + tag);
            }
            {   // Einzeltaste: kurzer Tipp wird auf ≥ 50 ms gestreckt, langer folgt dem Knopf
                KeyProfile p; p.load(game.generic_string());
                const auto ev = simulate(p, KeyProfile::kX, hold, hz);
                check(ev.size() == 2 && ev[0].pressed && !ev[1].pressed &&
                      ev[0].key == RETROK_LSHIFT && ev[1].key == RETROK_LSHIFT,
                      "Einzeltaste: ab und auf" + tag);
                if (ev.size() != 2) continue;
                const double held = ev[1].t - ev[0].t;
                check(held >= 0.05 - 1e-9, "Einzeltaste: mindestens 50 ms" + tag);
                check(held <= std::max(hold, 0.05) + frame + 1e-9, "Einzeltaste: nicht länger als nötig" + tag);
            }
        }
    }
}

// --- 3. Referenzzählung: zwei Eingaben halten dieselbe Taste ---
void testSharedKey(const fs::path& tmp) {
    const fs::path game = tmp / "shared";
    fs::create_directories(game);
    writeFile(game / "xrshell.keys", "x = LSHIFT\nleft_trigger = LSHIFT\n");
    KeyProfile p; p.load(game.generic_string());
    LibretroAdapter a;
    bool in[KeyProfile::kInputCount] = {};
    auto step = [&](double t) { p.update(in, t, a); };
    in[KeyProfile::kX] = true; step(0.0);
    in[KeyProfile::kLeftTrigger] = true; step(0.1);
    in[KeyProfile::kX] = false; step(0.2);
    check(a.events.size() == 1, "geteilt: SHIFT bleibt unten, solange eine Eingabe hält");
    in[KeyProfile::kLeftTrigger] = false; step(0.3);
    check(a.events.size() == 2 && !a.events[1].second, "geteilt: SHIFT los, wenn beide los");
}

// --- 4. Parser: Kommentare, Groß/klein, CRLF, Fehlerzeilen, alle Tastennamen ---
void testParser(const fs::path& tmp) {
    const fs::path game = tmp / "parser";
    fs::create_directories(game);
    for (const char* eol : {"\n", "\r\n"}) {
        std::string text;
        for (const char* l : {"# Kommentar", "", "   A   =   esc   # Kommentar hinter Wert",
                              "unbekannt = H", "y = NICHTDA", "x = LSHIFT+", "RIGHT_STICK_CLICK=f12",
                              "left_stick_up = kp_plus"})
            (text += l) += eol;
        writeFile(game / "xrshell.keys", text);
        KeyProfile p;
        check(p.load(game.generic_string()), std::string("Parser lädt trotz Fehlerzeilen, eol=") +
                                             (eol[1] ? "CRLF" : "LF"));
        check(p.bound(KeyProfile::kA) && p.bound(KeyProfile::kRightStickClick) &&
              p.bound(KeyProfile::kLeftStickUp), "Parser: gültige Zeilen übernommen");
        check(!p.bound(KeyProfile::kY), "Parser: unbekannte Taste verwirft die Zeile");
        check(!p.bound(KeyProfile::kX), "Parser: leerer Kombi-Teil verwirft die Zeile");
        const auto ev = simulate(p, KeyProfile::kRightStickClick, 0.1, 90.0);
        check(!ev.empty() && ev[0].key == RETROK_F12, "Parser: F12");
    }
    // Jeder dokumentierte Tastenname (TEST.md 3a) wird erkannt.
    const char* names[] = {"a", "z", "0", "9", "f1", "f12", "space", "enter", "return", "esc", "escape",
                           "tab", "backspace", "insert", "delete", "home", "end", "pageup", "pagedown",
                           "up", "down", "left", "right", "lshift", "rshift", "lctrl", "rctrl", "lalt",
                           "ralt", "pause", "minus", "equals", "comma", "period", "kp_plus", "kp_minus",
                           "kp0", "kp1", "kp2", "kp3", "kp4", "kp5", "kp6", "kp7", "kp8", "kp9"};
    for (const char* n : names) {
        writeFile(game / "xrshell.keys", std::string("a = ") + n + "\n");
        KeyProfile p; p.load(game.generic_string());
        check(p.bound(KeyProfile::kA), std::string("Tastenname erkannt: ") + n);
    }
    for (const char* bad : {"f0", "f13", "ctrl", "shift", "aa", ""}) {
        writeFile(game / "xrshell.keys", std::string("a = ") + bad + "\n");
        KeyProfile p;
        check(!p.load(game.generic_string()), std::string("ungültiger Tastenname abgelehnt: '") + bad + "'");
    }
}

// --- 5. Profilsuche: Spielordner, Spieldatei, game/-Ebene, kein Profil ---
void testLookup(const fs::path& tmp) {
    const fs::path root = tmp / "lookup" / "game";
    fs::create_directories(root / "MitProfil");
    fs::create_directories(root / "OhneProfil");
    writeFile(root / "MitProfil" / "xrshell.keys", "a = R\n");
    writeFile(root / "spiel.sfc", "x");
    KeyProfile inDir, fromParent, fromFile, none;
    check(inDir.load((root / "MitProfil").generic_string()) && inDir.bound(KeyProfile::kA),
          "Suche: im Spielordner");
    check(!none.load((root / "OhneProfil").generic_string()), "Suche: ohne Profil (game/ leer) → keins");
    writeFile(root / "xrshell.keys", "x = LSHIFT\n");
    check(fromParent.load((root / "OhneProfil").generic_string()) && fromParent.bound(KeyProfile::kX),
          "Suche: eine Ebene höher in game/");
    check(fromFile.load((root / "spiel.sfc").generic_string()) && fromFile.bound(KeyProfile::kX),
          "Suche: neben der Spieldatei");
    KeyProfile empty;
    check(!empty.load(""), "Suche: leerer Pfad");
}

} // namespace

int main() {
    // Eigener Name pro Lauf: zwei Sitzungen dürfen die Host-Tests gleichzeitig laufen lassen.
    const fs::path tmp = fs::temp_directory_path() /
        ("xrshell-test-key-profile-" + std::to_string(std::random_device{}()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    testShippedProfiles(tmp);
    testScrollKeys(tmp);
    testReload(tmp);
    testTimingSweep(tmp);
    testSharedKey(tmp);
    testParser(tmp);
    testLookup(tmp);
    fs::remove_all(tmp);
    std::printf("test_key_profile: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
