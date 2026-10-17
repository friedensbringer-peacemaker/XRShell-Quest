// Host-Vorschau + Logik-Test des VR-Menüs (ohne Quest, ohne GL-Kontext): xr.dosgames mit den Reitern
// Spiele · Bild · Zeiger · DOS · Steuerung · Spiel. Rastert die Tafel auf der CPU (GL-Aufrufe sind Stubs aus
// tests/host-stubs/) und schreibt PNGs nach tests/out/xrdos-menu-*.png. Schrift: Systemschrift des
// Rechners (Windows Segoe UI) statt Roboto der Quest — Abstände/Breiten weichen leicht ab.
// Gebaut von tests/run-host-tests.sh (braucht einmal einen Gradle-Build für OpenXR-/stb-Header).
//
// Prüft: Reiter nur mit DOS-Seite, Tafelhöhe (andere Apps unverändert 816 px), Klick ‹ › auf
// DOS-Zeilen, gedimmte Mausgeschwindigkeit, Neustart/Zurücksetzen mit sichtbarer Bestätigung,
// Hohe Auflösung nur mit Profiltaste, Hinweis ohne Spiel, Teil-Redraw nur der geänderten Zeile;
// Reiter Steuerung (rechter Griff, Zieh-Art/-Richtung/-Tempo, rechter Stick), gedimmt ohne „Karte ziehen“.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#define private public   // Zustand/redraw für die Vorschau erreichbar
#include "vr_menu.h"
#undef private

#include "stb_image_write.h"

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 30) std::printf("FEHLER: %s\n", what.c_str());
}

int rowTop(int row) { return 84 + 72 + row * 64; }
// Spaltenlage (mit Bibliothek zweispaltig, 1536 px): ‹ › rechtsbündig, Knöpfe mittig in der
// Reiter-Spalte, Beschriftung links darin.
float moreX(const VrMenu& m) { return static_cast<float>(m.mTexW - 100); }
float lessX(const VrMenu& m) { return static_cast<float>(m.mTexW - 428); }
float midX(const VrMenu& m) { return static_cast<float>((m.contentX0() + m.mTexW) / 2); }
float labelX(const VrMenu& m) { return static_cast<float>(m.contentX0() + 200); }

XrPosef aimAt(const VrMenu& m, float px, float py) {
    XrPosef pose{};
    pose.orientation.w = 1.0f;   // Blick nach −Z
    pose.position.x = (px / static_cast<float>(m.mTexW) - 0.5f) * m.mW;
    pose.position.y = (0.5f - py / static_cast<float>(m.mTexH)) * m.mH;
    pose.position.z = m.mCz + 1.0f;
    return pose;
}

// RGBA-Tafel auf dunklen Grund gemischt als PNG (Ecken bleiben sichtbar rund).
bool writePng(const std::string& path, const VrMenu& m) {
    std::vector<uint8_t> rgb(static_cast<size_t>(m.mTexW) * m.mTexH * 3);
    for (size_t i = 0; i < m.mPixels.size(); ++i) {
        const uint32_t c = m.mPixels[i], a = c >> 24;
        const uint32_t bg[3] = {40, 44, 52};
        for (int k = 0; k < 3; ++k)
            rgb[i * 3 + k] = static_cast<uint8_t>((((c >> (8 * k)) & 0xFF) * a + bg[k] * (255 - a)) / 255);
    }
    return stbi_write_png(path.c_str(), m.mTexW, m.mTexH, 3, rgb.data(), m.mTexW * 3) != 0;
}

DosPage samplePage() {
    DosPage p;
    p.gameRunning = true;
    const char* values[dosopt::kCount] = {"Relativ", "1,0×", "486DX2 66 MHz", "4:3", "S3 Trio64"};
    for (int i = 0; i < dosopt::kCount; ++i) {
        p.options[i].present = true;
        p.options[i].value = values[i];
    }
    // wie main.cpp: Core-Mausgeschwindigkeit ersetzt durch XRShell-Faktoren pro Achse
    p.options[dosopt::kMouseSpeed].present = false;
    p.relative = true;
    p.gainX = "1,00×";
    p.gainY = "2,00×";
    p.hiresKey = "F8";
    p.userValues = true;
    return p;
}

mapdrag::Settings sampleControls() {
    mapdrag::Settings c;
    c.grip = mapdrag::kGripDrag;   // xr.dosgames-Standard
    return c;
}

void setupMenu(VrMenu& m, bool dosPage) {
    m.setTheme(MenuTheme{});
    m.setOptions(true, true);
    m.setLibrary({"DOS\tPirates! Gold", "DOS\tPirates!", "DOS\tPopulous", "DOS\tPopulous II",
                  "DOS\tSimCity 2000", "DOS\tSyndicate Wars"}, 5, true);
    m.setLibraryEmptyHint("Keine Spiele");
    if (dosPage) {
        m.setDosPage(true, samplePage());
        m.setControlsPage(true, true, sampleControls());
    }
    m.open(1.8f);
}

void click(VrMenu& m, float px, float py, ViewSettings& s, double& t, VrMenu::Action& out) {
    const XrPosef aim = aimAt(m, px, py);
    const VrMenu::PadInput none;
    m.update(&aim, false, false, none, t += 0.05, s);
    out = m.update(&aim, true, true, none, t += 0.05, s);
    m.update(&aim, false, false, none, t += 0.05, s);
}

void testLogic() {
    using A = VrMenu::Action;
    ViewSettings s;
    double t = 10.0;
    const VrMenu::PadInput none;

    // Andere Apps: keine DOS-Seite, Reiter „Ansicht“, Tafelhöhe wie bisher (9 Zeilen + Leiste).
    VrMenu plain;
    plain.setTheme(MenuTheme{});
    plain.setOptions(true, true);
    plain.setLibrary({}, -1, false);
    plain.open(1.8f);
    plain.update(nullptr, false, false, none, t, s);
    check(plain.tabs().size() == 3 && plain.mTexH == 84 + 72 + 10 * 64 + 20, "ohne DOS-Seite: 3 Reiter, 816 px hoch");
    bool fit = false;
    for (const auto& it : plain.rows(VrMenu::kTabView)) fit |= it.row == VrMenu::kFitSize;
    check(!fit, "ohne DOS-Seite: kein „Größe passend“");

    // xr.dosgames: 6 Reiter, DOS und Steuerung vor Spiel, Bild mit „Größe passend“ (10 Zeilen + Leiste).
    VrMenu m;
    setupMenu(m, true);
    m.update(nullptr, false, false, none, t, s);
    const auto tabs = m.tabs();
    check(tabs.size() == 5 && tabs[2] == VrMenu::kTabDos && tabs[3] == VrMenu::kTabControls &&
              tabs[4] == VrMenu::kTabGame,
          "xr.dosgames: Reiter Bild/Zeiger/DOS/Steuerung/Spiel (Spiele: Liste links)");
    check(m.contentRows(VrMenu::kTabControls).size() == 5 &&
              m.contentRows(VrMenu::kTabControls)[0].row == VrMenu::kCtlGrip,
          "Steuerung: Griff, Zieh-Art, Zieh-Richtung, Tempo, Stick");
    check(m.mTexH == 84 + 72 + 11 * 64 + 20, "xr.dosgames: Tafel 880 px (Bild hat 10 Zeilen)");
    check(m.contentRows(VrMenu::kTabDos).size() == 10,
          "DOS: Maus, Faktor X/Y, Tempo, Seitenverhältnis, Grafikkarte, Hohe Auflösung, Abgleichen, Neustart, Zurücksetzen");
    check(m.contentRows(VrMenu::kTabDos)[1].row == VrMenu::kDosGainX, "Faktoren direkt unter dem Mausmodus");
    for (VrMenu::Tab tab : tabs) {
        const auto list = m.rows(tab);
        check(list.size() == 11 && list.back().row == VrMenu::kQuickBar, "jeder Reiter: gleiche Höhe, Leiste unten");
    }

    // Klick ‹ › auf CPU-Tempo → DosOption mit Richtung.
    m.mTab = VrMenu::kTabDos;
    m.layout();
    m.update(nullptr, false, false, none, t += 0.1, s);
    const int cyc = m.rowIndex(VrMenu::kDosCycles);
    A a = A::None;
    click(m, moreX(m), static_cast<float>(rowTop(cyc) + 32), s, t, a);
    check(a == A::DosOption && m.dosOption() == dosopt::kCycles && m.dosStep() == 1, "› auf CPU-Tempo");
    click(m, lessX(m), static_cast<float>(rowTop(cyc) + 32), s, t, a);
    check(a == A::DosOption && m.dosStep() == -1, "‹ auf CPU-Tempo");
    click(m, labelX(m), static_cast<float>(rowTop(cyc) + 32), s, t, a);
    check(a == A::DosOption && m.dosStep() == 1, "Klick auf die Beschriftung = eine Stufe weiter");
    // Mausfaktor Y ›, Abgleichen; bei „Laser-genau“ gesperrt mit Hinweis.
    click(m, moreX(m), static_cast<float>(rowTop(m.rowIndex(VrMenu::kDosGainY)) + 32), s, t, a);
    check(a == A::DosGain && m.dosOption() == 1 && m.dosStep() == 1, "› auf Maus senkrecht");
    click(m, midX(m), static_cast<float>(rowTop(m.rowIndex(VrMenu::kDosCalibrate)) + 32), s, t, a);
    check(a == A::DosCalibrate, "Abgleichen");
    DosPage direct = samplePage();
    direct.relative = false;
    m.setDosPage(true, direct);
    click(m, moreX(m), static_cast<float>(rowTop(m.rowIndex(VrMenu::kDosGainX)) + 32), s, t, a);
    check(a == A::None && m.mStatus.find("Relativ") != std::string::npos, "Faktor bei „Laser-genau“ gesperrt");
    m.setDosPage(true, samplePage());
    // Hohe Auflösung.
    click(m, midX(m), static_cast<float>(rowTop(m.rowIndex(VrMenu::kDosHires)) + 32), s, t, a);
    check(a == A::DosHires, "Hohe Auflösung");

    // Neustart: erster Klick scharf (sichtbar), zweiter innerhalb 3 s, sonst zurück.
    const int rs = m.rowIndex(VrMenu::kDosRestart);
    click(m, midX(m), static_cast<float>(rowTop(rs) + 32), s, t, a);
    check(a == A::ConfirmArmed && m.mConfirmShown, "Neustart: erster Klick schaltet sichtbar scharf");
    click(m, midX(m), static_cast<float>(rowTop(rs) + 32), s, t, a);
    check(a == A::RestartGame, "Neustart: zweiter Klick in 3 s");
    click(m, midX(m), static_cast<float>(rowTop(rs) + 32), s, t, a);
    m.update(nullptr, false, false, none, t += 3.5, s);
    check(!m.mConfirmShown, "Neustart fällt nach 3 s zurück");
    // Zurücksetzen: ohne eigene Werte nur Hinweis, mit eigenen Werten Bestätigung.
    DosPage page = samplePage();
    page.userValues = false;
    m.setDosPage(true, page);
    const int rr = m.rowIndex(VrMenu::kDosReset);
    click(m, midX(m), static_cast<float>(rowTop(rr) + 32), s, t, a);
    check(a == A::None && m.mStatus == "Schon Spiel-Standard", "Zurücksetzen ohne eigene Werte: nur Hinweis");
    m.setDosPage(true, samplePage());
    click(m, midX(m), static_cast<float>(rowTop(rr) + 32), s, t, a);
    check(a == A::ConfirmArmed, "Zurücksetzen: scharf");
    click(m, midX(m), static_cast<float>(rowTop(rr) + 32), s, t, a);
    check(a == A::ResetGameOptions, "Zurücksetzen: zweiter Klick");
    // B bricht die Bestätigung ab.
    click(m, midX(m), static_cast<float>(rowTop(rr) + 32), s, t, a);
    VrMenu::PadInput b;
    b.bEdge = true;
    check(m.update(nullptr, false, false, b, t += 0.1, s) == A::Close && m.mConfirmUntil == 0.0, "B: schließt, Bestätigung weg");
    m.open(1.8f);

    // Stick: Fokus auf CPU-Tempo, rechts = DosOption.
    m.mTab = VrMenu::kTabDos;
    m.layout();
    m.mPadFocus = true;
    m.mHoverRow = cyc;
    VrMenu::PadInput right;
    right.stickX = 1.0f;
    check(m.update(nullptr, false, false, right, t += 0.5, s) == A::DosOption && m.dosStep() == 1, "Stick rechts auf CPU-Tempo");
    m.update(nullptr, false, false, none, t += 0.1, s);

    // Teil-Redraw: Wert ändert sich → nur diese Zeile, keine volle Tafel.
    m.mPadFocus = false;
    m.mHoverRow = -1;
    m.redraw(s, t);
    const std::vector<uint32_t> before = m.mPixels;
    page = samplePage();
    page.options[dosopt::kCycles].value = "Maximum";
    m.setDosPage(true, page);
    check(!m.mDirty, "Wertänderung: kein Voll-Redraw");
    int outside = 0, inside = 0;
    for (int y = 0; y < m.mTexH; ++y)
        for (int x = 0; x < m.mTexW; ++x) {
            const bool diff = m.mPixels[static_cast<size_t>(y) * m.mTexW + x] != before[static_cast<size_t>(y) * m.mTexW + x];
            if (!diff) continue;
            if (y >= rowTop(cyc) && y < rowTop(cyc) + 64) ++inside; else ++outside;
        }
    check(inside > 0 && outside == 0, "nur die Zeile CPU-Tempo neu gezeichnet");
    // Ohne Profiltaste keine Zeile „Hohe Auflösung“; Höhe bleibt (Bild hat mehr Zeilen).
    page.hiresKey.clear();
    m.setDosPage(true, page);
    check(m.rowIndex(VrMenu::kDosHires) < 0 && m.mTexH == 84 + 72 + 11 * 64 + 20, "ohne hires: keine Zeile, gleiche Höhe");
    check(m.mTexH == 880, "DOS mit allen Zeilen sprengt die Tafel nicht (10 Zeilen wie Bild)");
    // Ohne Spiel: nur Hinweis, nicht fokussierbar.
    DosPage none2;
    m.setDosPage(true, none2);
    const auto dosRows = m.contentRows(VrMenu::kTabDos);
    check(dosRows.size() == 1 && dosRows[0].row == VrMenu::kDosNoGame && !VrMenu::isFocusable(VrMenu::kDosNoGame),
          "ohne Spiel: Hinweis statt Optionen");
    // Bild: „Größe passend zum Spiel“.
    m.setDosPage(true, samplePage());
    m.mTab = VrMenu::kTabView;
    m.layout();
    m.update(nullptr, false, false, none, t += 0.1, s);
    click(m, midX(m), static_cast<float>(rowTop(m.rowIndex(VrMenu::kFitSize)) + 32), s, t, a);
    check(a == A::FitSize, "Bild: Größe passend zum Spiel");
    // Reiter Steuerung: › auf „Rechter Griff“ → ControlOption; gedimmte Zieh-Zeilen nur mit Hinweis.
    m.mTab = VrMenu::kTabControls;
    m.layout();
    m.update(nullptr, false, false, none, t += 0.1, s);
    click(m, moreX(m), static_cast<float>(rowTop(m.rowIndex(VrMenu::kCtlGrip)) + 32), s, t, a);
    check(a == A::ControlOption && m.controlRow() == mapdrag::kRowGrip && m.dosStep() == 1, "› auf Rechter Griff");
    click(m, lessX(m), static_cast<float>(rowTop(m.rowIndex(VrMenu::kCtlSpeed)) + 32), s, t, a);
    check(a == A::ControlOption && m.controlRow() == mapdrag::kRowSpeed && m.dosStep() == -1, "‹ auf Zieh-Tempo");
    mapdrag::Settings rmb = sampleControls();
    rmb.grip = mapdrag::kGripRightClick;
    m.redraw(s, t);
    std::vector<uint32_t> before2 = m.mPixels;
    m.setControlsPage(true, true, rmb);
    check(!m.mDirty, "Steuerung: Wertänderung ohne Voll-Redraw");
    int outsideRows = 0;
    const int firstTop = rowTop(0), lastBottom = rowTop(5);
    for (int y = 0; y < m.mTexH; ++y)
        for (int x = 0; x < m.mTexW; ++x)
            if (m.mPixels[static_cast<size_t>(y) * m.mTexW + x] != before2[static_cast<size_t>(y) * m.mTexW + x] &&
                (y < firstTop || y >= lastBottom))
                ++outsideRows;
    check(outsideRows == 0, "Steuerung: nur die fünf Zeilen neu gezeichnet");
    click(m, moreX(m), static_cast<float>(rowTop(m.rowIndex(VrMenu::kCtlModel)) + 32), s, t, a);
    check(a == A::None && m.mStatus.find("Karte ziehen") != std::string::npos, "Zieh-Art ohne Karte ziehen: gesperrt");
    click(m, moreX(m), static_cast<float>(rowTop(m.rowIndex(VrMenu::kCtlStick)) + 32), s, t, a);
    check(a == A::ControlOption && m.controlRow() == mapdrag::kRowStick, "Rechter Stick bleibt einstellbar");
    m.setControlsPage(true, false, rmb);
    check(m.contentRows(VrMenu::kTabControls).size() == 1 &&
              m.contentRows(VrMenu::kTabControls)[0].row == VrMenu::kDosNoGame,
          "Steuerung ohne Spiel: Hinweis");
    m.setControlsPage(true, true, sampleControls());
    // Andere Apps: kein Reiter Steuerung.
    check(plain.tabs().size() == 3, "ohne DOS-Seite: kein Reiter Steuerung");

    // Info in der Kopfzeile ohne Statusmeldung.
    m.setInfo("320×200 · 4:3");
    check(m.mInfoDirty, "Info: Kopfzeile neu");
    m.update(nullptr, false, false, none, t += 5.0, s);
    check(!m.mInfoDirty, "Info gezeichnet");
}

void renderPreviews() {
    ViewSettings settings;
    settings.width = 2.0f;
    settings.distance = 2.0f;
    settings.passthrough = 0;
    settings.filter = static_cast<int>(ScreenFilter::Hd);
    struct Shot { const char* name; VrMenu::Tab tab; int hover; bool pending; bool armed; };
    const Shot shots[] = {
        {"steuerung", VrMenu::kTabControls, 0, false, false},
        {"steuerung-hand-folgt", VrMenu::kTabControls, 3, false, false},
        {"steuerung-rechtsklick", VrMenu::kTabControls, -1, false, false},
        {"spiele", VrMenu::kTabView, -1, false, false},   // Liste steht links in jedem Reiter
        {"bild", VrMenu::kTabView, 2, false, false},
        {"zeiger", VrMenu::kTabPointer, -1, false, false},
        {"dos", VrMenu::kTabDos, 2, false, false},
        {"dos-neustart", VrMenu::kTabDos, -1, true, true},
        {"dos-laser-genau", VrMenu::kTabDos, -1, false, false},
        {"spiel", VrMenu::kTabGame, -1, false, false},
    };
    for (const Shot& shot : shots) {
        VrMenu m;
        m.mFont.load();
        m.mFontBold.load(true);
        setupMenu(m, true);
        DosPage page = samplePage();
        if (std::string(shot.name) == "dos-laser-genau") {
            page.options[dosopt::kMouse].value = "Laser-genau";
            page.relative = false;
        }
        if (shot.pending) {
            page.options[dosopt::kSvga].value = "Tseng ET4000";
            page.options[dosopt::kSvga].pending = true;
            page.restartPending = true;
        }
        m.setDosPage(true, page);
        mapdrag::Settings controls = sampleControls();
        if (std::string(shot.name) == "steuerung-hand-folgt") controls.model = mapdrag::kFollow;
        if (std::string(shot.name) == "steuerung-rechtsklick") controls.grip = mapdrag::kGripRightClick;
        m.setControlsPage(true, true, controls);
        m.setInfo("320×200 · 4:3");
        m.mTab = shot.tab;
        m.layout();
        m.mHoverRow = shot.hover;
        m.mHoverZone = VrMenu::kZoneMore;
        if (shot.armed) {
            m.mConfirmRow = VrMenu::kDosRestart;
            m.mConfirmUntil = 3.0;
        }
        m.redraw(settings, 0.0);
        check(m.mTexH == 880, "Vorschau: Tafelhöhe");
        const std::string path = std::string("tests/out/xrdos-menu-") + shot.name + ".png";
        check(writePng(path, m), "Vorschau schreiben: " + path);
    }
}

} // namespace

int main() {
    testLogic();
    renderPreviews();
    std::printf("menu_preview: %d Prüfungen, %d Fehler (Bilder: tests/out/xrdos-menu-*.png)\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
