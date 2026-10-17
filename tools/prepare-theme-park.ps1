# Bereitet Theme Park (Bullfrog 1994, DOS/GOG) für XRShell vor — BYOA: alles kommt aus der
# eigenen GOG-Installation des Spielers und landet in einem Ordner außerhalb des Repos.
#
#   powershell -File tools\prepare-theme-park.ps1 -Source <GOG-Ordner> [-Out <Zielordner>] [-Language 1]
#   bash tools/push-game.sh <Zielordner> 1-ThemePark
#
# Der GOG-Installer (setup_theme_park_*.exe) wird vorher mit innoextract entpackt; -Source
# ist der Ordner mit GAME\, THEME.CD\ und CONFIG\. Ergebnis im Zielordner (= Laufwerk C:):
#   GAME\ THEME.CD\ CONFIG\ SETUP.BAT   Spieldaten wie bei GOG
#   THEME.BAT                           Start: Intro + Spiel in der gewählten Sprache
#   THEME.CD\SNDSETUP.INF               Soundblaster 16 + FM-Musik (kein General MIDI nötig)
#   xrshell.keys / xrshell.opt          aus profiles\theme-park.*
#   XRTHEME\*.BMP + xrshell.theme       VR-Menü in Spieloptik (Rasen, Knöpfe, Spielschrift)
# Sprachen (-Language): 0 Englisch, 1 Deutsch, 2 Italienisch (laut GOG-Installer).
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$Out = '',   # Standard: ..\original-data\quest\1-ThemePark neben dem Repo
    [int]$Language = 1
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $Out) { $Out = Join-Path (Split-Path $repo -Parent) 'original-data\quest\1-ThemePark' }
foreach ($p in @("$Source\GAME\MAIN.EXE", "$Source\GAME\DATA\MPALETTE.DAT", "$Source\THEME.CD")) {
    if (-not (Test-Path $p)) { throw "Fehlt: $p - ist -Source der entpackte GOG-Ordner?" }
}
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path

foreach ($d in @('GAME', 'THEME.CD', 'CONFIG')) { Copy-Item (Join-Path $Source $d) $Out -Recurse }
if (Test-Path "$Source\SETUP.BAT") { Copy-Item "$Source\SETUP.BAT" $Out }

$ascii = [Text.Encoding]::ASCII
$crlf = "`r`n"
[IO.File]::WriteAllText("$Out\THEME.BAT",
    "@C:${crlf}@cd C:\game${crlf}:START${crlf}@intro -C:/THEME.CD${crlf}" +
    "@main -cC:/game/ -dC:/THEME.CD/ -l$Language${crlf}@IF ERRORLEVEL 10 GOTO START${crlf}", $ascii)
[IO.File]::WriteAllText("$Out\THEME.CD\SNDSETUP.INF",
    "SOUNDFX = SB16 220 7 1${crlf}MUSIC = SB16FM 388${crlf}", $ascii)
Copy-Item "$repo\profiles\theme-park.keys" "$Out\xrshell.keys"
Copy-Item "$repo\profiles\theme-park.opt" "$Out\xrshell.opt"

# --- VR-Menü in Spieloptik: Bilder aus den Spieldaten nach XRTHEME\ ---------------------
Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.IO;

public static class ThemeParkTheme {
    const uint Key = 0xFF00FF;   // Schlüsselfarbe → im Menü durchsichtig
    static uint[] pal = new uint[256];

    public static void Build(string data, string dir) {
        byte[] p = File.ReadAllBytes(Path.Combine(data, "MPALETTE.DAT"));
        for (int i = 0; i < 256; i++) {
            uint r = (uint)(p[i * 3] & 63), g = (uint)(p[i * 3 + 1] & 63), b = (uint)(p[i * 3 + 2] & 63);
            pal[i] = ((r << 2 | r >> 4) << 16) | ((g << 2 | g >> 4) << 8) | (b << 2 | b >> 4);
        }
        byte[] menu = File.ReadAllBytes(Path.Combine(data, "MMENU-0.DAT"));
        // Rasenstreifen links im Titelbild (unter den Blumen, ohne Logo), gespiegelt gekachelt.
        Grass(menu, Path.Combine(dir, "PANEL.BMP"), 512, 376, 0.65);
        Grass(menu, Path.Combine(dir, "TITLE.BMP"), 512, 42, 0.43);
        Strip(Path.Combine(dir, "STRIP.BMP"));
        Button(Path.Combine(dir, "BUTTON.BMP"));
        Arrow(Path.Combine(dir, "LEFT.BMP"), true, false);
        Arrow(Path.Combine(dir, "LEFTPR.BMP"), true, true);
        Arrow(Path.Combine(dir, "RIGHT.BMP"), false, false);
        Arrow(Path.Combine(dir, "RIGHTPR.BMP"), false, true);
        Font(data, Path.Combine(dir, "FONT.BMP"));
    }

    static void Grass(byte[] menu, string file, int w, int h, double mul) {
        uint[] px = new uint[w * h];
        for (int y = 0; y < h; y++) {
            int ty = (y / 120) % 2 == 1 ? 119 - y % 120 : y % 120;
            for (int x = 0; x < w; x++) {
                int tx = (x / 52) % 2 == 1 ? 51 - x % 52 : x % 52;
                px[y * w + x] = Scale(pal[menu[(70 + ty) * 320 + 2 + tx]], mul);
            }
        }
        Bmp(file, w, h, px);
    }

    static void Strip(string file) {
        uint[] px = new uint[64 * 6];
        for (int y = 0; y < 6; y++)
            for (int x = 0; x < 64; x++) px[y * 64 + x] = y < 4 ? 0xFFD228u : 0xA07814u;
        Bmp(file, 64, 6, px);
    }

    // Knopf 24×16, dreiteilig gestreckt (8 px Kappen): dunkles Grün, gelber Rand, runde Ecken.
    static void Button(string file) {
        const int w = 24, h = 16;
        uint[] px = new uint[w * h];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int cx = x < 4 ? 4 : (x > w - 5 ? w - 5 : x), cy = y < 4 ? 4 : (y > h - 5 ? h - 5 : y);
                double d = Math.Sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
                px[y * w + x] = d > 4.2 ? Key : (d > 3.0 || y == 0 || y == h - 1) ? 0xFFD228u : 0x0C2A08u;
            }
        Bmp(file, w, h, px);
    }

    // Pfeilknopf 24×24: blauer Kreis mit gelbem Dreieck, gedrückt umgekehrt.
    static void Arrow(string file, bool left, bool pressed) {
        const int s = 24;
        uint[] px = new uint[s * s];
        uint disc = pressed ? 0xFFD228u : 0x2860AAu, tri = pressed ? 0x10131Au : 0xFFD228u;
        for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++) {
                double dx = x - 11.5, dy = y - 11.5;
                if (dx * dx + dy * dy > 11.5 * 11.5) { px[y * s + x] = Key; continue; }
                int ax = left ? x : s - 1 - x;            // Spitze links, Grundseite rechts
                int half = ax - 7;                          // Breite wächst nach rechts
                bool inTri = ax >= 7 && ax <= 15 && Math.Abs(y - 11.5) <= half * 0.8 + 0.5;
                px[y * s + x] = inTri ? tri : disc;
            }
        Bmp(file, s, s, px);
    }

    // Schrift: Zellen 18×17, 16 je Zeile. Große blaue Schrift (MFONT-0 56–104), dazu
    // % . ( ) / aus der kleinen Schrift (dreifach). Reihenfolge = font_chars in xrshell.theme.
    // Zeichencodes statt Umlauten: Windows PowerShell 5.1 liest das Skript ohne BOM als ANSI.
    // Ue Ae Oe sz, U O I E A mit Akut, N mit Tilde; danach das Malzeichen (wie X).
    public static readonly string Umlauts = new string(new char[] {
        (char)0xDC, (char)0xC4, (char)0xD6, (char)0xDF, (char)0xDA,
        (char)0xD3, (char)0xCD, (char)0xC9, (char)0xC1, (char)0xD1 });
    public static readonly string Chars =
        ",-0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ" + Umlauts + "%.()/" + (char)0xD7;
    static void Font(string data, string file) {
        byte[] tab = File.ReadAllBytes(Path.Combine(data, "MFONT-0.TAB"));
        byte[] dat = File.ReadAllBytes(Path.Combine(data, "MFONT-0.DAT"));
        int[] index = { 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68 };
        const int cw = 18, ch = 17, cols = 16;
        int count = Chars.Length, rows = (count + cols - 1) / cols;
        uint[] px = new uint[cw * cols * ch * rows];
        for (int i = 0; i < px.Length; i++) px[i] = Key;
        for (int c = 0; c < count; c++) {
            char k = Chars[c];
            int sprite, scale = 1;
            bool center = k == '-' || k == ':' || k == '(' || k == ')' || k == '/';
            if (c < 13) sprite = index[c];
            else if (k >= 'A' && k <= 'Z') sprite = 69 + (k - 'A');
            else if (k == (char)0xD7) sprite = 69 + ('X' - 'A');   // Malzeichen wie X
            else {
                int u = Umlauts.IndexOf(k);
                if (u >= 0) sprite = 95 + u;
                else { sprite = new[] { 1, 41, 43, 44, 45 }["%.()/".IndexOf(k)]; scale = 3; }
            }
            int off = BitConverter.ToInt32(tab, sprite * 6), w = tab[sprite * 6 + 4], h = tab[sprite * 6 + 5];
            int oy = center ? (ch - h * scale) / 2 : ch - h * scale;
            int ox = (c % cols) * cw, cy = (c / cols) * ch;
            int pos = off;
            for (int y = 0; y < h; y++) {
                int x = 0;
                while (pos < dat.Length) {
                    int run = (sbyte)dat[pos++];
                    if (run == 0) break;
                    if (run < 0) { x -= run; continue; }
                    for (int j = 0; j < run; j++, x++, pos++)
                        for (int sy = 0; sy < scale; sy++)
                            for (int sx = 0; sx < scale; sx++) {
                                int X = x * scale + sx, Y = oy + y * scale + sy;
                                if (X < cw && Y >= 0 && Y < ch)
                                    px[(cy + Y) * cw * cols + ox + X] = pal[dat[pos]];
                            }
                }
            }
        }
        Bmp(file, cw * cols, ch * rows, px);
    }

    static uint Scale(uint c, double m) {
        return ((uint)(((c >> 16) & 255) * m) << 16) | ((uint)(((c >> 8) & 255) * m) << 8) | (uint)((c & 255) * m);
    }

    static void Bmp(string file, int w, int h, uint[] px) {
        int row = (w * 3 + 3) & ~3;
        using (var bw = new BinaryWriter(File.Create(file))) {
            bw.Write((ushort)0x4D42); bw.Write(54 + row * h); bw.Write(0); bw.Write(54);
            bw.Write(40); bw.Write(w); bw.Write(h); bw.Write((ushort)1); bw.Write((ushort)24);
            bw.Write(0); bw.Write(row * h); bw.Write(2835); bw.Write(2835); bw.Write(0); bw.Write(0);
            for (int y = h - 1; y >= 0; y--) {
                for (int x = 0; x < w; x++) {
                    uint c = px[y * w + x];
                    bw.Write((byte)c); bw.Write((byte)(c >> 8)); bw.Write((byte)(c >> 16));
                }
                for (int k = w * 3; k < row; k++) bw.Write((byte)0);
            }
        }
    }
}
'@

$themeDir = Join-Path $Out 'XRTHEME'
New-Item -ItemType Directory -Force $themeDir | Out-Null
[ThemeParkTheme]::Build((Join-Path $Source 'GAME\DATA'), $themeDir)

$theme = @"
# VR-Menue im Stil von Theme Park (Bilder und Schrift: eigene GOG-Daten, Ordner XRTHEME)
panel = XRTHEME/PANEL.BMP
title = XRTHEME/TITLE.BMP
strip = XRTHEME/STRIP.BMP
button = XRTHEME/BUTTON.BMP
arrow_left = XRTHEME/LEFT.BMP
arrow_left_pressed = XRTHEME/LEFTPR.BMP
arrow_right = XRTHEME/RIGHT.BMP
arrow_right_pressed = XRTHEME/RIGHTPR.BMP
font = XRTHEME/FONT.BMP
font_cell = 18x17
font_chars = $([ThemeParkTheme]::Chars)
font_upper = 1
text = #FFFFFF
text_dim = #8C968C
title_text = #FFD228
accent = #FFD228A0
hover = #FFEE9680
transparent = #FF00FF
"@
[IO.File]::WriteAllText("$Out\xrshell.theme", $theme.Replace("`r`n", "`n"), (New-Object Text.UTF8Encoding($false)))

Write-Host "Fertig: $Out"
Write-Host "Auf die Quest: bash tools/push-game.sh `"$Out`" 1-ThemePark"
