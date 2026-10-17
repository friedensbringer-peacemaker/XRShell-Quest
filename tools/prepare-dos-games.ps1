# Bereitet DOS-Spiele aus eigenen GOG-Kaeufen fuer xr.dosgames (XRShell, DOSBox-Pure) vor.
# BYOA: alles kommt aus der entpackten GOG-Installation des Spielers und landet in einem
# Ordner ausserhalb des Repos. Der Ordner ist Laufwerk C:; ein CD-Image liegt auf C: und
# wird von DOSBox-Pure als D: eingehaengt, weil AUTOBOOT.DBP den Start uebernimmt.
#
#   innoextract -e -d <entpackt>\<id> setup_<spiel>.exe          (Syndicate Wars: --language=german)
#   powershell -File tools\prepare-dos-games.ps1 -Source <entpackt>\<id> -Game <id> [-Out <Ziel>]
#   powershell -File tools\prepare-dos-games.ps1 -Source <entpackt> -Game all
#   APP_ID=xr.dosgames bash tools/push-game.sh <Ziel>/<Ordner> <Ordner>   (-> files/games/dos/<Ordner>/)
#   powershell -File tools\prepare-dos-games.ps1 -Game all -ProfilesOnly
#       nur xrshell.keys/.opt/.title in schon gebauten Spielordnern erneuern (ohne GOG-Quelle),
#       dazu die Spiel-Konfiguration nachziehen (SimCity 2000: SC2000.CFG Ton + Aufloesung)
#
# Spiele (-Game):            Ordner auf der Quest (games/dos/<Ordner>/)
#   pirates-gold    Pirates! Gold (1993, CD)          PiratesGold
#   pirates         Pirates! (1987, EGA)              Pirates
#   populous        Populous + Promised Lands (1989)  Populous
#   populous2       Populous II (1991)                Populous2
#   simcity2000     SimCity 2000 Special Edition      SimCity2000
#   syndicate-wars  Syndicate Wars (1996, CD)         SyndicateWars
#   worms           Worms United (Steam 70640, CD)    WormsUnited  (-Source = Steam-Ordner "Worms";
#                   bei -Game all: Unterordner worms)
#   Shooter (Pad-Modus, vorbereitet - ungeprueft gegen echte Installer; -Game all: Unterordner
#   doom, doom2, final-doom von -Source). Gesucht wird rekursiv (GOG/Steam legen die DOS-
#   Fassung unterschiedlich tief ab), "rerelease"-Ordner (KEX 2024) werden uebergangen:
#   doom            The Ultimate Doom (DOOM.EXE+.WAD) Doom
#   doom2           Doom II (DOOM2.EXE + DOOM2.WAD)   Doom2
#   doom-tnt        Final Doom: TNT (DOOM2.EXE+TNT)   FinalDoomTNT
#   doom-plutonia   Final Doom: Plutonia              FinalDoomPlutonia
#   quake           Quake 1.06 (QUAKE.EXE, id1)       Quake  (Steam/GOG; Musik als CD-Image aus
#                   rerelease\id1\music\*.ogg, falls vorhanden)
#   Ohne DOS-EXE (nur WAD, z. B. Unity-/BFG-Fassung) bricht das Skript ab: Weg 2 = App xr.shooter.
#
# Ergebnis je Spiel: Spieldateien, XRSTART.BAT (Start), AUTOBOOT.DBP (eine Zeile, CRLF),
# xrshell.title (Name in der Spielauswahl), xrshell.keys / xrshell.opt aus profiles\,
# DOS.YML (Gamepad-Belegung fuer DOSBox Pure) aus profiles\<profil>.yml, falls vorhanden,
# bei MIDI-Musik DOSBOX.SF2 (GeneralUser GS). Belegung und Befunde: docs\DOS-SPIELE.md.
# Hinweis: Skript bleibt ASCII - Windows PowerShell 5.1 liest Dateien ohne BOM als ANSI.
param(
    [string]$Source = '',      # entpackter GOG-Ordner (bei -ProfilesOnly nicht noetig)
    [Parameter(Mandatory = $true)]
    [ValidateSet('pirates-gold', 'pirates', 'populous', 'populous2', 'simcity2000', 'syndicate-wars', 'worms',
                 'doom', 'doom2', 'doom-tnt', 'doom-plutonia', 'quake', 'all')]
    [string]$Game,
    [string]$Out = '',         # Standard: ..\original-data\quest\dos\<Ordner> neben dem Repo
    [string]$SoundFont = '',   # General-MIDI-Soundfont fuer MT-32/MIDI-Musik
    [ValidateSet('640x480', '800x600', '1024x768')]
    [string]$SimCityResolution = '800x600',   # SimCity 2000: VESA-Modus in SC2000.CFG (256 Farben)
    [switch]$ProfilesOnly      # nur Profile (keys/opt/title/DOS.YML) in vorhandene Ordner kopieren
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$platform = Split-Path $repo -Parent
if (-not $SoundFont) {
    $SoundFont = Join-Path (Split-Path $platform -Parent) 'XR-HolidayIsland\original-data\soundfont\GeneralUser-GS.sf2'
}

$games = [ordered]@{
    'pirates-gold'   = @{ Folder = 'PiratesGold';   Title = 'Pirates! Gold';          Profile = 'pirates-gold' }
    'pirates'        = @{ Folder = 'Pirates';       Title = 'Pirates! (1987)';        Profile = 'pirates' }
    'populous'       = @{ Folder = 'Populous';      Title = 'Populous';               Profile = 'populous' }
    'populous2'      = @{ Folder = 'Populous2';     Title = 'Populous II';            Profile = 'populous-2' }
    'simcity2000'    = @{ Folder = 'SimCity2000';   Title = 'SimCity 2000';           Profile = 'simcity-2000' }
    'syndicate-wars' = @{ Folder = 'SyndicateWars'; Title = 'Syndicate Wars';         Profile = 'syndicate-wars' }
    'worms'          = @{ Folder = 'WormsUnited';   Title = 'Worms United';           Profile = 'worms' }
    # Shooter: ein Profil fuer alle vier (Doom-Engine 1.9); Src = Unterordner bei -Game all.
    'doom'           = @{ Folder = 'Doom';          Title = 'The Ultimate Doom';      Profile = 'doom'; Src = 'doom' }
    'doom2'          = @{ Folder = 'Doom2';         Title = 'Doom II';                Profile = 'doom'; Src = 'doom2' }
    'doom-tnt'       = @{ Folder = 'FinalDoomTNT';  Title = 'Final Doom: TNT Evilution'; Profile = 'doom'; Src = 'final-doom' }
    'doom-plutonia'  = @{ Folder = 'FinalDoomPlutonia'; Title = 'Final Doom: Plutonia'; Profile = 'doom'; Src = 'final-doom' }
    'quake'          = @{ Folder = 'Quake';         Title = 'Quake';                  Profile = 'quake'; Src = 'quake' }
}

# Bekannte Groessen der DOS-1.9-IWADs (Bytes). Andere Groessen = Warnung, kein Abbruch
# (KEX-"rerelease"-WADs sind erweitert und gehoeren zu xr.shooter, nicht zu DOOM.EXE).
$doomWads = @{
    'DOOM.WAD'     = @(12408292, 11159840)    # Ultimate Doom 1.9 / Doom (registriert) 1.9
    'DOOM2.WAD'    = @(14604584)              # Doom II 1.9
    'TNT.WAD'      = @(18195736, 18654796)    # Final Doom / id-Anthology-Fix
    'PLUTONIA.WAD' = @(17420824, 18240172)    # Final Doom / id-Anthology-Fix
}

$ascii = [Text.Encoding]::ASCII
function Write-Dos([string]$path, [string[]]$lines) {
    # DOS-Textdatei: ASCII, CRLF, abschliessender Zeilenumbruch.
    [IO.File]::WriteAllText($path, (($lines -join "`r`n") + "`r`n"), $ascii)
}
function Need([string]$path) {
    if (-not (Test-Path $path)) { throw "Fehlt: $path - ist -Source der entpackte GOG-Ordner des Spiels?" }
}
function Copy-Into([string]$from, [string]$to) {
    Copy-Item $from $to -Recurse -Force
}
function Find-GameFile([string]$root, [string]$name, [string]$near = '') {
    # Datei rekursiv suchen (GOG/Steam legen die DOS-Fassung verschieden tief ab, z. B. base\,
    # finaldoombase\, TNT\). KEX-Ordner "rerelease" auslassen. Bevorzugt im Ordner $near.
    $hits = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\rerelease(\\|$)' })
    if ($near) { $same = @($hits | Where-Object { $_.DirectoryName -eq $near }); if ($same.Count) { return $same[0] } }
    if ($hits.Count) { return $hits[0] }
    return $null
}
function Write-DoomCfg([string]$path) {
    # DEFAULT.CFG der DOS-Fassung 1.9 (Doom/Doom II/Final Doom lesen sie aus dem Startordner).
    # Tasten = Doom-Standard in Doom-Tastencodes (Pfeile 172-175, Strg 157, Shift 182, Alt 184),
    # dazu passt profiles\doom.yml. Maus und Joystick aus (Pad-Modus schickt Tasten).
    # Ton: Effekte und Musik Sound Blaster (3; Musik = OPL-FM), 220h (544)/IRQ 7/DMA 1.
    $cfg = @(
        'mouse_sensitivity 5', 'sfx_volume 8', 'music_volume 8', 'show_messages 1',
        'key_right 174', 'key_left 172', 'key_up 173', 'key_down 175',
        'key_strafeleft 44', 'key_straferight 46', 'key_fire 157', 'key_use 32',
        'key_strafe 184', 'key_speed 182',
        'use_mouse 0', 'mouseb_fire 0', 'mouseb_strafe 1', 'mouseb_forward 2',
        'use_joystick 0', 'joyb_fire 0', 'joyb_strafe 1', 'joyb_use 3', 'joyb_speed 2',
        'screenblocks 10', 'detaillevel 0', 'snd_channels 8',
        'snd_musicdevice 3', 'snd_sfxdevice 3', 'snd_sbport 544', 'snd_sbirq 7', 'snd_sbdma 1',
        'snd_mport 816', 'usegamma 0')
    foreach ($i in 0..9) { $cfg += ('chatmacro{0} "no macro"' -f $i) }
    Write-Dos $path $cfg
}
function Prepare-Doom([string]$src, [string]$dst, [string]$wadName, [string[]]$exeNames) {
    # Doom-Engine 1.9 (DOS/4GW eingebunden): nur EXE + IWAD ins Spiel, DEFAULT.CFG neu.
    # Pro Ordner genau ein IWAD - DOOM2.EXE nimmt sonst DOOM2.WAD vor TNT/PLUTONIA.
    # Mehrere EXE-Namen: der erste, der neben dem IWAD liegt, gewinnt (Steam "DOOM + DOOM II":
    # base\tnt\final.exe, base\plutonia\final.exe - die Final-Doom-EXE), sonst der erste Fund.
    $wad = Find-GameFile $src $wadName
    if (-not $wad) { throw "Fehlt: $wadName unter $src - ist -Source der entpackte GOG-/Steam-Ordner?" }
    $exe = $null
    foreach ($n in $exeNames) {
        $hit = Find-GameFile $src $n $wad.DirectoryName
        if ($hit -and $hit.DirectoryName -eq $wad.DirectoryName) { $exe = $hit; break }
        if ($hit -and -not $exe) { $exe = $hit }
    }
    $exeName = if ($exe) { $exe.Name.ToUpper() } else { $exeNames -join '/' }
    if (-not $exe) {
        throw ("Keine DOS-Fassung: $exeName fehlt unter $src (nur $wadName gefunden). Diese Kaufversion " +
               "liefert nur die Daten - Weg 2: App xr.shooter (PrBoom) mit $wadName.")
    }
    if ($exe.DirectoryName -ne $wad.DirectoryName) {
        Write-Warning "$exeName liegt nicht neben $wadName ($($exe.FullName)) - Kombination ungeprueft."
    }
    if ($doomWads[$wadName] -notcontains $wad.Length) {
        Write-Warning ("{0}: {1:N0} Bytes - keine bekannte DOS-1.9-Groesse (KEX-/BFG-WAD?)." -f $wad.FullName, $wad.Length)
    }
    Copy-Item -LiteralPath $exe.FullName "$dst\$exeName"
    Copy-Item -LiteralPath $wad.FullName "$dst\$wadName"
    Write-DoomCfg "$dst\DEFAULT.CFG"
    Write-Dos "$dst\XRSTART.BAT" @('@echo off', 'C:', 'cd \', $exeName)
}

function Write-Iso([string]$path, [string]$volume, [string]$readme) {
    # Kleines ISO9660-Image (Datenspur der Quake-CD) ueber Windows-IMAPI2 - keine Zusatzwerkzeuge.
    if (-not ('XrIsoWriter' -as [type])) {
        Add-Type -TypeDefinition @'
using System; using System.IO; using System.Runtime.InteropServices; using System.Runtime.InteropServices.ComTypes;
public static class XrIsoWriter {
    public static void Save(object stream, string path) {
        IStream st = (IStream)stream;
        using (FileStream fs = File.Create(path)) {
            byte[] buf = new byte[65536]; IntPtr pn = Marshal.AllocHGlobal(4);
            try { while (true) { st.Read(buf, buf.Length, pn); int n = Marshal.ReadInt32(pn); if (n <= 0) break; fs.Write(buf, 0, n); } }
            finally { Marshal.FreeHGlobal(pn); }
        }
    }
}
'@
    }
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("xriso-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory $tmp | Out-Null
    try {
        [IO.File]::WriteAllText("$tmp\README.TXT", $readme + "`r`n", $ascii)
        $fsi = New-Object -ComObject IMAPI2FS.MsftFileSystemImage
        $fsi.FileSystemsToCreate = 1   # ISO9660
        $fsi.VolumeName = $volume
        $fsi.Root.AddTree($tmp, $false)
        [XrIsoWriter]::Save($fsi.CreateResultImage().ImageStream, $path)
    } finally { Remove-Item $tmp -Recurse -Force }
}

function Copy-Profiles([string]$id, [string]$dst) {
    $g = $games[$id]
    [IO.File]::WriteAllText("$dst\xrshell.title", $g.Title + "`n", $ascii)
    foreach ($ext in 'keys', 'opt') {
        $p = Join-Path $repo "profiles\$($g.Profile).$ext"
        if (Test-Path $p) { Copy-Item $p "$dst\xrshell.$ext" }
    }
    # Gamepad-Belegung fuer DOSBox Pure (Pad-Modus): C:\DOS.YML, input_pad_*/input_wheel_*.
    $yml = Join-Path $repo "profiles\$($g.Profile).yml"
    if (Test-Path $yml) { Copy-Item $yml "$dst\DOS.YML" }
}

# SimCity 2000: VIDEOCARD-Codes der VESA-Treibertabelle in SC2K.EXE (256 Farben). GOG-Vorgabe ist
# ve:31; ve:2x waeren 16 Farben, ve:53/55 weitere 1024x768-Varianten. Mehr als 1024x768 nimmt
# XRShell nicht an (Texturgrenze), deshalb keine groessere Wahl. Befund: docs\DOS-SPIELE.md.
$simCityVideo = @{ '640x480' = 've:31'; '800x600' = 've:32'; '1024x768' = 've:33' }

function Patch-GameConfig([string]$id, [string]$dst) {
    # Konfigurationsdateien des Spiels im Spielordner anpassen (idempotent, auch bei -ProfilesOnly).
    switch ($id) {
        'simcity2000' {
            # Effekte SB16 220h - IRQ 5 -> 7 (DOSBox-Pure-Standard), Musik bleibt Roland MT-32 ueber
            # MPU-401 (hier: Soundfont); Grafik VESA-Modus nach -SimCityResolution.
            $path = "$dst\SC2000.CFG"
            if (-not (Test-Path $path)) { Write-Warning "Fehlt: $path"; return }
            $cfg = [IO.File]::ReadAllText($path, $ascii)
            $cfg = $cfg -replace '(?m)^SFXIRQ=\d+', 'SFXIRQ=7' -replace '(?m)^SFXADDRESS=\w+', 'SFXADDRESS=220' -replace '(?m)^SFXDMA=\d+', 'SFXDMA=1'
            $cfg = $cfg -replace '(?m)^VIDEOCARD=\S+', ('VIDEOCARD=' + $simCityVideo[$SimCityResolution])
            [IO.File]::WriteAllText($path, $cfg, $ascii)
            Write-Host ("  SC2000.CFG: VIDEOCARD={0} ({1}x256), Effekte SB16 220h/7/1" -f $simCityVideo[$SimCityResolution], $SimCityResolution)
        }
    }
}

function Prepare([string]$id, [string]$src, [string]$dst) {
    $g = $games[$id]
    if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
    New-Item -ItemType Directory -Force $dst | Out-Null
    $dst = (Resolve-Path $dst).Path
    $midi = $false
    switch ($id) {
        'pirates-gold' {
            # GOG: imgmount D DATA.DAT (ISO), Start auf D:\PIRATESG.CD mit cdpatch; Sprache G = Deutsch.
            # CONFIG.PIR (GOG) steht schon auf Soundblaster 220h, IRQ 7, DMA 1 - nur pruefen.
            foreach ($p in 'DATA.DAT', 'PIRATESG.CD\CONFIG.PIR', 'PIRATESC') { Need "$src\$p" }
            foreach ($d in 'PIRATESC', 'PIRATESG.CD', 'MPS') { if (Test-Path "$src\$d") { Copy-Into "$src\$d" $dst } }
            Copy-Item "$src\DATA.DAT" "$dst\PIRATESG.ISO"
            foreach ($cfg in "$dst\PIRATESG.CD\CONFIG.PIR", "$dst\PIRATESC\CONFIG.PIR") {
                $b = [IO.File]::ReadAllBytes($cfg)
                $port = [BitConverter]::ToUInt16($b, 6); $irq = [BitConverter]::ToUInt16($b, 8); $dma = [BitConverter]::ToUInt16($b, 10)
                if ($port -ne 0x220 -or $irq -ne 7 -or $dma -ne 1) {
                    Write-Warning ("{0}: Soundblaster {1:X}h/{2}/{3} statt 220h/7/1" -f $cfg, $port, $irq, $dma)
                }
            }
            Write-Dos "$dst\XRSTART.BAT" @('@echo off', 'D:', 'cd \PIRATESG.CD', 'lh cdpatch',
                'piratesg.exe G', 'cdpatch x', 'C:')
        }
        'pirates' {
            # GOG-Variante EGA/PC-Lautsprecher: STUFFIT tippt die Startfragen (Grafik 3 = EGA,
            # 1 Laufwerk, Steuerung 2 = Tastatur). DISKS = Spielstand-"Diskette" aus __support\save.
            foreach ($p in 'pir.exe', 'disk1', 'disk2', 'diskp', 'STUFFIT.COM', '__support\save\disks') { Need "$src\$p" }
            foreach ($f in 'pir.exe', 'disk1', 'disk2', 'diskp') { Copy-Item "$src\$f" ("$dst\" + $f.ToUpper()) }
            Copy-Item "$src\STUFFIT.COM" "$dst\STUFFIT.COM"
            Copy-Item "$src\__support\save\disks" "$dst\DISKS"
            Write-Dos "$dst\XRSTART.BAT" @('@echo off', 'C:', 'cd \',
                "STUFFIT.COM +1 '1' +1 '1' +0 '3' +0 '1' +0 '2'", 'PIR.EXE')
        }
        'populous' {
            # GOG: "populous Vga Roland MT-32" - Musik ueber MPU-401 (hier: Soundfont), 7500 Zyklen.
            Need "$src\POPULOUS.EXE"; Need "$src\DATA"
            Copy-Item "$src\POPULOUS.EXE" $dst
            if (Test-Path "$src\TRANS.EXE") { Copy-Item "$src\TRANS.EXE" $dst }
            Copy-Into "$src\DATA" $dst
            Write-Dos "$dst\XRSTART.BAT" @('@echo off', 'C:', 'cd \', 'populous Vga Roland MT-32')
            $midi = $true
        }
        'populous2' {
            # GOG: GO.BAT = INTRO + POP2 /ss /h (Soundblaster, hohe Detailstufe), SB 220h/5/1.
            foreach ($p in 'POP2.EXE', 'INTRO.EXE', 'DATA', 'SOUND') { Need "$src\$p" }
            foreach ($f in 'POP2.EXE', 'INTRO.EXE', 'GO.BAT', 'GO_LOW.BAT') { if (Test-Path "$src\$f") { Copy-Item "$src\$f" $dst } }
            foreach ($d in 'DATA', 'SOUND', 'POPSAVE') { if (Test-Path "$src\$d") { Copy-Into "$src\$d" $dst } }
            Write-Dos "$dst\XRSTART.BAT" @('@echo off', 'C:', 'cd \', 'INTRO', 'POP2 /ss /h')
        }
        'simcity2000' {
            # DOS-Fassung (SC2K.EXE, DOS/4GW, VESA). GOG: imgmount d SC2000SE.DAT (ISO: Intro-/WillTV-
            # Videos), LOADFIX -1 SC2K. SC2000.CFG (Ton, Aufloesung): Patch-GameConfig.
            foreach ($p in 'SC2K.EXE', 'SC2000.CFG', 'SC2000SE.DAT', 'DOS4GW.EXE') { Need "$src\$p" }
            $skip = '^(DOSBOX|__redist|__support|app|commonappdata|tmp|cloud_saves)$|^goggame-|^loader\.|\.pdf$|^SC2000SE\.DAT$'
            Get-ChildItem $src | Where-Object { $_.Name -notmatch $skip } | ForEach-Object { Copy-Into $_.FullName $dst }
            Copy-Item "$src\SC2000SE.DAT" "$dst\SC2000SE.ISO"
            Write-Dos "$dst\XRSTART.BAT" @('@echo off', 'C:', 'cd \', ':RUN', 'LOADFIX -1 SC2K',
                'if errorlevel 55 goto WILL', 'goto END', ':WILL', 'WILLTV', 'goto RUN', ':END')
            $midi = $true
        }
        'syndicate-wars' {
            # DOS-Fassung (MAIN.EXE /w /g, DOS/4GW), nicht die swars-Portierung. GOG: imgmount d
            # SWARS\game.ins (Cue + game.gog, CD-Audio), Start ueber PLAY.BAT. Deutsch wie GOGs
            # "Language Settings": LANGUAGE\GER\* an ihre Plaetze, LANGUAGE\_GER -> CONFIG.INI.
            # Musik: SBPRO2.MDI (OPL), Effekte SB16.DIG mit IRQ/DMA aus BLASTER (220h/7/1).
            $s = "$src\SWARS"
            if (-not (Test-Path $s) -and (Test-Path "$src\app\SWARS")) { $s = "$src\app\SWARS" }
            foreach ($p in 'MAIN.EXE', 'game.ins', 'game.gog', 'LANGUAGE\GER\TEXT.DAT', 'LANGUAGE\_GER') { Need "$s\$p" }
            New-Item -ItemType Directory -Force "$dst\SWARS" | Out-Null
            Get-ChildItem $s | Where-Object { $_.Name -notmatch '^(game\.gog|game\.ins|LANGUAGE)$' } |
                ForEach-Object { Copy-Into $_.FullName "$dst\SWARS" }
            $map = @{ 'ALLTEXT.IDX' = 'QDATA'; 'ALLTEXT.WAD' = 'QDATA'; 'INTRO.SMK' = 'QDATA'; 'CHU_ELE.SMK' = 'DATA';
                      'SYN_ELE.SMK' = 'DATA'; 'TEXT.DAT' = 'DATA'; 'SOUND.DAT' = 'SOUND' }
            foreach ($f in $map.Keys) { Copy-Item "$s\LANGUAGE\GER\$f" "$dst\SWARS\$($map[$f])\$f" -Force }
            Copy-Item "$s\LANGUAGE\_GER" "$dst\SWARS\CONFIG.INI" -Force
            # CD-Image auf C: (Wurzel): DOSBox-Pure haengt es beim AUTOBOOT-Start als D: ein.
            Copy-Item "$s\game.ins" "$dst\GAME.INS"
            Copy-Item "$s\game.gog" "$dst\game.gog"
            Write-Dos "$dst\XRSTART.BAT" @('@echo off', 'C:', 'cd \SWARS',
                'SMACKPLY.EXE C:\SWARS\QDATA\INTRO.SMK', 'MAIN.EXE /w /g')
        }
        'worms' {
            # Steam "Worms" (App 70640) = Worms United, DOS/4GW, im DOSBox-0.74-Paket. Steam:
            # C: = DriveC (installiertes WORMS\), imgmount d worms.cue (Datenspur worms.bin +
            # Audiospuren 02-18.ogg = Musik), Start DriveC\goworms.bat -> WORMS.BAT. Soundkarte
            # erkennt Worms ueber BLASTER. WormsCD\ (entpackte CD) und DOSBox\ bleiben weg.
            foreach ($p in 'DriveC\WORMS\BIN\WRMS.EXE', 'DriveC\WORMS\DATA', 'DriveC\WORMS\WORMS.CFG', 'worms.bin', 'worms.cue') { Need "$src\$p" }
            Copy-Into "$src\DriveC\WORMS" "$dst\WORMS"
            # CD-Image auf C: (Wurzel), Musik in CDAUDIO\. DOSBox Pure erkennt OGG am Dateikopf,
            # der Typ in der CUE ist egal (WAVE wie bei Quake).
            Copy-Item "$src\worms.bin" "$dst\WORMS.BIN"
            $cue = @('FILE "WORMS.BIN" BINARY', '  TRACK 01 MODE1/2352', '    INDEX 01 00:00:00')
            $tracks = @(Get-ChildItem -LiteralPath $src -File | Where-Object { $_.Name -match '^\d\d\.ogg$' } | Sort-Object Name)
            if (-not $tracks.Count) { Write-Warning "Keine Musikspuren (NN.ogg) neben worms.cue - Worms ohne CD-Musik." }
            else { New-Item -ItemType Directory -Force "$dst\CDAUDIO" | Out-Null }
            $n = 2
            foreach ($t in $tracks) {
                $num = [int]$t.BaseName
                if ($num -ne $n) { Write-Warning "Musik: $($t.Name) ausserhalb der Reihe (erwartet $('{0:D2}' -f $n).ogg) - uebersprungen"; continue }
                $name = 'TRACK{0:D2}.OGG' -f $num
                Copy-Item -LiteralPath $t.FullName "$dst\CDAUDIO\$name"
                $cue += @("FILE ""CDAUDIO\$name"" WAVE", ('  TRACK {0:D2} AUDIO' -f $num), '    INDEX 01 00:00:00')
                $n++
            }
            Write-Dos "$dst\WORMS.CUE" $cue
            if ($tracks.Count) { Write-Host ("  Musik: {0} Spuren als CD-Image (WORMS.CUE)" -f ($n - 2)) }
            # Wie WORMS.BAT von Steam, nur mit festen Pfaden (wormscd = CD-Laufwerk mit FMV\ und Musik).
            Write-Dos "$dst\XRSTART.BAT" @('@echo off', 'C:', 'cd \WORMS', 'SET dos4g=quiet',
                'SET wormscfg=C:\WORMS\WORMS.CFG', 'SET wormscd=D:', 'C:\WORMS\BIN\BLACK.EXE', 'C:\WORMS\BIN\WRMS')
        }
        # Shooter (vorbereitet, ungeprueft gegen echte Installer). GOG/Steam starten jeweils
        # die EXE im Ordner des IWAD (Final Doom: cd TNT bzw. PLUTONIA, DOOM2.EXE).
        'doom'          { Prepare-Doom $src $dst 'DOOM.WAD' @('DOOM.EXE') }
        'doom2'         { Prepare-Doom $src $dst 'DOOM2.WAD' @('DOOM2.EXE') }
        'doom-tnt'      { Prepare-Doom $src $dst 'TNT.WAD' @('FINAL.EXE', 'DOOM2.EXE') }
        'doom-plutonia' { Prepare-Doom $src $dst 'PLUTONIA.WAD' @('FINAL.EXE', 'DOOM2.EXE') }
        'quake' {
            # DOS-Fassung 1.06 (Steam/GOG: QUAKE.EXE + CWSDPMI.EXE neben Id1\). Nur das Basisspiel
            # (Missionspakete: xr.shooter). Musik: Die Original-CD hatte Audiospuren 2-11; die
            # Neuauflage liefert sie als rerelease\id1\music\trackNN.ogg (44,1 kHz Stereo). Daraus
            # wird QUAKE.CUE (Datenspur QUAKE.ISO + OGG-Spuren) - DOSBox Pure haengt die CUE beim
            # AUTOBOOT-Start als D: ein, Quake spielt die Spuren per MSCDEX wie von der CD.
            $exe = Find-GameFile $src 'QUAKE.EXE'
            if (-not $exe) { throw "Keine DOS-Fassung: QUAKE.EXE fehlt unter $src - Weg 2: App xr.shooter (TyrQuake)." }
            $id1 = Get-ChildItem -LiteralPath $exe.DirectoryName -Directory | Where-Object { $_.Name -ieq 'id1' } | Select-Object -First 1
            if (-not $id1) { throw "Fehlt: id1\ neben $($exe.FullName)" }
            $paks = @(Get-ChildItem -LiteralPath $id1.FullName -File | Where-Object { $_.Name -like 'PAK*.PAK' })
            if (-not ($paks | Where-Object { $_.Name -ieq 'PAK0.PAK' })) { throw "Fehlt: $($id1.FullName)\PAK0.PAK" }
            Copy-Item -LiteralPath $exe.FullName "$dst\QUAKE.EXE"
            $dpmi = Find-GameFile $src 'CWSDPMI.EXE' $exe.DirectoryName
            if ($dpmi) { Copy-Item -LiteralPath $dpmi.FullName "$dst\CWSDPMI.EXE" }
            else { Write-Warning "CWSDPMI.EXE fehlt - DOSBox stellt DPMI selbst bereit, sollte trotzdem laufen." }
            New-Item -ItemType Directory -Force "$dst\ID1" | Out-Null
            foreach ($f in $paks) { Copy-Item -LiteralPath $f.FullName "$dst\ID1\$($f.Name.ToUpper())" }
            $music = @(Get-ChildItem -LiteralPath $src -Recurse -File -Filter 'track*.ogg' -ErrorAction SilentlyContinue |
                Where-Object { $_.Directory.Name -ieq 'music' -and $_.Directory.Parent.Name -ieq 'id1' -and $_.Name -match '(?i)^track(\d\d)\.ogg$' } |
                Sort-Object Name)
            $start = 'QUAKE.EXE'
            if ($music.Count) {
                New-Item -ItemType Directory -Force "$dst\CDAUDIO" | Out-Null
                Write-Iso "$dst\QUAKE.ISO" 'QUAKE' 'Quake CD (Datenspur), Musik in CDAUDIO\ - XRShell'
                $cue = @('FILE "QUAKE.ISO" BINARY', '  TRACK 01 MODE1/2048', '    INDEX 01 00:00:00')
                $n = 2
                foreach ($m in $music) {
                    $num = [int]($m.Name.Substring(5, 2))
                    if ($num -ne $n) { Write-Warning "Musik: $($m.Name) ausserhalb der Reihe (erwartet track$('{0:D2}' -f $n).ogg) - uebersprungen"; continue }
                    $name = 'TRACK{0:D2}.OGG' -f $num
                    Copy-Item -LiteralPath $m.FullName "$dst\CDAUDIO\$name"
                    $cue += @("FILE ""CDAUDIO\$name"" WAVE", ('  TRACK {0:D2} AUDIO' -f $num), '    INDEX 01 00:00:00')
                    $n++
                }
                Write-Dos "$dst\QUAKE.CUE" $cue
                Write-Host ("  Musik: {0} Spuren als CD-Image (QUAKE.CUE)" -f ($n - 2))
            } else {
                $start = 'QUAKE.EXE -nocdaudio'
                Write-Warning "Keine Musik (rerelease\id1\music\trackNN.ogg) gefunden - Quake ohne CD-Musik."
            }
            Write-Dos "$dst\XRSTART.BAT" @('@echo off', 'C:', 'cd \', $start)
        }
    }

    # Start ohne DOSBox-Pure-Startmenue; nur so wird das CD-Image auf C: als D: eingehaengt.
    Write-Dos "$dst\AUTOBOOT.DBP" @('C:\XRSTART.BAT')
    Copy-Profiles $id $dst
    Patch-GameConfig $id $dst
    if ($midi) {
        if (Test-Path $SoundFont) { Copy-Item $SoundFont "$dst\DOSBOX.SF2" }
        else { Write-Warning "Keine Soundfont ($SoundFont) - MIDI-Musik bleibt stumm." }
    }
    $size = (Get-ChildItem $dst -Recurse -File | Measure-Object Length -Sum).Sum
    Write-Host ("{0,-15} -> {1} ({2:N0} MB)" -f $id, $dst, ($size / 1MB))
}

$ids = if ($Game -eq 'all') { @($games.Keys) } else { @($Game) }
foreach ($id in $ids) {
    $dst = if ($Out) { if ($Game -eq 'all') { Join-Path $Out $games[$id].Folder } else { $Out } }
           else { Join-Path $platform "original-data\quest\dos\$($games[$id].Folder)" }
    if ($ProfilesOnly) {
        if (-not (Test-Path $dst)) { Write-Warning "Fehlt: $dst - erst ohne -ProfilesOnly bauen"; continue }
        Copy-Profiles $id (Resolve-Path $dst).Path
        Patch-GameConfig $id (Resolve-Path $dst).Path
        Write-Host ("{0,-15} -> {1} (Profile erneuert)" -f $id, $dst)
        continue
    }
    if (-not $Source) { throw "-Source fehlt (entpackter GOG-Ordner) - oder -ProfilesOnly" }
    $sub = if ($games[$id].Src) { $games[$id].Src } else { $id }
    $src = if ($Game -eq 'all') { Join-Path $Source $sub } else { $Source }
    if ($Game -eq 'all' -and -not (Test-Path $src)) { Write-Host ("{0,-15} -- uebersprungen ({1} fehlt)" -f $id, $src); continue }
    $src = (Resolve-Path $src).Path
    Prepare $id $src $dst
}
Write-Host "Auf die Quest (xr.dosgames): APP_ID=xr.dosgames bash tools/push-game.sh <Spielordner> <Ordnername>"
