<!-- Entwurf für ein GitHub-Issue (noch nicht angelegt). Anlegen z. B. mit:
     gh issue create --title "xr.dosgames: DOS-Spielesammlung mit Spielauswahl" --body-file docs/issue-xr-dos.md -->

## Ziel

Eigene DOS-Spiele (GOG-Käufe, BYOA) in **einer** Quest-App `xr.dosgames` spielbar machen:
DOSBox-Pure, Spielauswahl im VR-Menü, Profile (Tasten, Kern-Optionen, Menü-Optik) pro Spiel.

## Umfang

- Flavor `dos` → App-ID/Label `xr.dosgames`, nur DOSBox-Pure, Bibliotheksmodus (`XRSHELL_LIBRARY` +
  `XRSHELL_LIBRARY_DOS`)
- Bibliothek kennt jetzt **Ordner-Spiele**: `files/games/dos/<Ordner>/` = Laufwerk C:, Titel aus
  `xrshell.title`, nur Systeme, deren Core im APK liegt
- Tastenprofil + Menü-Optik werden beim Spielwechsel neu geladen; laufendes Spiel in der Auswahl
  = weiterspielen; kein Import in `xr.dosgames`
- `tools/prepare-dos-games.ps1` (AUTOBOOT.DBP, CD-Image auf C:, SB16-Setup, Sprache, Soundfont)
- `tools/push-game.sh`: `APP_ID=xr.dosgames` → `files/games/dos/<Name>/`
- Profile: Pirates! Gold, Pirates! (1987), Populous, Populous II, SimCity 2000, Syndicate Wars
- Doku: `docs/DOS-SPIELE.md`

## Abnahme (Headset)

Prüfliste in `docs/DOS-SPIELE.md`, Abschnitt „Im Headset zu prüfen“ (Start ohne Spiel → Auswahl,
alle sechs Titel starten mit Bild/Ton/Maus, CD als D:, Deutsch bei Pirates! Gold und Syndicate Wars,
Spielwechsel mit Profilwechsel, Weiterspielen, Neustart setzt fort).

## Nicht enthalten

Populous: The Beginning (Windows/Direct3D). MT-32-ROMs (Musik läuft über GeneralUser-GS-Soundfont).
