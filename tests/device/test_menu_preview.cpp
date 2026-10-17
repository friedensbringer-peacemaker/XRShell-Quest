// Geräte-Test + Vorschau (Quest, arm64): VR-Menü ohne Headset ansehen.
// Rendert die Menütafel (CPU-Raster, ohne GL-Kontext — Uploads laufen ins Leere)
// in BMP-Dateien unter /data/local/tmp/xrshell-menu-*.bmp; run-device-tests.sh holt
// sie nach tests/out/. Prüft dabei: Tafel nicht leer, Ecken durchsichtig,
// Schnellleiste in jedem Reiter, Leiste wechselt bei laufender Aufnahme
// und zeigt „Beenden“ nach dem ersten Klick sichtbar scharf.

#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#define private public   // Zustand/redraw für die Vorschau erreichbar
#include "vr_menu.h"
#undef private

namespace {
int gChecks = 0, gFailures = 0;
std::string gOutDir = "/data/local/tmp";   // Host-Vorschau: tests/out (argv[1])
void check(bool ok, const char* what) {
    ++gChecks;
    if (!ok && ++gFailures <= 20) std::printf("FEHLER: %s\n", what);
}

// 32-Bit-BMP (BGRA, von unten nach oben) aus dem RGBA-Menüpuffer, auf Schwarz gemischt.
bool writeBmp(const std::string& path, const std::vector<uint32_t>& px, int w, int h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t dataSize = static_cast<uint32_t>(w * h * 4);
    uint8_t header[54] = { 'B', 'M' };
    auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) header[at + i] = (v >> (8 * i)) & 0xFF; };
    put32(2, 54 + dataSize); put32(10, 54); put32(14, 40); put32(18, static_cast<uint32_t>(w));
    put32(22, static_cast<uint32_t>(h)); header[26] = 1; header[28] = 32; put32(34, dataSize);
    std::fwrite(header, 1, sizeof(header), f);
    std::vector<uint8_t> row(static_cast<size_t>(w) * 4);
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t c = px[static_cast<size_t>(y) * w + x];
            const uint32_t a = c >> 24;
            row[x * 4 + 0] = static_cast<uint8_t>(((c >> 16) & 0xFF) * a / 255);
            row[x * 4 + 1] = static_cast<uint8_t>(((c >> 8) & 0xFF) * a / 255);
            row[x * 4 + 2] = static_cast<uint8_t>((c & 0xFF) * a / 255);
            row[x * 4 + 3] = 255;
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

int opaquePixels(const VrMenu& m) {
    int n = 0;
    for (uint32_t c : m.mPixels) n += (c >> 24) > 200;
    return n;
}

// Oberkante einer Zeile in Texturpixeln (Raster aus vr_menu.cpp: Kopf 84, Reiter 72, Zeile 64).
int rowTop(const VrMenu&, int row) { return 84 + 72 + row * 64; }

// Strahl senkrecht von vorn auf Texturpixel (px, py) der Tafel (lokale Koordinaten).
XrPosef aimAt(const VrMenu& m, float px, float py) {
    XrPosef pose{};
    pose.orientation.w = 1.0f;   // Blick nach −Z
    pose.position.x = (px / static_cast<float>(m.mTexW) - 0.5f) * m.mW;
    pose.position.y = (0.5f - py / static_cast<float>(m.mTexH)) * m.mH;
    pose.position.z = m.mCz + 1.0f;
    return pose;
}

// Pflichtumfang (Skill xr-vr-menu §0) als Logik-Prüfung ohne Headset.
void testLogic() {
    using A = VrMenu::Action;
    ViewSettings s;
    VrMenu menu;
    menu.setTheme(MenuTheme{});
    menu.setOptions(true, true);   // Maus-Core: Tastatur + Spiel-Mauszeiger
    menu.setLibrary({}, -1, false);
    menu.open(1.8f);
    const VrMenu::PadInput none;
    double t = 10.0;
    menu.update(nullptr, false, false, none, t, s);

    // P5: Tafelhöhe in allen Reitern gleich, Schnellleiste immer an derselben Stelle.
    const int texH = menu.mTexH;
    const size_t rowCount = menu.rows().size();
    for (VrMenu::Tab tab : { VrMenu::kTabPointer, VrMenu::kTabGame, VrMenu::kTabView }) {
        menu.mTab = tab;
        menu.layout();
        check(menu.mTexH == texH && menu.rows().size() == rowCount, "Tafelhöhe beim Reiterwechsel stabil");
    }
    const int barTop = rowTop(menu, static_cast<int>(rowCount) - 1);
    const float barY = static_cast<float>(barTop + 32);
    const int span = (menu.mTexW - 40) / 3;

    // Lücke zwischen „Video“ und „Schließen“ löst nichts aus; „Schließen“ schließt.
    XrPosef gap = aimAt(menu, static_cast<float>(20 + span), barY);
    menu.update(&gap, false, false, none, t += 0.1, s);
    check(menu.update(&gap, true, true, none, t += 0.1, s) == A::None, "Lücke der Schnellleiste nicht klickbar");
    XrPosef closeBtn = aimAt(menu, static_cast<float>(20 + span + span / 2), barY);
    menu.update(&closeBtn, false, false, none, t += 0.1, s);
    check(menu.update(&closeBtn, true, true, none, t += 0.1, s) == A::Close, "Schließen in der Schnellleiste");

    // P3 Beenden: erster Klick scharf (sichtbar), zweiter in 3 s beendet, sonst zurückfallen.
    XrPosef quitBtn = aimAt(menu, static_cast<float>(20 + 2 * span + span / 2), barY);
    menu.update(&quitBtn, false, false, none, t += 0.1, s);
    check(menu.update(&quitBtn, true, true, none, t += 0.1, s) == A::QuitArmed, "Beenden: erster Klick schaltet scharf");
    menu.update(&quitBtn, false, false, none, t += 0.1, s);
    check(menu.mQuitShown, "Beenden-Knopf sichtbar scharf");
    check(menu.update(&quitBtn, true, true, none, t += 1.0, s) == A::Quit, "Beenden: zweiter Klick in 3 s");
    menu.update(&quitBtn, true, true, none, t += 0.1, s);   // wieder scharf
    menu.update(&quitBtn, false, false, none, t += 3.5, s);
    check(!menu.mQuitShown, "Beenden fällt nach 3 s zurück");
    menu.open(1.8f);
    check(menu.mQuitArmedUntil == 0.0, "Öffnen setzt Beenden zurück");

    // P1: B schließt.
    VrMenu::PadInput b;
    b.bEdge = true;
    check(menu.update(nullptr, false, false, b, t += 0.1, s) == A::Close, "B schließt das Menü");

    // P2: Stick + A ohne Strahl: Fokus auf die erste Zeile, A/rechts ändert den Wert.
    menu.open(1.8f);
    menu.mTab = VrMenu::kTabView;
    menu.layout();
    s.passthrough = 0;
    VrMenu::PadInput down;
    down.stickY = -1.0f;
    menu.update(nullptr, false, false, down, t += 0.1, s);
    check(menu.mHoverRow == 0 && menu.mPadFocus, "Stick setzt den Fokus auf die erste Zeile");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    VrMenu::PadInput right;
    right.stickX = 1.0f;
    check(menu.update(nullptr, false, false, right, t += 0.1, s) == A::Changed && s.passthrough == 10,
          "Stick rechts ändert den Wert");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    VrMenu::PadInput a;
    a.aEdge = true;
    menu.update(nullptr, false, false, a, t += 0.1, s);
    check(s.passthrough == 20, "A löst die fokussierte Zeile aus");
    VrMenu::PadInput up;
    up.stickY = 1.0f;
    menu.update(nullptr, false, false, up, t += 0.1, s);
    check(menu.mHoverTab >= 0 && menu.mHoverRow < 0, "Stick hoch über die erste Zeile → Reiterleiste");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, right, t += 0.1, s);
    check(menu.mTab == VrMenu::kTabPointer, "Stick rechts auf der Reiterleiste wechselt den Reiter");

    // P2: Dauerdruck auf ›: nach 0,45 s alle 0,12 s eine Stufe.
    menu.mTab = VrMenu::kTabView;
    menu.layout();
    menu.mPadFocus = false;
    s.saturation = 100;
    const int satRow = 3;   // Passthrough, Wölbung, Filter, Sättigung
    XrPosef more = aimAt(menu, 924.0f, static_cast<float>(rowTop(menu, satRow) + 32));
    menu.update(&more, false, false, none, t += 0.1, s);
    menu.update(&more, true, true, none, t += 0.01, s);
    check(s.saturation == 110, "Klick auf › eine Stufe");
    for (int i = 0; i < 50; ++i) menu.update(&more, false, true, none, t += 0.02, s);   // 1 s gehalten (50 Hz)
    check(s.saturation >= 150 && s.saturation <= 170, "Dauerdruck wiederholt");
    menu.update(&more, false, false, none, t += 0.1, s);
    const int afterRelease = s.saturation;
    menu.update(&more, false, false, none, t += 1.0, s);
    check(s.saturation == afterRelease, "Loslassen beendet die Wiederholung");

    // P3: Spiel-Mauszeiger im Reiter Zeiger (Maus-Core), P4: gespeichert und geklemmt.
    bool hasCursorRow = false;
    for (const auto& item : menu.rows(VrMenu::kTabPointer)) hasCursorRow |= item.row == VrMenu::kGameCursor;
    check(hasCursorRow, "Spiel-Mauszeiger im Reiter Zeiger");
    ViewSettings out;
    out.gameCursor = 1;
    out.rayStyle = ViewSettings::kRayStyleOff;
    out.marker = ViewSettings::kMarkerOff;
    const std::string cfgPath = gOutDir + "/xrshell-test-view.cfg";
    const char* cfg = cfgPath.c_str();
    check(out.save(cfg), "Einstellungen speichern");
    ViewSettings in;
    check(in.load(cfg) && in.gameCursor == 1 && !in.gameCursorVisible(), "Spiel-Mauszeiger Aus gespeichert");
    std::remove(cfg);
    check(in.rayThickness(false) == 0.0f && in.rayThickness(true) > 0.0f, "Strahl Aus, im Menü trotzdem sichtbar");
    check(in.markerSize(false) == 0.0f && in.markerSize(true) > 0.0f, "Zielpunkt Aus, im Menü trotzdem sichtbar");
}
// Teil-Redraw gegen Vollbild: gleich? Sonst Rechteck der Abweichungen ausgeben.
bool samePixels(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int w) {
    if (a.size() != b.size()) return false;
    int x0 = w, y0 = 1 << 30, x1 = -1, y1 = -1, n = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == b[i]) continue;
        const int x = static_cast<int>(i % w), y = static_cast<int>(i / w);
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        if (++n == 1) std::printf("  erste Abweichung (%d,%d): %08X statt %08X\n", x, y, a[i], b[i]);
    }
    if (n) std::printf("  %d Pixel abweichend in x %d..%d, y %d..%d\n", n, x0, x1, y0, y1);
    return n == 0;
}

// Bibliothek: Beispiel-Einträge „System<TAB>Titel“ — nur Namen, keine Spieldaten.
std::vector<std::string> sampleLibrary() {
    return { "DOS\tAlpha Rally", "DOS\tBlock Puzzle Deluxe", "Doom\tCastle Quest", "Quake\tDungeon Diver",
             "DOS\tEnchanted Forest", "Wolf3D\tFast Fox Runner", "Doom\tGalaxy Defender", "Quake\tHarvest Island",
             "DOS\tIce Kingdom", "Doom\tJungle Jump", "DOS\tKart Grand Prix", "Quake\tLegend of the Lighthouse",
             "DOS\tMega Mech Arena", "Doom\tNinja Shadow", "DOS\tÜber Ocean Odyssey", "Quake\tPixel Pinball",
             "DOS\tSpace Pilot's Duel", "Adventure\tTurbo Thunder", "Doom\tUltra Volley", "DOS\tWild West Wanderer" };
}

int keyIndex(const VrMenu& m, const char* label) {
    for (size_t k = 0; k < m.mKeys.size(); ++k)
        if (std::string(m.mKeys[k].label) == label) return static_cast<int>(k);
    return -1;
}

XrPosef aimAtKey(const VrMenu& m, int k) {
    const auto& key = m.mKeys[k];
    return aimAt(m, (key.x0 + key.x1) * 0.5f, (key.y0 + key.y1) * 0.5f);
}

// Strahl auf ein Ziel, eine Runde halten, dann Trigger-Flanke.
VrMenu::Action click(VrMenu& m, const XrPosef& aim, double& t, ViewSettings& s) {
    const VrMenu::PadInput none;
    m.update(&aim, false, false, none, t += 0.05, s);
    const VrMenu::Action a = m.update(&aim, true, true, none, t += 0.05, s);
    m.update(&aim, false, false, none, t += 0.05, s);
    return a;
}

void testLibrary() {
    using A = VrMenu::Action;
    // Suche: Wörter, Groß/klein, Umlaute, Apostroph, System, zusammengeschrieben.
    check(VrMenu::matchesSearch("DOS\tSuper Space World", "space"), "Suche: Teilwort");
    check(VrMenu::matchesSearch("DOS\tSuper Space World", "WORLD super"), "Suche: mehrere Wörter, Reihenfolge egal");
    check(!VrMenu::matchesSearch("DOS\tSuper Space World", "space kart"), "Suche: alle Wörter nötig");
    check(VrMenu::matchesSearch("DOS\tÜber Ocean Odyssey", "uber"), "Suche: Ü = u");
    check(VrMenu::matchesSearch("DOS\tÜber Ocean Odyssey", "über"), "Suche: ü getippt");
    check(VrMenu::matchesSearch("DOS\tSpace Pilot's Duel", "pilots"), "Suche: Apostroph egal");
    check(VrMenu::matchesSearch("DOS\tSpace Pilot's Duel", "spacepilot"), "Suche: zusammengeschrieben");
    check(VrMenu::matchesSearch("Quake\tPixel Pinball", "quake"), "Suche: System");
    check(VrMenu::matchesSearch("Doom\tGroße Straße", "strasse"), "Suche: ß = ss");
    check(VrMenu::matchesSearch("Doom\tCastle Quest", ""), "Suche: leer passt immer");

    ViewSettings s;
    VrMenu menu;
    menu.setTheme(MenuTheme{});
    menu.setOptions(false, true);   // Pad-Core
    menu.setLibrary(sampleLibrary(), 2, true);
    menu.setTitle("xr.shooter", "Shooter");
    menu.open(1.8f);
    const VrMenu::PadInput none;
    double t = 50.0;
    menu.update(nullptr, false, false, none, t, s);

    // Zweispaltig: breitere Tafel, linke Spalte so hoch wie die rechte.
    check(menu.mTexW == 1536, "Bibliothek: Tafel 1536 px breit");
    check(menu.libRows().size() == menu.rows().size(), "Bibliothek: Spalten gleich hoch");
    check(menu.gamesPerPage() == 7 && menu.pageCount() == 3, "Bibliothek: 7 Spiele pro Seite, 3 Seiten");
    check(menu.libRows()[0].row == VrMenu::kSearch, "Suchfeld oben links");

    // Strahl auf das zweite Spiel → Start mit dem Index in der Gesamtliste.
    const float gameX = 280.0f;
    check(click(menu, aimAt(menu, gameX, VrMenu::rowTop(1) + 32.0f), t, s) == A::StartGame &&
          menu.selectedGame() == 1, "Klick auf Spiel startet es");

    // Seite weiter per ›.
    const int pageRow = static_cast<int>(menu.libRows().size()) - 2;
    click(menu, aimAt(menu, VrMenu::kLibW - 12 - 44, VrMenu::libTop(pageRow) + 32.0f), t, s);
    check(menu.mPage == 1, "› blättert weiter");

    // Suchfeld → Tastatur; „M“ „A“ tippen filtert live; ← löscht; Fertig schließt.
    click(menu, aimAt(menu, 200.0f, VrMenu::kTitleH + 36.0f), t, s);
    check(menu.mSearchActive && menu.mKeys.size() == 47, "Klick ins Suchfeld öffnet die Tastatur");
    {   // Tastatur an: nur rechte Spalte + Suchfeld neu — muss dem Vollbild gleichen
        check(!menu.mDirty, "Tastatur an: kein Vollbild");
        const std::vector<uint32_t> part = menu.mPixels;
        menu.redraw(s, t);
        check(samePixels(part, menu.mPixels, menu.mTexW), "Tastatur an: Teil-Redraw gleicht dem Vollbild");
    }
    click(menu, aimAtKey(menu, keyIndex(menu, "M")), t, s);
    click(menu, aimAtKey(menu, keyIndex(menu, "E")), t, s);
    click(menu, aimAtKey(menu, keyIndex(menu, "C")), t, s);
    check(menu.mQuery == "mec" && menu.mFiltered.size() == 1 && menu.mPage == 0, "Tippen filtert die Liste");
    {   // Teil-Redraw nach dem Tippen = Vollbild (keine Streifen)
        const std::vector<uint32_t> part = menu.mPixels;
        menu.redraw(s, t);
        check(samePixels(part, menu.mPixels, menu.mTexW), "Teil-Redraw der Liste gleicht dem Vollbild");
    }
    click(menu, aimAtKey(menu, keyIndex(menu, "\xE2\x86\x90")), t, s);
    check(menu.mQuery == "me", "Rücktaste löscht ein Zeichen");
    // Treffer per Strahl starten, während die Tastatur offen ist.
    check(click(menu, aimAt(menu, gameX, VrMenu::rowTop(0) + 32.0f), t, s) == A::StartGame &&
          !menu.mSearchActive, "Treffer starten schließt die Tastatur");
    click(menu, aimAt(menu, 200.0f, VrMenu::kTitleH + 36.0f), t, s);
    click(menu, aimAtKey(menu, keyIndex(menu, "Fertig")), t, s);
    check(!menu.mSearchActive && menu.mQuery == "me", "Fertig schließt die Tastatur, Suchtext bleibt");
    click(menu, aimAt(menu, VrMenu::kLibW - 12 - 34, VrMenu::kTitleH + 36.0f), t, s);
    check(menu.mQuery.empty() && menu.mFiltered.size() == 20, "× leert die Suche");

    // B: erst Tastatur zu, dann Menü zu.
    click(menu, aimAt(menu, 200.0f, VrMenu::kTitleH + 36.0f), t, s);
    VrMenu::PadInput b;
    b.bEdge = true;
    check(menu.update(nullptr, false, false, b, t += 0.1, s) == A::None && !menu.mSearchActive,
          "B schließt im Suchmodus nur die Tastatur");
    {   // Tastatur aus: Reiter wieder da, ohne Vollbild
        const std::vector<uint32_t> part = menu.mPixels;
        menu.redraw(s, t);
        check(samePixels(part, menu.mPixels, menu.mTexW), "Tastatur aus: Teil-Redraw gleicht dem Vollbild");
    }
    {   // Reiterwechsel per Strahl: nur die rechte Spalte neu
        const int x0 = VrMenu::kLibW, n = static_cast<int>(menu.tabs().size());
        const float tabX = static_cast<float>(x0 + 30 + (menu.mTexW - x0 - 60) * 3 / (2 * n));   // Mitte Reiter 2
        click(menu, aimAt(menu, tabX, VrMenu::kTitleH + 36.0f), t, s);
        check(menu.mTab == VrMenu::kTabPointer && !menu.mDirty, "Reiterwechsel ohne Vollbild");
        const std::vector<uint32_t> part = menu.mPixels;
        menu.redraw(s, t);
        check(samePixels(part, menu.mPixels, menu.mTexW), "Reiterwechsel: Teil-Redraw gleicht dem Vollbild");
        menu.mTab = VrMenu::kTabView;
    }
    check(menu.update(nullptr, false, false, b, t += 0.1, s) == A::Close, "B schließt danach das Menü");

    // Stick: Fokus startet auf dem ersten Spiel, rechts → Reiter, links → zurück in die Liste.
    menu.open(1.8f);
    menu.showGamesTab();
    VrMenu::PadInput down, right, left, a;
    down.stickY = -1.0f;
    right.stickX = 1.0f;
    left.stickX = -1.0f;
    a.aEdge = true;
    menu.update(nullptr, false, false, down, t += 0.1, s);
    check(menu.mHoverLib == 1 && menu.mPadFocus, "Stick: Fokus aufs erste Spiel");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, right, t += 0.1, s);
    check(menu.mHoverTab >= 0 && menu.mHoverLib < 0, "Stick rechts: in die Reiterleiste");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, left, t += 0.1, s);
    check(menu.mHoverLib == 1, "Stick links von der Reiterleiste: zurück in die Liste");
    {   // Teil-Redraw nach Fokuswechsel = Vollbild
        const std::vector<uint32_t> part = menu.mPixels;
        menu.redraw(s, t);
        check(samePixels(part, menu.mPixels, menu.mTexW), "Teil-Redraw beim Fokuswechsel gleicht dem Vollbild");
    }
    VrMenu::PadInput up;
    up.stickY = 1.0f;
    // Stick blättert am Seitenende: unter dem 7. Spiel → Seite 2, erstes Spiel; zurück → Seite 1, 7. Spiel.
    for (int i = 0; i < 6; ++i) {
        menu.update(nullptr, false, false, down, t += 0.1, s);
        menu.update(nullptr, false, false, none, t += 0.1, s);
    }
    check(menu.mHoverLib == 7 && menu.mPage == 0, "Stick: letztes Spiel der Seite");
    menu.update(nullptr, false, false, down, t += 0.1, s);
    check(menu.mPage == 1 && menu.mHoverLib == 1, "Stick runter am Seitenende: nächste Seite");
    {   // Blättern zeichnet nur die Liste neu
        menu.update(nullptr, false, false, none, t += 0.1, s);
        const std::vector<uint32_t> part = menu.mPixels;
        menu.redraw(s, t);
        check(samePixels(part, menu.mPixels, menu.mTexW), "Blättern per Stick: Teil-Redraw gleicht dem Vollbild");
    }
    menu.update(nullptr, false, false, up, t += 0.1, s);
    check(menu.mPage == 0 && menu.mHoverLib == 7, "Stick hoch am Seitenanfang: vorige Seite, letztes Spiel");
    for (int i = 0; i < 6; ++i) {
        menu.update(nullptr, false, false, none, t += 0.1, s);
        menu.update(nullptr, false, false, up, t += 0.1, s);
    }
    check(menu.mHoverLib == 1, "Stick: zurück beim ersten Spiel");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    // Stick hoch aufs Suchfeld, A → Tastatur, Stick rechts in die Tasten, A tippt.
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, up, t += 0.1, s);
    check(menu.mHoverLib == 0, "Stick hoch: Suchfeld");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, a, t += 0.1, s);
    check(menu.mSearchActive, "A im Suchfeld öffnet die Tastatur");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, right, t += 0.1, s);
    check(menu.mHoverKey == 0, "Stick rechts: erste Taste");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, a, t += 0.1, s);
    check(menu.mQuery == "1", "A tippt die fokussierte Taste");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, b, t += 0.1, s);
    check(!menu.mSearchActive && menu.mHoverLib == 0, "B: Fokus zurück aufs Suchfeld");

    // Wieder öffnen: Bild vom letzten Mal weiterverwenden (kein Vollbild), Fokus/Tastatur zurückgesetzt.
    menu.mQuery.clear();   // „1“ aus dem Tipp-Test hätte die Liste leer gefiltert
    menu.refilter();
    menu.mLibDirty = true;   // wie pressKey/× beim Leeren
    menu.open(1.8f);
    menu.update(nullptr, false, false, none, t += 0.1, s);
    click(menu, aimAt(menu, 200.0f, VrMenu::kTitleH + 36.0f), t, s);   // Tastatur an
    const XrPosef onGame = aimAt(menu, 280.0f, VrMenu::rowTop(2) + 32.0f);
    menu.update(&onGame, false, false, none, t += 0.1, s);            // Fokus auf einem Spiel
    check(menu.mSearchActive && menu.mHoverLib == 3, "Vor dem Schließen: Tastatur an, Spiel im Fokus");
    menu.close();
    menu.open(1.8f);
    check(!menu.mDirty && !menu.mSearchActive && !menu.hovered(), "Wieder öffnen: kein Vollbild, Fokus/Tastatur zurück");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    {
        const std::vector<uint32_t> part = menu.mPixels;
        menu.redraw(s, t);
        check(samePixels(part, menu.mPixels, menu.mTexW), "Wieder öffnen: Bild gleicht dem Vollbild");
    }
    menu.close();
    menu.setStatus("Geändert, während zu", t);   // Änderung bei geschlossenem Menü
    menu.open(1.8f);
    check(menu.mDirty, "Änderung bei geschlossenem Menü: beim Öffnen neu zeichnen");

    // Leere Bibliothek: Hinweis statt Liste.
    VrMenu empty;
    empty.setTheme(MenuTheme{});
    empty.setLibrary({}, -1, true);
    empty.open(1.8f);
    const auto lib = empty.libRows();
    check(lib.size() == empty.rows().size() && lib[1].row == VrMenu::kLibraryEmpty && lib.back().row == VrMenu::kSpacer,
          "Leere Bibliothek: Hinweis, Spalte gleich hoch");
}

// Reiter „Tasten“: Antippen + Quest-Taste drücken, Abbruch nach 5 s, Sticks, Knöpfe.
void testPadTab() {
    using A = VrMenu::Action;
    constexpr int kStart = 3, kAid = 8;
    ViewSettings s;
    VrMenu menu;
    menu.setTheme(MenuTheme{});
    menu.setOptions(false, true);
    menu.setLibrary(sampleLibrary(), 0, true);
    menu.setPad(true, "pad", padmap::systemFor("pad").defaults);   // allgemeines RetroPad (Rückfall)
    menu.open(1.8f);
    menu.mTab = VrMenu::kTabPad;
    menu.layout();
    const VrMenu::PadInput none;
    double t = 200.0;
    menu.update(nullptr, false, false, none, t, s);
    check(menu.tabs().size() == 4, "Tasten: vierter Reiter bei Pad-Cores");
    const auto list = menu.rows();
    check(list[0].row == VrMenu::kPadHint && list[1].row == VrMenu::kPadPair && list[4].row == VrMenu::kPadPair &&
          list[5].row == VrMenu::kPadStickL && list[7].row == VrMenu::kPadActions, "Tasten: Zeilen für das RetroPad");
    check(menu.rows().size() == menu.libRows().size(), "Tasten: Tafelhöhe unverändert");

    // „Start“ (viertes Paar, links) antippen → wartet; A drücken → A erzeugt Start.
    const int x0 = VrMenu::kLibW + 20, span = (menu.mTexW - 20 - x0) / 2;
    const XrPosef startChip = aimAt(menu, static_cast<float>(x0 + span / 2), VrMenu::rowTop(4) + 32.0f);
    menu.update(&startChip, false, false, none, t += 0.05, s);
    VrMenu::PadInput trig;
    trig.sources[padmap::kRightTrigger] = true;   // derselbe Trigger klickt — darf nicht zuweisen
    menu.update(&startChip, true, true, trig, t += 0.05, s);
    check(menu.padLearning() && menu.mLearnTarget == kStart, "Tasten: Antippen wartet auf Quest-Taste");
    check(menu.update(&startChip, false, true, trig, t += 0.05, s) == A::None && menu.padLearning(),
          "Tasten: gehaltener Trigger weist nicht zu");
    {   // Teil-Redraw im Zuweisen-Zustand = Vollbild
        const std::vector<uint32_t> part = menu.mPixels;
        menu.redraw(s, t);
        check(samePixels(part, menu.mPixels, menu.mTexW), "Tasten: Teil-Redraw gleicht dem Vollbild");
    }
    VrMenu::PadInput aDown;
    aDown.sources[padmap::kA] = true;
    aDown.aEdge = true;   // A ist auch Menü-Taste — beim Zuweisen nur Quelle
    check(menu.update(&startChip, false, false, aDown, t += 0.05, s) == A::PadChanged, "Tasten: A zugewiesen");
    check(menu.padMapping().button[padmap::kA] == kStart && !menu.padLearning(), "Tasten: A erzeugt jetzt Start");
    check(menu.padMapping().button[padmap::kY] == kAid, "Tasten: Y (bisher Start) tauscht auf A");
    menu.update(&startChip, false, false, none, t += 0.05, s);

    // B beim Zuweisen ist Quelle, schließt nicht; Abbruch nach 5 s.
    menu.update(&startChip, true, true, none, t += 0.05, s);
    VrMenu::PadInput bDown;
    bDown.sources[padmap::kB] = true;
    bDown.bEdge = true;
    check(menu.update(&startChip, false, false, bDown, t += 0.05, s) == A::PadChanged &&
          menu.padMapping().button[padmap::kB] == kStart, "Tasten: B beim Zuweisen = Quelle, nicht Schließen");
    menu.update(&startChip, false, false, none, t += 0.05, s);
    menu.update(&startChip, true, true, none, t += 0.05, s);
    menu.update(&startChip, false, false, none, t += 5.5, s);
    check(!menu.padLearning(), "Tasten: Zuweisen bricht nach 5 s ab");

    // Stick-Zeile: RetroPad ohne Analog kennt nur Steuerkreuz / Aus.
    const XrPosef stickMore = aimAt(menu, static_cast<float>(menu.mTexW - 100), VrMenu::rowTop(5) + 32.0f);
    check(click(menu, stickMore, t, s) == A::PadChanged &&
          menu.padMapping().stick[padmap::kLeftStick] == padmap::kStickOff, "Tasten: Stick › → Aus");
    click(menu, stickMore, t, s);
    check(menu.padMapping().stick[padmap::kLeftStick] == padmap::kStickDpad, "Tasten: RetroPad ohne Analog");

    // Knöpfe: für alle / zurücksetzen.
    check(click(menu, aimAt(menu, static_cast<float>(x0 + span / 2), VrMenu::rowTop(7) + 32.0f), t, s) ==
          A::PadSaveSystem, "Tasten: Für alle Spiele des Systems");
    check(click(menu, aimAt(menu, static_cast<float>(x0 + span + span / 2), VrMenu::rowTop(7) + 32.0f), t, s) ==
          A::PadReset, "Tasten: Zurücksetzen");

    // Stick-Bedienung: von der Reiterleiste nach unten aufs erste Paar, rechts = zweite Taste.
    menu.mPadFocus = false;
    menu.setHover(VrMenu::Focus());
    menu.mRay = VrMenu::Focus();
    VrMenu::PadInput down, right, up;
    down.stickY = -1.0f;
    right.stickX = 1.0f;
    up.stickY = 1.0f;
    menu.update(nullptr, false, false, up, t += 0.5, s);   // Fokus setzen (erstes Spiel links)
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, right, t += 0.1, s);   // → Reiterleiste
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, down, t += 0.1, s);
    check(menu.mHoverRow == 1 && menu.mHoverZone == VrMenu::kZoneBar0, "Tasten: Stick runter aufs erste Paar");
    menu.update(nullptr, false, false, none, t += 0.1, s);
    menu.update(nullptr, false, false, right, t += 0.1, s);
    check(menu.mHoverRow == 1 && menu.mHoverZone == VrMenu::kZoneBar1, "Tasten: Stick rechts = zweite Taste");

    // DOS-Pad: elf Tasten, keine Kurzanleitung (Platz), Analog wählbar.
    VrMenu dospad;
    dospad.setTheme(MenuTheme{});
    dospad.setLibrary(sampleLibrary(), 0, true);
    dospad.setPad(true, "dospad", padmap::systemFor("dospad").defaults);
    dospad.open(1.8f);
    const auto pl = dospad.rows(VrMenu::kTabPad);
    check(pl[0].row == VrMenu::kPadPair && pl.size() == dospad.rows(VrMenu::kTabView).size(),
          "Tasten DOS-Pad: passt ohne höhere Tafel");
    // Einspaltige Variante (ohne Bibliothek) bekommt den Reiter ebenfalls.
    VrMenu single;
    single.setTheme(MenuTheme{});
    single.setPad(true, "pad", padmap::systemFor("pad").defaults);
    single.open(1.8f);
    check(single.tabs().size() == 4 && single.rows(VrMenu::kTabPad)[0].row == VrMenu::kPadHint,
          "Tasten RetroPad einspaltig");
}

// Vorschauen der zweispaltigen Tafel (Bibliothek).
void previewLibrary() {
    ViewSettings settings;
    settings.width = 2.4f;
    settings.distance = 1.8f;
    settings.passthrough = 40;
    struct Shot { const char* name; bool search; const char* query; bool emptyLib; };
    const Shot shots[] = { { "liste", false, "", false }, { "suche", true, "an", false },
                           { "leer", false, "", true }, { "keine-treffer", false, "zzz", false } };
    for (const Shot& shot : shots) {
        VrMenu menu;
        menu.mFont.load();
        menu.mFontBold.load(true);
        menu.setTheme(MenuTheme{});
        menu.setOptions(false, true);
        menu.setLibrary(shot.emptyLib ? std::vector<std::string>{} : sampleLibrary(), 2, true);
        menu.setTitle("xr.shooter", "Shooter");
        menu.open(1.8f);
        menu.mQuery = shot.query;
        menu.refilter();
        menu.mSearchActive = shot.search;
        if (shot.search) menu.mHoverKey = keyIndex(menu, "A");
        else menu.mHoverLib = 3;   // drittes Spiel unter dem Strahl
        menu.redraw(settings, 0.0);
        check(opaquePixels(menu) > menu.mTexW * menu.mTexH / 2, "Bibliothek: Tafel überwiegend deckend");
        check((menu.mPixels[0] >> 24) < 40, "Bibliothek: Ecke durchsichtig");
        char path[512];
        std::snprintf(path, sizeof(path), "%s/xrshell-menu-bibliothek-%s.bmp", gOutDir.c_str(), shot.name);
        check(writeBmp(path, menu.mPixels, menu.mTexW, menu.mTexH), "Vorschau schreiben");
    }
    // Reiter „Tasten“: RetroPad (Start wartet aufs Zuweisen) und DOS-Pad.
    for (const char* folder : { "pad", "dospad" }) {
        VrMenu menu;
        menu.mFont.load();
        menu.mFontBold.load(true);
        menu.setTheme(MenuTheme{});
        menu.setOptions(false, true);
        menu.setLibrary(sampleLibrary(), 2, true);
        menu.setTitle("xr.shooter", "Shooter");
        menu.setPad(true, folder, padmap::systemFor(folder).defaults);
        menu.open(1.8f);
        menu.mTab = VrMenu::kTabPad;
        menu.layout();
        const bool pad = std::string(folder) == "pad";
        if (pad) menu.mLearnTarget = 3;   // „Start“ wartet
        menu.mHoverRow = pad ? 1 : 2;
        menu.mHoverZone = VrMenu::kZoneBar1;
        menu.redraw(settings, 0.0);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/xrshell-menu-bibliothek-tasten-%s.bmp", gOutDir.c_str(), folder);
        check(writeBmp(path, menu.mPixels, menu.mTexW, menu.mTexH), "Vorschau schreiben");
    }
}
// Bibliotheks-Apps starten im Menü: Reiter „Spiel“ mit „Weiterspielen: <Titel>“ (nur ohne laufendes
// Spiel) und „Beim Start: Menü/Letztes Spiel“; „zuletzt“-Abzeichen; start_mode in der cfg.
void testResume() {
    using A = VrMenu::Action;
    ViewSettings s;
    s.width = 2.4f;
    s.distance = 1.8f;
    check(s.startMode == 0, "Standard: Start im Menü");
    VrMenu menu;
    menu.mFont.load();
    menu.mFontBold.load(true);
    menu.setTheme(MenuTheme{});
    menu.setOptions(true, true);
    menu.setTitle("xr.adventure", "Adventures");
    menu.setLibrary(sampleLibrary(), -1, true, 12);   // nichts läuft, zuletzt Spiel 12
    menu.open(1.8f);
    menu.showGamesTab();
    check(menu.mTab == VrMenu::kTabGame, "Start ohne Spiel mit letztem: Reiter Spiel");
    const auto rows = menu.contentRows(VrMenu::kTabGame);
    check(rows.size() >= 2 && rows[0].row == VrMenu::kResume && rows[1].row == VrMenu::kStartMode,
          "Weiterspielen oben, darunter Beim Start");
    const int gpp = menu.gamesPerPage();
    check(menu.mPage == 12 / gpp, "Liste auf der Seite des letzten Spiels");
    double t = 20.0;
    const float midX = static_cast<float>(VrMenu::kLibW + menu.mTexW) / 2.0f;
    const A a = click(menu, aimAt(menu, midX, VrMenu::rowTop(0) + 32.0f), t, s);
    check(a == A::StartGame && menu.selectedGame() == 12, "Weiterspielen startet das letzte Spiel");
    {   // Vorschau mit Strahl auf dem Knopf
        menu.mHoverRow = 0;
        menu.redraw(s, t);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/xrshell-menu-weiterspielen.bmp", gOutDir.c_str());
        check(writeBmp(path, menu.mPixels, menu.mTexW, menu.mTexH), "Vorschau schreiben");
    }
    // Beim Start umschalten (Pfeil rechts) → eigene Aktion, Wert gewechselt.
    const float moreX = static_cast<float>(menu.mTexW - 100);
    const A b = click(menu, aimAt(menu, moreX, VrMenu::rowTop(1) + 32.0f), t, s);
    check(b == A::StartModeChanged && s.startMode == ViewSettings::kStartResume, "Beim Start → Letztes Spiel");
    // Spiel läuft: kein Weiterspielen-Knopf, Beim Start bleibt.
    menu.setLibrary(sampleLibrary(), 12, true, 12);
    const auto running = menu.contentRows(VrMenu::kTabGame);
    check(running[0].row == VrMenu::kStartMode, "Spiel läuft: kein Weiterspielen");
    // Ohne letztes Spiel: kein Knopf; Tafelhöhe bleibt in allen Reitern gleich.
    menu.setLibrary(sampleLibrary(), -1, true, -1);
    check(menu.contentRows(VrMenu::kTabGame)[0].row == VrMenu::kStartMode, "ohne letztes Spiel: kein Knopf");
    // Ungültiger Index (Spiel gelöscht) wird ignoriert.
    menu.setLibrary(sampleLibrary(), -1, true, 999);
    check(menu.mLastGame == -1, "letztes Spiel außerhalb der Liste ignoriert");
    // cfg-Rundlauf
    const std::string cfg = gOutDir + "/xrshell-test-start.cfg";
    check(s.save(cfg), "cfg speichern");
    ViewSettings back;
    check(back.load(cfg) && back.startMode == ViewSettings::kStartResume, "start_mode gespeichert und geladen");
    std::remove(cfg.c_str());
}

// xr.adventure wie auf der Quest (Audit gegen Skill xr-vr-menu): drei Adventures, eins läuft,
// Maus-Core ohne Savestates, Spielmenü-Knopf; ein Bild je Reiter.
void previewAdventure() {
    ViewSettings settings;
    settings.width = 2.4f;
    settings.distance = 1.8f;
    VrMenu menu;
    menu.mFont.load();
    menu.mFontBold.load(true);
    menu.setTheme(MenuTheme{});
    menu.setOptions(true, true);
    menu.setLibraryEmptyHint("Keine Spiele – Ordner mit *.scummvm nach files/games/adventure/");
    menu.setLibrary({ "Adventure\tBaphomets Fluch", "Adventure\tBaphomets Fluch 2", "Adventure\tBeneath a Steel Sky" },
                    0, true, 0);
    menu.setTitle("xr.adventure", "Adventures · 0.3.3");
    menu.setStatesAvailable(false);
    menu.setGameMenuAvailable(true);
    menu.setEnvironmentAvailable(false);
    menu.setControlsHelp({ "Trigger Klick \xC2\xB7 B Ansehen \xC2\xB7 A Spielmenü",
                           "Griff Fenster \xC2\xB7 X Zoom \xC2\xB7 Y Tastatur \xC2\xB7 Stick Szene überspringen" });
    menu.open(1.8f);
    check(menu.mSingleSystem, "Ein-System-Bibliothek erkannt (keine Plakette)");
    bool env = false, help = false;
    for (const auto& it : menu.contentRows(VrMenu::kTabView)) env |= it.row == VrMenu::kEnvironment;
    for (const auto& it : menu.contentRows(VrMenu::kTabGame)) help |= it.row == VrMenu::kHelp;
    check(!env && help, "Adventure: keine Umgebung-Zeile, Bedienungshilfe im Reiter Spiel");
    const VrMenu::Tab tabs[] = { VrMenu::kTabView, VrMenu::kTabPointer, VrMenu::kTabGame };
    const char* names[] = { "ansicht", "zeiger", "spiel" };
    for (int t = 0; t < 3; ++t) {
        menu.mTab = tabs[t];
        menu.layout();
        menu.mHoverRow = -1;
        menu.redraw(settings, 0.0);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/xrshell-menu-adventure-audit-%s.bmp", gOutDir.c_str(), names[t]);
        check(writeBmp(path, menu.mPixels, menu.mTexW, menu.mTexH), "Vorschau schreiben");
    }
}

// xr.adventure: ScummVM hat keine Savestates → Reiter „Spiel“ zeigt einen Hinweis statt
// Speichern/Laden (vorher: Klick meldete nur „Speichern fehlgeschlagen“).
void testNoStates() {
    ViewSettings settings;
    settings.width = 2.4f;
    settings.distance = 1.8f;
    VrMenu menu;
    menu.mFont.load();
    menu.mFontBold.load(true);
    menu.setTheme(MenuTheme{});
    menu.setOptions(true, true);
    menu.setLibrary(sampleLibrary(), 1, true);
    menu.setTitle("xr.adventure", "Adventures");
    menu.open(1.8f);
    const int texH = menu.mTexH;
    auto has = [&](VrMenu::Row r) {
        for (const auto& it : menu.contentRows(VrMenu::kTabGame)) if (it.row == r) return true;
        return false;
    };
    check(has(VrMenu::kSaveState) && has(VrMenu::kLoadState) && !has(VrMenu::kNoStates),
          "Spielstände möglich: Speichern/Laden da");
    menu.setStatesAvailable(false);
    check(!has(VrMenu::kSaveState) && !has(VrMenu::kLoadState) && !has(VrMenu::kStateSlot) &&
              has(VrMenu::kNoStates) && has(VrMenu::kSaveProfile),
          "ohne Spielstände: Hinweis statt Speichern/Laden, Profile bleiben");
    check(menu.mTexH == texH, "ohne Spielstände: Tafel bleibt gleich hoch");
    check(menu.rows(VrMenu::kTabGame).back().row == VrMenu::kQuickBar, "Schnellleiste bleibt unten");
    menu.mTab = VrMenu::kTabGame;
    menu.layout();
    menu.mHoverRow = 0;   // Strahl auf dem Hinweis: nicht fokussierbar, kein Klick
    menu.redraw(settings, 0.0);
    char path[512];
    std::snprintf(path, sizeof(path), "%s/xrshell-menu-adventure-spiel.bmp", gOutDir.c_str());
    check(writeBmp(path, menu.mPixels, menu.mTexW, menu.mTexH), "Vorschau schreiben");
    // ScummVM: statt Hinweis ein Knopf, der das Spielmenü (Strg+F5) öffnet.
    menu.setGameMenuAvailable(true);
    check(has(VrMenu::kGameMenu) && !has(VrMenu::kNoStates), "Spielmenü belegt: Knopf statt Hinweis");
    check(menu.mTexH == texH, "Spielmenü-Knopf: Tafel bleibt gleich hoch");
    {
        int row = -1;
        const auto list = menu.contentRows(VrMenu::kTabGame);
        for (size_t i = 0; i < list.size(); ++i) if (list[i].row == VrMenu::kGameMenu) row = static_cast<int>(i);
        double t = 30.0;
        const float midX = static_cast<float>(VrMenu::kLibW + menu.mTexW) / 2.0f;
        const VrMenu::Action a = click(menu, aimAt(menu, midX, VrMenu::rowTop(row) + 32.0f), t, settings);
        check(a == VrMenu::Action::GameMenu, "Klick auf Spielmenü → Aktion GameMenu");
        menu.mHoverRow = row;
        menu.redraw(settings, t);
        std::snprintf(path, sizeof(path), "%s/xrshell-menu-adventure-spielmenue.bmp", gOutDir.c_str());
        check(writeBmp(path, menu.mPixels, menu.mTexW, menu.mTexH), "Vorschau schreiben");
    }
    menu.setGameMenuAvailable(false);
    menu.setStatesAvailable(true);
    check(has(VrMenu::kSaveState), "Spielstände wieder möglich: Einträge zurück");
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1) gOutDir = argv[1];
    testLogic();
    testLibrary();
    testPadTab();
    previewLibrary();
    testNoStates();
    testResume();
    previewAdventure();
    ViewSettings settings;
    settings.width = 2.4f;
    settings.distance = 1.8f;
    settings.passthrough = 40;

    {
        VrMenu menu;
        menu.mFont.load();
        menu.mFontBold.load(true);
        menu.setTheme(MenuTheme{});   // setzt auch die Farben (sonst in init())
        menu.setOptions(true, true);   // Maus-Core: Vorschau zeigt auch „Spiel-Mauszeiger“
        menu.setLibrary({}, -1, false);
        const char* themeName = "standard";

        const VrMenu::Tab tabs[] = { VrMenu::kTabView, VrMenu::kTabPointer, VrMenu::kTabGame };
        const char* tabNames[] = { "ansicht", "zeiger", "spiel" };
        for (int t = 0; t < 3; ++t) {
            for (int rec = 0; rec < 2; ++rec) {
                if (rec == 1 && t != 2) continue;   // Aufnahme-Zustand nur einmal zeigen
                menu.mTab = tabs[t];
                menu.open(1.8f);
                menu.mHoverRow = 1;                  // zweite Zeile unter dem Strahl
                menu.mHoverZone = VrMenu::kZoneMore;
                menu.mRecording = rec == 1;
                menu.mRecordSeconds = 83;
                menu.mQuitArmedUntil = rec == 1 ? 3.0 : 0.0;   // Aufnahme-Bild zeigt auch „Beenden scharf“
                menu.redraw(settings, 0.0);
                check(menu.mQuitShown == (rec == 1), "Beenden-Knopf scharf gezeichnet");
                const auto list = menu.rows();
                check(!list.empty() && list.back().row == VrMenu::kQuickBar, "Schnellleiste unten im Reiter");
                check(opaquePixels(menu) > menu.mTexW * menu.mTexH / 2, "Tafel überwiegend deckend");
                // Standard-Tafel hat runde Ecken.
                check((menu.mPixels[0] >> 24) < 40, "Ecke oben links durchsichtig");
                char path[512];
                std::snprintf(path, sizeof(path), "%s/xrshell-menu-%s-%s%s.bmp", gOutDir.c_str(), themeName,
                              tabNames[t], rec ? "-aufnahme" : "");
                check(writeBmp(path, menu.mPixels, menu.mTexW, menu.mTexH), "Vorschau schreiben");
            }
        }
    }
    std::printf("test_menu_preview: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
