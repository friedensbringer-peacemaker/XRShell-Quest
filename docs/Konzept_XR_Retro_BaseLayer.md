# Konzept: XR Retro Base Layer — offene Plattform für Classic-Game-Ports in VR

## 1. Ziel

Eine Basis-Laufzeitumgebung („XRShell") für Android-VR-Headsets (Quest, Pico, Android XR),
mit der sich klassische PC-Spiele möglichst einfach in VR portieren lassen:

- **Plattform stellt:** OpenXR-Runtime-Schale, virtuelle Bildschirme, Controller→Maus/Tastatur-
  Mapping, Audio, Dateiverwaltung, Loader-UI.
- **Spieler stellt:** Das Originalspiel (CD, GOG, Steam) bzw. die Spieldateien selbst.
- **Ports bauen auf:** bestehenden Open-Source-Engine-Reimplementierungen auf
  (OpenTTD, OpenRCT2, CorsixTH, ScummVM, DOSBox …), nicht auf Neuimplementierung.

## 2. Legal-Konzept

### Prinzip: „BYOA" (Bring Your Own Assets)

Vorbilder, die dieses Modell seit Jahren praktizieren:

| Projekt | Modell |
| --- | --- |
| DOSBox | Verteilt nur die Emulationsplattform, keine Spiele |
| ScummVM | Engine offen, Spieldateien vom Nutzer; liest sogar Steam-/GOG-Versionen direkt |
| OpenTTD / OpenRCT2 | Engine offen, Original-Assets nötig (optional freie Ersatz-Assets: OpenGFX/OpenGraphics) |
| RetroArch / libretro | Cores offen, ROMs/Spiele ausschließlich vom Nutzer |

### Konkrete Regeln für das Projekt

1. **Niemals Spieldaten, BIOS, Keys oder Cracks bundeln oder mitliefern.**
2. **Niemals Kopierschutz umgehen** — Import erfolgt aus vom Nutzer legal erworbenen
   Originalquellen (CD, GOG, Steam-Installation, eigene Disketten-Images).
3. **Keine Original-Marken im eigenen Namen** — keine App „RCT VR" o. ä.; neutrale Namen
   („XRShell", Adapter heißen z. B. „Tycoon-Adapter", nicht „RollerCoaster Tycoon").
4. **Markenrecht bei Screenshots/Werbung** beachten.
5. Engine-Adapter erben die Lizenz der jeweiligen Engine (meist GPL). Das Projekt ist
   durchgehend Open Source → GPL-Konformität unproblematisch, solange Quellen offen bleiben.
6. Kommerzielle Distribution (Meta Store) ist damit machbar — alle Vorbilder oben sind
   teilweise offiziell vertrieben (ScummVM auf Google Play/Steam, RetroArch auf Steam).

### Besonders starke legale Optionen

- **Freie Ersatz-Assets nutzen, wo sie existieren:**
  - OpenTTD: **OpenGFX / OpenSFX / OpenMSX** (komplett frei spielbar ohne Original)
  - OpenRCT2: **OpenGraphics** (in Arbeit, teilweise nutzbar)
  - Widelands: komplett eigenständige freie Assets (GPL) — kein Original nötig
  - ScummVM: mehrere offiziell freigegebene Spiele (Beneath a Steel Sky, Flight of the Amazon Queen…)
- Dadurch kann die Plattform Demo-/Showcase-Inhalte vollständig legal ausliefern.

## 3. Engine-Inventar (Ziel-Spiele → vorhandene Open-Source-Basis)

### Wirtschaftssimulation / Tycoon
| Spiel | Engine | Lizenz | Bemerkung |
| --- | --- | --- | --- |
| Transport Tycoon Deluxe | **OpenTTD** | GPL | SDL2; freie Assets OpenGFX vorhanden |
| RollerCoaster Tycoon 1/2 | **OpenRCT2** | GPL | SDL2; benötigt Original-Assets |
| Theme Hospital | **CorsixTH** | MIT | SDL2; benötigt Original-Assets |
| Locomotion | **OpenLoco** | MIT | baut auf OpenRCT2-Code auf |
| Sim Theme Park / Theme Park World | **OpenTPW** | MIT | früher Stand — eher Langfristig |
| Theme Park (Bullfrog, 1994) | DOSBox-Pure (libretro) | GPL | DOS-Version direkt emulierbar |

### Aufbau / RTS
| Spiel | Engine | Lizenz | Bemerkung |
| --- | --- | --- | --- |
| Die Siedler 2 | **Return to the Roots (RttR)** | GPL | Engine-Recreation, nutzt Original-Assets |
| Siedler-ähnlich (frei) | **Widelands** | GPL | eigenständige Assets — komplett frei |
| Caesar III / Pharaoh | **Julius / Ozymandias** | AGPL | SDL2 |
| Command & Conquer / Red Alert | **OpenRA** | GPL |
| Age of Empires II | **openage** | GPL | noch im Bau |
| Dungeon Keeper | **KeeperFX** | GPL |

### Adventures (Maus-lastig → ideal für Raycast-Input)
| Spiel | Engine | Bemerkung |
| --- | --- | --- |
| LucasArts / Sierra / viele mehr | **ScummVM** | SDL2, GPL; VR-Fork **ScummVR** existiert bereits als Referenz (Quest, OpenXR) |

### God-Games (Hand-UI → VR-nativ, Kern-These des Projekts)

Diese Spiele wurden bereits mit einer „Gottes-Hand" als Mauszeiger entworfen —
in VR wird daraus statt einer Übersetzung eine **1:1-Naturerfüllung**:

| Spiel | Engine | Bemerkung |
| --- | --- | --- |
| Black & White (2001) | **openblack** | GPL v3, modernes C++/Vulkan/OpenGL, experimentelles Android-Targeting bereits vorhanden; Assets nur aus eigener Original-CD (B&W ist digital nirgends erhältlich — Rechte EA/Microsoft, Lionhead geschlossen). Die Zaubergesten (Symbole in die Luft zeichnen) sind per Handtracking in VR wörtlich umsetzbar |
| Dungeon Keeper 1 | **KeeperFX** | Open Source; Kernmechanik „Hand" (Kreaturen greifen, aufs Maul hauen, Gold greifen) ist VR-nativ |
| Dungeon Keeper 2 | **OpenKeeper** | Java/GPL, früher Stand |
| Populous (1/2) | DOSBox-Pure | Keine relevante Engine-Reimplementation — DOS-/Amiga-Version über Emulation, Hand bleibt 2D-Pointer auf Leinwand |
| Populous: The Beginning | keine | 3D-Original, kein offener Engine-Nachbau bekannt |
| Genre-Beweis | **Deisim** (kommerziell, Quest) | „Modern take on Populous/Black & White, designed with roomscale VR" — belegt, dass God-Game-Mechanik in VR funktioniert und ein Publikum hat |

**Bedeutung für die Roadmap:** God-Games sind die Kategorie, in der das Projekt
differenzierend ist. Stufe 1 = Hand-als-Raycast-Maus auf Leinwand (alle God-Games
sofort brauchbar). Stufe 2 = engine-spezifische VR-Adapter, die die Hand in die
3D-Welt des Spiels projizieren (openblack/KeeperFX), inkl. Gesten-Erkennung
(B&W-Zauber) und „Greifen"-Physik. Langfristig das eigentliche Herzstück.

### Fallback: DOS/Windows-Ära
| Weg | Komponente |
| --- | --- |
| DOS-Spiele allgemein | **DOSBox-Staging / DOSBox-X / DOSBox-Pure** als libretro-Core |
| Windows-95/98-Spiele | langfristig: Winlator-Prinzip (Wine unter Android), sehr experimentell |

### Der entscheidende Architektur-Hebel: SDL2

Fast alle genannten Engines (**OpenTTD, CorsixTH, OpenXcom, DevilutionX, ScummVM,
OpenLoco, Julius** …) basieren auf **SDL2**, und SDL2 hat einen offiziellen Android-Port
mit GLES-Backend. Ein einziger, gemeinsamer **SDL-Video/Input-Backend, der in eine
OpenXR-Textur rendert und Controller als Maus/Tastatur füttert**, erschließt damit
Dutzende Engines mit einem Adapter statt einem Port pro Spiel.

## 4. Architektur

```
┌────────────────────────────────────────────────┐
│ XRShell (Basis-Layer, MIT/Apache)               │
│  · OpenXR-Session + Stereo-Rendering            │
│  · Virtuelle Bildschirme (Curved/Flat Quads)    │
│  · Input-Abstraktion:                            │
│    Controller-Raycast → Maus                     │
│    Buttons → Tastatur/Maus-Events                │
│    Bluetooth-Tastatur/Maus, Touch, Handtracking  │
│  · Audio-Routing, Lautstärke pro Bildschirm      │
│  · Datei-Browser (Asset-Import vom Nutzer)       │
│  · Loader-UI + Plugin-Verwaltung                 │
├────────────────────────────────────────────────┤
│ Adapter (je klein, Open Source)                  │
│  A) SDL2→OpenXR-Shim      → CorsixTH, OpenTTD,  │
│                             ScummVM, DevilutionX…│
│  B) libretro-Core-Loader  → DOSBox-Pure (Theme   │
│                             Park, Siedler 2 DOS),│
│                             ScummVM, PrBoom …    │
│  C) Native Engine-Plugins → OpenRCT2, RttR, …    │
│  D) (später) Gamepad/3DOF-native VR-Adapter      │
└────────────────────────────────────────────────┘
```

### Technologie-Stack-Empfehlung

| Option | Pro | Contra |
| --- | --- | --- |
| **Native C++/OpenXR + SDL2** | maximale Kontrolle, SDL-Shim direkt, GPL-Engines nativ | meiste Arbeit |
| **Godot 4.6 (universelles OpenXR-APK)** | Universal-APK für alle Android-XR-Headsets, Meta fördert aktiv, schnelle UI | SDL-Shim läuft „außen", Bridging nötig |
| **StereoKit (C#, MIT)** | minimaler Overhead, sehr schnelle Iteration | C#-Interop zu C-Engines (SDL/libretro) aufwändiger |

**Empfehlung:** XRShell nativ in **C++/OpenXR mit SDL2** bauen (der SDL-Shim ist das Herz
des Projekts), Loader-UI in einer leichten Ebene (z. B. Dear ImGui oder Godot als
Frontend-Shell). Keine Unity/Unreal-Abhängigkeit — Lizenz und Overhead.

## 5. Meilensteine

| # | Meilenstein | Ergebnis | Aufwand |
| --- | --- | --- | --- |
| M0 | XRShell-Core: OpenXR-Session, Bildschirm-Quad, Raycast-Maus, ImGui-UI | Leere VR-Shell mit Mauszeiger | ~4–8 Wochen |
| M1 | Adapter B: libretro + DOSBox-Pure | **Theme Park (DOS) spielbar in VR** — erster PoC | ~2–4 Wochen auf M0 |
| M2 | SDL→OpenXR-Shim (Adapter A) | **CorsixTH / OpenTTD spielbar** (SDL2 nativ, kein Emulator) | ~4–8 Wochen |
| M3 | Asset-Import-Assistent (CD/GOG/Steam-Pfad, Validierung) + Plugin-API | „Installiere Theme Hospital"-Flow | ~4 Wochen |
| M4 | Komfort: Curved Screen, Multi-Screen, Savestate-Verwaltung, Bluetooth-KB | Daily-Driver-Qualität | fortlaufend |
| M5 | Engine-spezifische VR-Extras (z. B. RCT-Kamera in VR, First-Person-Klinik in CorsixTH) | differenzierendes Feature | Forschung |

**Proof-of-Concept-Scope:** M0 + M1 (DOSBox-Pure) — ein Headset, ein Spiel
(Bullfrog's Theme Park per DOS-Emulation), virtuelle Leinwand, Controller-Maus.
Bewusst simpel, aber der komplette Weg „APK bauen → Assets importieren → spielen" ist damit durchlaufen.

## 6. Distribution

1. **GitHub + SideQuest** (Sideload) — Startphase, wie ScummVR/Team Beef.
2. **Meta App Lab / Horizon Store** — sobald stabil; Emulatoren werden dort grundsätzlich
   akzeptiert, solange keine Assets enthalten sind.
3. Engine-Quellen und Adapter inkl. Build-Rezepte offenlegen (GPL-Konformität).

## 7. Risiken

- **GPL-Verbund:** Adapter an GPL-Engines dürfen nur als Open Source verteilt werden →
  Projekt von Anfang an komplett offen führen (kein „Closed Core").
- **Performance:** 2D-SDL-Engines sind auf Quest-SoC kein Problem; DOSBox-Dynarec auch nicht;
  PC-Emulation (Winlator-Pfad) wäre das eigentliche Limit — deshalb nicht in den PoC.
- **UX-Mauer:** Maus-Strategiespiele auf Raycast sind brauchbar, aber nicht magisch —
  die „Wow"-Phase kommt erst mit engine-spezifischen VR-Adaptern (M5).
- **Markenrecht:** strikte Trennung Plattform/Inhalte, siehe Abschnitt 2.

## 8. Backlog & Ausbau-Reihenfolge

Prinzip: **mit dem Einfachsten starten, Backlog nach Dimension/Tiefe staffeln.**
Jede Stufe baut auf der vorherigen auf — die Basis-Schicht (M0) wird nur einmal gebaut.

### Tier 1 — 2D- und Aufbau-Spiele (Start, geringste Hürde)

Rendering in Textur + Raycast-Maus reicht vollständig; keine 3D-Problematik.

| Spiel | Weg | Status Basis |
| --- | --- | --- |
| Theme Park (DOS, 1994) | DOSBox-Pure (libretro) | PoC (M1) |
| Die Siedler 2 | Return to the Roots (Android-lib) oder DOSBox | Engine GPL, SDL-lastig |
| Transport Tycoon Deluxe | OpenTTD (SDL-Shim) | ausgereift, freie Assets vorhanden |
| Theme Hospital | CorsixTH (SDL-Shim) | MIT-Lizenz, aktiv |
| Adventures (LucasArts, Sierra …) | ScummVM (SDL-Shim) | ScummVR als Referenz |
| Populous 1/2 | DOSBox-Pure | keine Engine-Reimpl. vorhanden |

### Tier 2 — 2.5D/isometrisch-3D (mittelfristig)

3D-Welten, aber Interaktion bleibt „Hand über Karte" → Leinwand-Ansatz weiter brauchbar,
erste VR-Extras (perspektivische Leinwand, Zoom-an-den-Kopf) möglich.

| Spiel | Weg |
| --- | --- |
| RollerCoaster Tycoon 1/2 | OpenRCT2 (SDL-Shim; native 3D-Kamera-Integration später) |
| Dungeon Keeper 1 | KeeperFX |
| Caesar III / Pharaoh | Julius / Ozymandias |
| Diablo 1 | DevilutionX (SDL-Shim) — Lizenz nur nicht-kommerziell, siehe Kandidaten: Diablo |
| Black & White (Stufe 1) | openblack auf Leinwand (experimentelles Android-Target existiert) |

### Tier 3 — echte 3D-Welten (langfristig, Kernmotivation)

Stereoscopic-Rendering, 6DOF-Kamera, Controller-Hand in der Welt statt Maus.
Referenzprojekt: **OpenMW-VR** (OpenXR-Fork von Morrowind) existiert für PCVR,
seit Feb 2026 existiert mit openmw-vr-quest auch ein funktionierender Quest-Port —
der Beweis, dass offene 3D-Engines nativ auf der Quest laufen.

| Spiel | Weg | Besonderheit |
| --- | --- | --- |
| Black & White | openblack + VR-Adapter | Handtracking-Gesten für Zauber; das Aushängeschild |
| Morrowind | OpenMW-VR / openmw-vr-quest | nahtlos, Referenzcode vorhanden |
| Populous: The Beginning | kein offener Nachbau | nur via PC-Streaming oder langfristig Reverse Engineering |
| Jedi Knight / Quake / Doom | Team-Beef-Ports (SideQuest) | bereits erledigt — nicht neu bauen, ggf. integrieren |

### Kandidaten: Command & Conquer (Stand 2026-09-27)

Eigene Lizenzen vorhanden (EA-App „Ultimate Collection“, zusätzlich Steam/Origin).
Grundlage: EA hat im Feb. 2025 die Quellen von Tiberian Dawn, Red Alert, Renegade und
Generals/Zero Hour unter GPLv3 veröffentlicht (mit Zusatzbedingungen, keine Markenrechte).
Für die SAGE-2-Titel gibt es weder Quellen noch einen Nachbau.

| Spiel | Offene Basis | Machbarkeit | Einordnung |
| --- | --- | --- | --- |
| Generals + Zero Hour | EA-GPL-Quellen → Generals-Zero-Hour-XR (Cesarus85, Quest-3-Tabletop) | **existiert bereits** | nicht neu bauen — nutzen, ggf. beitragen |
| C&C (Tiberian Dawn) + Covert Ops | OpenRA `cnc`, Vanilla Conquer, EA-GPL | hoch | Tier 1 via XR-OpenRA (Quest-Probe vorhanden) |
| Red Alert + Counterstrike/Aftermath | OpenRA `ra`, Vanilla Conquer, EA-GPL | hoch | Tier 1 via XR-OpenRA |
| Tiberian Sun + Firestorm | OpenRA-TS-Mod (erstes Release 2026, außerhalb des Installers) | mittel | Tier 2 nach OpenRA-RA/TD; setzt auf dem OpenRA-Adapter auf |
| Red Alert 2 + Yuri's Revenge | nur inoffizielles, unvollständiges OpenRA-RA2-Projekt; keine Quellen | niedrig | beobachten (hängt an OpenRA-TS-Reife) |
| Renegade | EA-GPL-Quellen (W3D-Engine, Vorfahre der Generals-Engine) | mittel, großer Aufwand | Tier 3 — Ego-Shooter, VR-nativ; DX8→GLES-Weg wie bei Generals XR |
| C&C 3 + Kane's Wrath, Alarmstufe Rot 3 + Aufstand, C&C 4 | SAGE 2 — keine Quellen, kein Nachbau | derzeit nicht | nur PC-Streaming |

### Kandidaten: Diablo (Stand 2026-09-27)

Grundlage für Teil 1 ist **DevilutionX** (SDL2, offizieller Android-Build, Hellfire,
Breitbild, Gamepad; sehr aktiv). Spieldaten vom Nutzer: `DIABDAT.MPQ` (GOG/CD),
für Hellfire zusätzlich `hellfire.mpq` u. a. — passt zu BYOA.

Zwei Legal-Punkte, die **vor** einem Adapter entschieden werden müssen:

1. DevilutionX ist keine Clean-Room-Reimplementierung, sondern aus dem dekompilierten
   Original rekonstruiert (Devilution). Blizzard duldet das seit Jahren, GOG verweist
   darauf — rechtlich trotzdem eine andere Kategorie als OpenTTD/CorsixTH.
2. Lizenz ist die **Sustainable Use License 1.0** (kein GPL): Nutzung und Weitergabe nur
   kostenlos und nicht-kommerziell. Ein Diablo-Adapter schließt damit Regel 6
   (kommerzielle Distribution) für dieses Paket aus → nur als separates, kostenloses
   Sideload-/SideQuest-Paket, nie im Store-Bundle.

| Spiel | Offene Basis | Machbarkeit | Einordnung |
| --- | --- | --- | --- |
| Diablo + Hellfire | DevilutionX (SDL2, Android-Build vorhanden) | hoch | Tier 2 via SDL2-Shim (#6); Bedienung ideal für Laser-Maus (Klick-lastig) |
| Diablo II + Lord of Destruction | keine spielbare offene Engine (OpenDiablo2 eingestellt, Abyss Engine unvollständig) | derzeit nicht | nur x86-Emulation (Wine + Box64, z. B. Winlator) — sprengt die Architektur; beobachten |
| Diablo II: Resurrected, III, IV | proprietär, keine Quellen | nein | nur PC-Streaming |

### Reihenfolge-Regel

Nie zwei Tiers gleichzeitig öffnen. Tier 1 → stabile Basis + Distribution →
erst dann Tier 2, Tier 3 parallel als Forschungs-Track (openblack-Reife beobachten).
Der Backlog lebt im GitHub-Projektboard, ein Eintrag pro Spiel/Adapter.
