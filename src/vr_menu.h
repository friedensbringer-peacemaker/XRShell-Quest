// XRShell — VR-Menü (linke Menütaste öffnet/schließt, B schließt).
// Mit Bibliothek (xr.dosgames, xr.shooter, xr.adventure) ist die Tafel zweispaltig: links die Spieleliste mit Suchfeld
// (Klick ins Feld → Bildschirmtastatur in der rechten Spalte, Liste filtert live), rechts
// die Reiter. Reiter:
//   ANSICHT: Passthrough, Wölbung, Filter, Sättigung, Kontrast, Bildgröße,
//            Abstand, Umgebung ausblenden (experimentell), Fenster zentrieren
//            (xr.dosgames: heißt „Bild“, dazu „Größe passend zum Spiel“)
//   ZEIGER:  Strahlfarbe, Strahl (… / Aus), Zielpunkt (… / Aus), Spiel-Mauszeiger, Bildzoom
//   DOS:     nur xr.dosgames — Mausmodus, Mausgeschwindigkeit, CPU-Tempo, Seitenverhältnis,
//            Grafikkarte (DOSBox-Pure-Optionen pro Spiel), Hohe Auflösung (Taste aus dem
//            Spielprofil), Spiel neu starten, Spiel-Standard wiederherstellen
//   STEUERUNG: nur xr.dosgames — rechter Griff (Karte ziehen / Rechtsklick / Aus), Zieh-Art,
//            Zieh-Richtung, Zieh-Tempo bzw. Pixel je Tastendruck, rechter Stick (map_drag.h)
//   TASTEN:  (Pad-Cores) Pad-Tasten antippen + Quest-Taste drücken, Sticks, für alle/zurück
//   SPIEL:   Spielstand-Platz, speichern, laden, Profil-Platz, Profil speichern/laden, Tastatur (DOS)
// Unten in jedem Reiter die Schnellleiste: Video · Schließen (= Weiter) · Beenden
// (erster Klick schaltet sichtbar scharf, zweiter innerhalb 3 s beendet).
// Pflichtumfang: Skill xr-vr-menu §0. TODO xrc/menu.h: Bestätigung, Zeiger-Sichtbarkeit
// und Klemmen auf xr-common umstellen, sobald XRShell xr-common einbindet.
//
// Die Tafel hängt lokal am Bildschirm (dreht und wandert mit), Bedienung per
// Laser + Trigger oder rechtem Stick + A (sichtbarer Fokus). Inhalt wird auf der CPU
// mit der System-TTF (Fallback: Pixelschrift) in eine Textur gezeichnet und nur in den
// geänderten Streifen neu hochgeladen. Die Tafelhöhe ist für alle Reiter gleich.
//
// GLES nur im Render-Thread (init/draw/update auf dem Main-Thread).

#pragma once

#include "dos_options.h"     // DOS-Seite: Optionen, Beschriftungen
#include "map_drag.h"        // Reiter Steuerung: Griff/Stick pro Spiel
#include "menu_theme.h"      // Optik aus Spielgrafik (xrshell.theme)
#include "pad_mapping.h"     // Tastenbelegung der Pad-Cores
#include "quad_renderer.h"   // ScreenFilter, raycastToQuad
#include "text_raster.h"

#include <GLES3/gl3.h>
#include <openxr/openxr.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Alles, was das Menü einstellt — <internalDataPath>/xrshell-view.cfg (aktuell)
// bzw. xrshell-profile<N>.cfg (Profile).
struct ViewSettings {
    int passthrough = 0;     // 0..100 %, 10er-Schritte
    int curve = 0;           // 0 keine, 1 leicht, 2 mittel, 3 stark
    int filter = 0;          // ScreenFilter
    // Filter im Menü gewählt? Sonst gilt der Standard des Spiels/der App (xrshell.opt
    // „xrshell_filter“, xr.dosgames: HD) und wird nur angezeigt, nicht als Wahl gespeichert.
    bool filterChosen = false;
    int saturation = 100;    // 0..200 %
    int contrast = 100;      // 50..150 %
    int environment = 0;     // experimentell: 0 aus, 1 Himmel, 2 + Horizont, 3 nur Strecke
    float width = 0.0f;      // Fensterbreite in m (0 = Standard)
    float distance = 0.0f;   // Abstand Kopf → Bild in m (0 = Standard)
    // Reiter „Zeiger“
    int rayColor = 0;        // Orange, Weiß, Cyan, Grün, Magenta, Gelb
    int rayStyle = 0;        // Normal, Dünn, Dick, Kurz, Aus (nur Zielpunkt)
    int marker = 0;          // Zielpunkt: Normal, Groß, Klein, Aus
    int zoom = 0;            // Bildzoom 1× / 1,5× / 2× / 3× — nicht gespeichert
    int gameCursor = 0;      // Spiel-Mauszeiger: 0 An, 1 Aus (wirkt, wo der Core es kann)
    // Spielzeiger verschieben (Maus-Cores): Schritte zu ptroff::kStep % der Bildbreite/-höhe,
    // positiv = rechts/unten. xr.dosgames speichert sie pro Spiel (xrshell_pointer_offset_x/y).
    int pointerOffsetX = 0, pointerOffsetY = 0;
    // Bibliotheks-Apps: was beim App-Start passiert — 0 Menü mit Spieleliste (Standard, Knopf
    // „Weiterspielen“), 1 letztes Spiel sofort fortsetzen. Global, nicht in Profilen.
    int startMode = 0;
    static constexpr int kRayColors = 6, kRayStyles = 5, kMarkers = 4, kZooms = 4, kGameCursors = 2;
    static constexpr int kStartModes = 2, kStartResume = 1;
    static constexpr int kRayStyleOff = 4, kMarkerOff = 3;
    static constexpr int kPointerOffsetSteps = 60;   // ±60 × 0,25 % = ±15 %
    float pointerOffsetPercentX() const;
    float pointerOffsetPercentY() const;
    void pointerRgb(float out[3]) const;
    // menuOpen: im offenen Menü bleiben Strahl und Zielpunkt immer sichtbar (Pflicht P3).
    float rayThickness(bool menuOpen = false) const;   // m, 0 = kein Strahl
    float rayMaxLength(bool menuOpen = false) const;   // m
    float markerSize(bool menuOpen = false) const;     // m, 0 = kein Zielpunkt
    bool gameCursorVisible() const { return gameCursor == 0; }
    float zoomFactor() const;

    bool load(const std::string& path);
    bool save(const std::string& path) const;
    // Wölbungsradius zur Stufe: Bogen von 0/30/60/90° über die Bildbreite.
    float curveRadius(float screenWidth) const;
    bool operator!=(const ViewSettings& o) const;
};

// Reiter „DOS“ (xr.dosgames): was die Zeilen zeigen. Der Aufrufer füllt ihn aus den Kern-Optionen
// des laufenden Spiels; das Menü meldet nur „Option i um eine Stufe ändern“ (Action::DosOption).
struct DosPage {
    struct Option {
        bool present = false;      // Core kennt die Option (sonst keine Zeile)
        std::string value;         // Anzeige
        bool dim = false;          // wirkt gerade nicht (z. B. Mausgeschwindigkeit bei „Laser-genau“)
        bool pending = false;      // geändert, wirkt erst nach „Spiel neu starten“
        bool operator==(const Option& o) const {
            return present == o.present && value == o.value && dim == o.dim && pending == o.pending;
        }
    };
    Option options[dosopt::kCount];
    std::string hiresKey;          // Taste aus dem Spielprofil („hires = F8“), leer = keine Zeile
    bool restartPending = false;   // mindestens eine Änderung wartet auf den Neustart
    bool userValues = false;       // es gibt eigene Werte für dieses Spiel (Zurücksetzen sinnvoll)
    bool gameRunning = false;      // ohne Spiel nur ein Hinweis
    // Relative Maus (XRShell-Faktoren pro Achse, Abgleich) — gedimmt, solange „Laser-genau“ gilt.
    std::string gainX, gainY;      // leer = keine Zeilen
    bool relative = false;
    int cursor = 0;                // „Zeiger lernen“: 0 aus, 1 gekoppelt, 2 pausiert (sucht), 3 lernt
    bool operator==(const DosPage& o) const {
        for (int i = 0; i < dosopt::kCount; ++i)
            if (!(options[i] == o.options[i])) return false;
        return hiresKey == o.hiresKey && restartPending == o.restartPending && userValues == o.userValues &&
               gameRunning == o.gameRunning && gainX == o.gainX && gainY == o.gainY && relative == o.relative &&
               cursor == o.cursor;
    }
    bool operator!=(const DosPage& o) const { return !(*this == o); }
};

class VrMenu {
public:
    enum class Action {
        None, Changed, WindowChanged, Recenter, ToggleKeyboard, Close,
        SaveState, LoadState, SaveProfile, LoadProfile, Quit, StartGame, ToggleRecording,
        QuitArmed,  // erster Klick auf Beenden: Knopf scharf (Aufrufer: Doppelpuls Haptik)
        PadChanged,     // Tastenbelegung geändert → padMapping() für dieses Spiel speichern
        PadSaveSystem,  // „Für alle <System>-Spiele“ → padMapping() als Systemstandard
        PadReset,       // „Zurücksetzen“ → Spielbelegung verwerfen (Aufrufer lädt neu, setPad)
        StartModeChanged,   // „Beim Start“ umgestellt → Einstellungen sofort speichern
        GameMenu,           // „Spielmenü öffnen“ → Profil-Eingabe game_menu nach dem Schließen senden
        ConfirmArmed,  // erster Klick auf Neustart/Zurücksetzen: Knopf scharf (Haptik wie oben)
        DosOption,     // Reiter DOS: Option dosOption() um dosStep() weiterschalten
        DosHires,      // Taste „hires“ aus dem Spielprofil senden (Menü schließt)
        DosGain,       // Mausfaktor X (dosOption() 0) bzw. Y (1) um dosStep() Stufen
        DosCalibrate,  // Maus abgleichen: Menü zu, Zeiger fangen, nächster Trigger = Spielzeiger
        DosLearnPointer,   // Zeiger lernen: Menü zu, Spielzeiger im Speicher suchen und koppeln
        RestartGame,   // Spiel neu starten (zweiter Klick)
        ResetGameOptions,   // Nutzerwerte des Spiels verwerfen (zweiter Klick)
        FitSize,       // Bildgröße passend zur Spielauflösung
        ControlOption  // Reiter Steuerung: Zeile controlRow() um dosStep() weiterschalten
    };
    // Stick-/Tastenbedienung (rechte Hand) für den Fokus — wirkt auch ohne Strahl auf der Tafel.
    struct PadInput {
        float stickX = 0.0f, stickY = 0.0f;   // rechter Stick, +Y = vorn
        bool aEdge = false;                   // A gedrückt (Flanke) = Fokus auslösen
        bool bEdge = false;                   // B gedrückt (Flanke) = Menü schließen
        bool sources[padmap::kSourceCount] = {};   // alle Quest-Tasten (Zuweisen im Reiter Tasten)
    };
    static constexpr int kStateSlots = 5, kProfileSlots = 3;

    bool init();
    void shutdown();

    bool visible() const { return mVisible; }
    // Tafel vor die Bildmitte (lokale Koordinaten), höchstens 0,6 m näher und nie
    // näher als 0,6 m am Kopf. headDistance = Abstand Kopf → Bildmitte.
    void open(float headDistance);
    // Nur die Lage nachführen (Fenstergröße/-abstand geändert) — kein Neuzeichnen.
    void reposition(float headDistance);
    void close() { mVisible = false; }
    // Menü-Optik des Spiels (inaktives Theme = Standardoptik). Wirkt beim nächsten Zeichnen.
    void setTheme(const MenuTheme& theme);

    // Maus-Cores (DOS): Einträge „Tastatur“ und „Spiel-Mauszeiger“; Passthrough ohne
    // Runtime-Support grau, Spiel-Mauszeiger grau, wenn der Core ihn nicht ausblenden kann.
    void setOptions(bool mouseCore, bool passthroughAvailable, bool gameCursorAvailable = false);
    // Reiter „Spiel“: Core ohne Spielstände (ScummVM) → Hinweis statt Speichern/Laden.
    void setStatesAvailable(bool available);
    // Ohne Savestates: Knopf „Spielmenü öffnen (Speichern/Laden)“ statt Hinweis (ScummVM: Strg+F5).
    void setGameMenuAvailable(bool available);
    // Reiter „Ansicht“: experimentelle Zeile „Umgebung“ (Himmel/Horizont ausblenden).
    void setEnvironmentAvailable(bool available);
    // Reiter „Spiel“: kurze Bedienungshilfe (je Zeile „Taste Wirkung · Taste Wirkung“), leer = keine.
    void setControlsHelp(const std::vector<std::string>& lines);
    // Kurze Rückmeldung in der Titelzeile (z. B. „Spielstand 2 gespeichert“).
    void setStatus(const std::string& text, double now);
    // Videoaufnahme: läuft sie (Sekunden) und ist sie möglich? Leiste zeigt „● Video“ bzw. „■ mm:ss“.
    void setRecording(bool recording, int seconds, bool available);
    // Bibliothek: Einträge „System<TAB>Titel“; running = laufendes Spiel (-1 = keins).
    // enabled = linke Spalte mit Spieleliste und Suche (breitere Tafel); sonst einspaltig.
    // last = zuletzt gespieltes Spiel (-1 = keins): Abzeichen „zuletzt“, Knopf „Weiterspielen“
    // im Reiter „Spiel“, solange kein Spiel läuft.
    void setLibrary(const std::vector<std::string>& labels, int running, bool enabled, int last = -1);
    // Hinweis bei leerer Bibliothek (ein führendes „Keine Spiele – “ wird zur Überschrift).
    void setLibraryEmptyHint(const std::string& emptyHint);
    void showGamesTab();                    // Liste auf die Seite des laufenden Spiels, Fokus aufs Suchfeld
    int selectedGame() const { return mSelectedGame; }   // Index in labels (nicht in der Trefferliste)
    // Reiter „DOS“ (xr.dosgames): enabled = Reiter zeigen; Inhalt wird bei Änderung zeilenweise neu gezeichnet.
    void setDosPage(bool enabled, const DosPage& page);
    int dosOption() const { return mDosOption; }
    // Reiter „Steuerung“ (xr.dosgames): enabled = Reiter zeigen, gameRunning = Zeilen statt Hinweis.
    void setControlsPage(bool enabled, bool gameRunning, const mapdrag::Settings& controls);
    int controlRow() const { return mControlRow; }   // mapdrag::Row der Action::ControlOption
    int dosStep() const { return mDosStep; }
    // Kleine Info rechts in der Kopfzeile, solange keine Statusmeldung läuft (z. B. „320×200 · 4:3“).
    void setInfo(const std::string& text);
    // Reiter „Tasten“ (Pad-Cores): System (Bibliotheksordner) und aktuelle Belegung.
    void setPad(bool enabled, const std::string& systemFolder, const padmap::Mapping& mapping);
    const padmap::Mapping& padMapping() const { return mPadMap; }
    bool padLearning() const { return mLearnTarget >= 0; }   // wartet auf eine Quest-Taste
    // Kopfzeile, z. B. „xr.dosgames“ + „DOS-Spiele“ (Standard: „XR-Menü“).
    void setTitle(const std::string& title, const std::string& subtitle = "");
    // Suche: Treffer, wenn jedes Wort der Anfrage in System oder Titel vorkommt — ohne
    // Groß-/Kleinschreibung, Umlaute/Akzente gefaltet (ä = a, ß = ss), Satzzeichen egal.
    static bool matchesSearch(const std::string& label, const std::string& query);

    // Pro Frame mit dem Strahl in Bildschirm-Koordinaten. Ändert settings.
    // triggerHeld: Trigger gehalten → ‹ › wiederholen (nach 0,45 s alle 0,12 s).
    Action update(const XrPosef* localAim, bool triggerPressedEdge, bool triggerHeld,
                  const PadInput& pad, double now, ViewSettings& settings);
    bool hovered() const { return mHoverRow >= 0 || mHoverTab >= 0 || mHoverLib >= 0 || mHoverKey >= 0; }
    bool focusMoved() const { return mFocusMoved; }   // Stick hat den Fokus bewegt (Haptik)
    float hitDistance() const { return mHitDistance; }   // Laserlänge bis zur Tafel
    int stateSlot() const { return mStateSlot; }
    int profileSlot() const { return mProfileSlot; }

    // eyeView = Blickmatrix · Bildschirm-Modell (Tafel liegt lokal am Bildschirm).
    void draw(const float* localView, const float* eyeProj);

private:
    enum Row {
        kPassthrough, kCurve, kFilter, kSaturation, kContrast, kSize, kDistance,
        kEnvironment, kRayColor, kRayStyle, kMarker, kZoom, kRecenter,
        kStateSlot, kSaveState, kLoadState, kProfileSlot, kSaveProfile, kLoadProfile,
        kGameCursor, kPointerOffsetX, kPointerOffsetY, kKeyboard,
        kGame, kPage, kLibraryEmpty,
        kSearch,                            // Suchfeld über der Spieleliste (Höhe der Reiterleiste)
        kNoMatch,                           // Suche ohne Treffer
        kLibraryHint,                       // zweite Zeile der leeren Bibliothek: Zielordner
        kDosMouse, kDosMouseSpeed, kDosCycles, kDosAspect, kDosSvga,   // Reihenfolge = dosopt::Id
        kDosHires, kDosRestart, kDosReset, kFitSize, kDosNoGame, kNoStates,
        kResume, kStartMode,                // Reiter „Spiel“ (Bibliothek): Weiterspielen, Beim Start
        kGameMenu,                          // Reiter „Spiel“ ohne Savestates: Spielmenü öffnen
        kHelp,                              // Reiter „Spiel“: Bedienungshilfe (game = Zeilenindex)
        kDosGainX, kDosGainY, kDosCalibrate, kDosLearn,
        kCtlGrip, kCtlModel, kCtlDirection, kCtlSpeed, kCtlStick,   // Reihenfolge = mapdrag::Row
        kSpacer,                            // Füllzeile: Schnellleiste steht in jedem Reiter gleich tief
        kQuickBar,                          // unten in jedem Reiter: Aufnahme · Schließen · Beenden
        // Reiter „Tasten“ (nach kQuickBar: isValueRow prüft r <= kZoom)
        kPadHint,                           // Kurzanleitung (wenn Platz ist)
        kPadPair,                           // zwei Konsolentasten (game = Index der ersten)
        kPadStickL, kPadStickR,             // Stick-Modus ‹ ›
        kPadActions                         // „Für alle …-Spiele“ · „Zurücksetzen“
    };
    enum Zone { kZoneRow, kZoneLess, kZoneMore, kZoneBar0, kZoneBar1, kZoneBar2 };
    enum Tab { kTabView, kTabPointer, kTabPad, kTabDos, kTabControls, kTabGame };
    struct Item { Row row; int game; };    // game = Index in mLibrary (nur kGame)
    // Bildschirmtastatur der Suche (rechte Spalte): Taste mit Rechteck in Texturpixeln.
    enum KeyKind { kKeyChar, kKeySpace, kKeyBackspace, kKeyClear, kKeyDone };
    struct Key { const char* label; const char* insert; KeyKind kind; int row; int x0, y0, x1, y1; };
    // Fokus-/Strahlziel: höchstens eines von row (rechte Spalte), tab, lib (linke Spalte), key.
    struct Focus {
        int row = -1, tab = -1, lib = -1, key = -1;
        Zone zone = kZoneRow;
        bool operator==(const Focus& o) const {
            return row == o.row && tab == o.tab && lib == o.lib && key == o.key && zone == o.zone;
        }
        bool operator!=(const Focus& o) const { return !(*this == o); }
        bool any() const { return row >= 0 || tab >= 0 || lib >= 0 || key >= 0; }
    };

    std::vector<Tab> tabs() const;          // sichtbare Reiter
    std::vector<Item> rows() const { return rows(mTab); }   // Zeilen des aktiven Reiters
    std::vector<Item> rows(Tab tab) const;  // inkl. Füllzeilen bis zur Schnellleiste
    std::vector<Item> contentRows(Tab tab) const;   // ohne Füllzeilen und Schnellleiste
    // Linke Spalte: [0] Suchfeld, dann eine Seite Spiele (aufgefüllt), Seitenzeile, Leerzeile.
    std::vector<Item> libRows() const;
    int gamesPerPage() const;
    int pageCount() const;
    void refilter();                        // mFiltered aus mLibrary + mQuery
    static bool isValueRow(Row r);
    static bool isFocusable(Row r) {
        return r != kSpacer && r != kLibraryEmpty && r != kNoMatch && r != kLibraryHint && r != kPadHint &&
               r != kDosNoGame && r != kNoStates && r != kHelp;
    }
    static bool isConfirmRow(Row r) { return r == kDosRestart || r == kDosReset; }
    int rowIndex(Row r) const;              // Zeile im aktiven Reiter, -1 = nicht da
    // Zeilen mit mehreren Knöpfen nebeneinander (Zonen kZoneBar0…): Anzahl, Spanne, Ankunftszone.
    int zoneCount(const Item& item) const;
    void zoneSpan(const Item& item, int& x0, int& x1) const;
    Zone arriveZone(const Item& item, int preferred = -1) const;
    void redrawPadRows(const ViewSettings& settings);
    void layout();                          // Texturgröße/Tafelgröße (größter Reiter), Tastatur
    // Raster: linke Kante der Reiter-Spalte, Oberkante einer Zeile, Oberkante der Schnellleiste.
    int contentX0() const { return mLibraryEnabled ? kLibW : 0; }
    static int rowTop(int row) { return kTitleH + kTabH + row * kRowH; }
    static int libTop(int lib) { return lib == 0 ? kTitleH : rowTop(lib - 1); }
    int barTop() const { return rowTop(static_cast<int>(rows().size()) - 1); }
    Focus hitTest(float px, float py, const std::vector<Item>& list, const std::vector<Tab>& tabList,
                  const std::vector<Item>& lib) const;
    Action activate(const std::vector<Item>& list, int row, Zone zone, double now, ViewSettings& settings);
    Action activateLib(const std::vector<Item>& lib, int index, Zone zone);
    void pressKey(int key);
    void setSearchActive(bool active);
    void armQuit(double now);
    void movePadFocus(const PadInput& pad, double now, const std::vector<Item>& list,
                      const std::vector<Tab>& tabList, ViewSettings& settings, Action& action);
    int keyToward(int from, int dir) const; // Nachbartaste (1 hoch, 2 runter, 3 links, 4 rechts)
    Focus initialFocus(const std::vector<Item>& list, const std::vector<Item>& lib) const;
    float fitPx(const char* utf8, int maxW, float px, bool bold = false) const;
    std::string ellipsize(std::string s, int maxW, float px, bool bold = false) const;
    void redrawTitle(double now);           // nur Kopfzeile (Statusmeldung) neu
    void drawTitleText(double now);
    void redraw(const ViewSettings& settings, double now);
    void redrawHover(const ViewSettings& settings, int oldRow, int newRow, bool tabsChanged);
    void redrawFocus(const ViewSettings& settings, const Focus& before, const Focus& after);
    void redrawLibrary();                   // ganze linke Spalte (+ Suchanzeige rechts)
    void redrawRight(const ViewSettings& settings);   // rechte Spalte + Suchfeld (Reiter, Tastatur)
    // Teil-Redraw: Streifen sammeln, dann einmal hochladen.
    void partRow(const ViewSettings& settings, int row);
    void partLib(int lib);
    void partKey(int key);
    void partTabs();
    void partUpload();
    void drawTabs();
    void drawSearchBand();                  // rechte Reiterleiste im Suchmodus: Anfrage groß
    void drawRow(size_t i, const Item& item, const ViewSettings& settings);
    void drawLib(size_t i, const Item& item);
    void drawPadRow(size_t i, const Item& item);
    void drawKey(int k);
    void hoverCard(int x0, int y0, int x1, int y1, bool hover);   // Zeilenkarte (+ Akzentbalken)
    void softButton(int x0, int y0, int x1, int y1, int r, uint32_t tone, bool filled);
    void knob(int cx, int cy, bool left, bool hover);   // runder Pfeilknopf ‹ ›
    void text(const char* utf8, int x, int y, float px, uint32_t color, bool bold = false);
    int textWidth(const char* utf8, float px, bool bold = false) const;
    void sysText(const char* utf8, int x, int y, float px, uint32_t color, bool bold);   // System-TTF
    int sysTextWidth(const char* utf8, float px, bool bold) const;
    void blend(int x, int y, uint32_t color, float cov);            // Pixel mischen
    void roundRect(int x0, int y0, int x1, int y1, int r, uint32_t color);
    void roundRectV(int x0, int y0, int x1, int y1, int r, uint32_t top, uint32_t bottom);
    const uint8_t* cornerMask(int r) const;   // Eckglättung je Radius (Tabelle r×r)
    mutable std::unordered_map<int, std::vector<uint8_t>> mCornerMasks;
    void circle(int cx, int cy, int r, uint32_t color);
    void fillRect(int x0, int y0, int x1, int y1, uint32_t color);
    // Menü-Optik: Bild gestreckt (nächster Nachbar, Alpha gemischt), dreiteilig
    // gestreckt (Ränder im Seitenverhältnis), Tafelhintergrund in einem Bereich,
    // Tönung über einem Bereich.
    void image(const ThemeImage& img, int x0, int y0, int x1, int y1);
    void image3(const ThemeImage& img, int x0, int y0, int x1, int y1);
    void panelArea(int x0, int y0, int x1, int y1);
    uint32_t panelColor(int y) const;       // Verlauf der Glas-Tafel (oben heller)
    void tint(int x0, int y0, int x1, int y1, uint32_t color);

    MenuTheme mTheme;
    // Farben der aktuellen Optik (Standard oder Theme); init() setzt die Standardwerte.
    uint32_t mColText = 0, mColDim = 0, mColDark = 0, mColAccent = 0;

    bool mVisible = false;
    bool mKeyboardEntry = false;            // Maus-Core: Tastatur + Spiel-Mauszeiger
    bool mPassthroughAvailable = true;
    bool mGameCursorAvailable = false;
    bool mStatesAvailable = true;           // Core kann Spielstände (Reiter „Spiel“)
    bool mGameMenuAvailable = false;        // Profil-Eingabe game_menu belegt (ScummVM: Strg+F5)
    bool mEnvironmentAvailable = true;      // Zeile „Umgebung (exp.)“ zeigen
    std::vector<std::string> mHelp;         // Bedienungshilfe im Reiter „Spiel“
    bool mSingleSystem = false;             // alle Spiele ein System → keine System-Plakette
    Tab mTab = kTabView;
    std::vector<std::string> mLibrary;      // „System<TAB>Titel“
    bool mLibraryEnabled = false;
    int mRunningGame = -1, mSelectedGame = -1, mPage = 0;
    int mLastGame = -1;                     // zuletzt gespielt (Index in mLibrary), -1 = keins
    std::string mQuery;                     // Suchtext (UTF-8, klein)
    std::vector<int> mFiltered;             // Indizes in mLibrary, die zur Suche passen
    std::vector<std::string> mSearchHay, mSearchSqueezed;   // gefaltete Titel (je Bibliothek einmal)
    bool mSearchActive = false;             // Tastatur rechts eingeblendet
    bool mLibDirty = false;                 // Liste/Suchtext geändert → linke Spalte neu
    bool mRightDirty = false;               // Reiter gewechselt / Suchmodus → rechte Spalte neu
    std::string mEmptyTitle = "Noch keine Spiele";
    std::string mEmptyHint = "Spielordner nach files/games/ kopieren";
    // Neustart / Zurücksetzen (Reiter DOS): gleiche Bestätigung wie Beenden
    Row mConfirmRow = kSpacer;
    double mConfirmUntil = 0.0;
    bool mConfirmShown = false;
    bool mDosPage = false;                  // Reiter „DOS“ (xr.dosgames)
    DosPage mDos;
    int mDosOption = 0, mDosStep = 1;
    bool mControlsPage = false, mControlsGame = false;   // Reiter „Steuerung“ (xr.dosgames)
    mapdrag::Settings mControls;
    int mControlRow = 0;
    std::string mInfo;                      // Kopfzeile rechts (ohne Statusmeldung)
    bool mInfoDirty = false;
    std::vector<Key> mKeys;
    std::string mTitle = "XR-Menü", mSubtitle;
    // Reiter „Tasten“
    bool mPadEnabled = false;
    std::string mPadFolder;
    padmap::Mapping mPadMap{};
    int mLearnTarget = -1;                  // RetroPad-ID, die auf eine Quest-Taste wartet
    double mLearnUntil = 0.0;               // Zuweisen bricht nach 5 s ab
    bool mPrevSources[padmap::kSourceCount] = {};
    bool mPadRowsDirty = false;             // Belegung/Zuweisen geändert → Tasten-Zeilen neu
    int mStateSlot = 1, mProfileSlot = 1;
    bool mRecording = false, mRecordAvailable = true;
    int mRecordSeconds = 0;
    double mQuitArmedUntil = 0.0;           // Beenden: zweiter Klick innerhalb 3 s
    bool mQuitShown = false;                // Knopf gerade scharf gezeichnet
    std::string mStatus;
    double mStatusUntil = 0.0;
    bool mStatusShown = false;
    std::vector<uint32_t> mTitleBg;         // Kopfzeilen-Hintergrund ohne Text (Teil-Redraw)
    uint32_t mTitleColor = 0;

    // Dauerdruck auf ‹ › und Stick-Fokus
    int mRepeatRow = -1, mRepeatKey = -1;
    Zone mRepeatZone = kZoneRow;
    double mRepeatNext = 0.0;
    bool mPadFocus = false;
    Focus mRay;                             // letztes Strahlziel: Strahl übernimmt erst, wenn er wandert
    int mStickDir = 0;                      // gehaltene Stick-Richtung (1 hoch, 2 runter, 3 links, 4 rechts)
    double mStickNext = 0.0;
    bool mFocusMoved = false;

    float mCz = 0.6f, mW = 1.2f, mH = 0.9f;   // Tafel (lokal: Mitte x = y = 0)
    // Fokus (Strahl oder Stick): rechte Zeile, Reiter, linke Spalte, Taste — höchstens einer ≥ 0.
    int mHoverRow = -1, mHoverTab = -1, mHoverLib = -1, mHoverKey = -1;
    Zone mHoverZone = kZoneRow;
    Focus hover() const {
        Focus f;
        f.row = mHoverRow; f.tab = mHoverTab; f.lib = mHoverLib; f.key = mHoverKey; f.zone = mHoverZone;
        return f;
    }
    void setHover(const Focus& f) {
        mHoverRow = f.row; mHoverTab = f.tab; mHoverLib = f.lib; mHoverKey = f.key; mHoverZone = f.zone;
    }
    int mPartMin = 0, mPartMax = 0;         // Teil-Redraw: gesammelter Streifen
    float mHitDistance = 4.0f;
    bool mDirty = true;
    ViewSettings mShown;

    TextRaster mFont, mFontBold;
    std::vector<uint32_t> mPixels;          // RGBA8 (Byte-Reihenfolge R,G,B,A)
    int mTexW = 1024, mTexH = 0;            // mit Bibliothek kLibTexW breit
    // Raster (Texturpixel): Kopf, Reiterleiste, Zeile; linke Spalte bis kLibW.
    static constexpr int kTitleH = 84, kTabH = 72, kRowH = 64, kPadBottom = 20;
    static constexpr int kLibW = 560, kLibTexW = 1536;
    GLuint mProgram = 0, mVbo = 0, mTexture = 0;
    GLint mLocMvp = -1, mLocTex = -1;
};
