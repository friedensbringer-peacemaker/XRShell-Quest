# Bereitet Shooter aus eigenen GOG-/Steam-Kaeufen fuer xr.shooter (PrBoom, ECWolf, TyrQuake) vor.
# BYOA: nur die Datendateien (IWAD, *.WL6/*.SOD, id1\PAK*.PAK) werden kopiert, keine EXE, kein
# Installer. Ziel liegt ausserhalb des Repos: ..\original-data\quest\shooter\<system>\<Ordner>\.
#
#   powershell -File tools\prepare-shooter.ps1 -Source <GOG-Installer.exe>      (entpackt mit innoextract)
#   powershell -File tools\prepare-shooter.ps1 -Source <entpackter GOG-Ordner | Steam-Spielordner>
#   powershell -File tools\prepare-shooter.ps1 -Source <...> -List              (nur anzeigen, nichts kopieren)
#   APP_ID=xr.shooter SYSTEM=doom bash tools/push-game.sh <Ziel>/doom/<Ordner> <Ordner>
#
# Gefunden wird alles, was im Quellordner (rekursiv) liegt - ein Kauf "DOOM + DOOM II" ergibt
# so mehrere Spiele. Originaldateien (Wurzel, base\, DOS-Ordner) gehen vor der 2024er-Neuauflage
# (rerelease\), die nur genommen wird, wenn es nichts anderes gibt (Warnung: PrBoom-Kompatibilitaet
# im Headset pruefen). Spiele mit DOS-EXE zusaetzlich fuer xr.dosgames: tools\prepare-dos-games.ps1.
# Doom-Ordner bekommen dazu <IWAD>.prboom.cfg (profiles\doom.prboom.cfg: 72 fps, Ja-Taste).
# Belegung und Pruefpunkte: docs\SHOOTER.md. Skript bleibt ASCII (Windows PowerShell 5.1).
param(
    [Parameter(Mandatory = $true)]
    [string]$Source,           # Installer (.exe) oder Ordner
    [string]$Out = '',         # Standard: ..\original-data\quest\shooter neben dem Repo
    [switch]$List              # nur Fundliste, nichts kopieren
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$platform = Split-Path $repo -Parent
$xr = Split-Path $platform -Parent
if (-not $Out) { $Out = Join-Path $platform 'original-data\quest\shooter' }
$ascii = [Text.Encoding]::ASCII

# Bekannte Groessen der DOS-Originale (v1.9 bzw. Final Doom/id Anthology) - nur zur Anzeige.
$knownSizes = @{
    'DOOM.WAD' = @(12408292, 11159840); 'DOOM2.WAD' = @(14604584, 14943400)
    'TNT.WAD' = @(18195736, 18654796); 'PLUTONIA.WAD' = @(17420824, 18240172)
}

# Spiele: System, Ordner, Titel, gesuchte Datei (Startdatei), Begleitdateien (Muster im selben Ordner).
$games = @(
    @{ Id = 'doom';      System = 'doom';   Folder = 'Doom';              Title = 'The Ultimate Doom';         File = 'DOOM.WAD' }
    @{ Id = 'doomu';     System = 'doom';   Folder = 'Doom';              Title = 'The Ultimate Doom';         File = 'DOOMU.WAD' }
    @{ Id = 'doom2';     System = 'doom';   Folder = 'Doom2';             Title = 'Doom II';                   File = 'DOOM2.WAD' }
    @{ Id = 'tnt';       System = 'doom';   Folder = 'FinalDoomTNT';      Title = 'Final Doom: TNT Evilution'; File = 'TNT.WAD' }
    @{ Id = 'plutonia';  System = 'doom';   Folder = 'FinalDoomPlutonia'; Title = 'Final Doom: Plutonia';      File = 'PLUTONIA.WAD' }
    # Zusatz-WADs (PWAD) mit eigenem Spielordner: Basis-IWAD daneben, Start ueber xrshell.start;
    # PrBoom sucht zum PWAD das passende IWAD im selben Ordner. Steam "DOOM + DOOM II" liefert sie
    # nur in rerelease\ (KEX-Fassungen) - ungeprueft, ob PrBoom alle Lumps versteht.
    @{ Id = 'sigil';     System = 'doom';   Folder = 'Sigil';             Title = 'Sigil';                     File = 'SIGIL.WAD';  Base = 'DOOM.WAD' }
    @{ Id = 'sigil2';    System = 'doom';   Folder = 'Sigil2';            Title = 'Sigil II';                  File = 'SIGIL2.WAD'; Base = 'DOOM.WAD' }
    @{ Id = 'nerve';     System = 'doom';   Folder = 'NoRestForTheLiving'; Title = 'Doom II: No Rest for the Living'; File = 'NERVE.WAD'; Base = 'DOOM2.WAD' }
    @{ Id = 'master';    System = 'doom';   Folder = 'MasterLevels';      Title = 'Master Levels for Doom II'; File = 'MASTERLEVELS.WAD'; Base = 'DOOM2.WAD' }
    @{ Id = 'freedoom1'; System = 'doom';   Folder = 'Freedoom1';         Title = 'Freedoom: Phase 1';         File = 'FREEDOOM1.WAD' }
    @{ Id = 'freedoom2'; System = 'doom';   Folder = 'Freedoom2';         Title = 'Freedoom: Phase 2';         File = 'FREEDOOM2.WAD' }
    @{ Id = 'wolf3d';    System = 'wolf3d'; Folder = 'Wolf3D';            Title = 'Wolfenstein 3D';            File = 'VSWAP.WL6'; Companions = '*.WL6' }
    @{ Id = 'sod';       System = 'wolf3d'; Folder = 'SpearOfDestiny';    Title = 'Spear of Destiny';          File = 'VSWAP.SOD'; Companions = '*.SOD' }
    @{ Id = 'quake';     System = 'quake';  Folder = 'Quake';             Title = 'Quake';                     File = 'PAK0.PAK'; Companions = 'PAK*.PAK'; Sub = 'id1' }
    # Missionspakete: eigener Ordner mit id1 + Paket; TyrQuake erkennt sie am Ordnernamen im Pfad
    # (hipnotic/rogue -> -hipnotic/-rogue), Spielordner duerfen diese Woerter (und id1) nicht enthalten.
    @{ Id = 'quake-mp1'; System = 'quake';  Folder = 'QuakeMP1';          Title = 'Quake: Scourge of Armagon'; File = 'PAK0.PAK'; Companions = 'PAK*.PAK'; Sub = 'hipnotic'; Base = 'id1' }
    @{ Id = 'quake-mp2'; System = 'quake';  Folder = 'QuakeMP2';          Title = 'Quake: Dissolution of Eternity'; File = 'PAK0.PAK'; Companions = 'PAK*.PAK'; Sub = 'rogue'; Base = 'id1' }
)

function Expand-Installer([string]$exe) {
    $ie = Join-Path $xr '_tools\innoextract\innoextract.exe'
    if (-not (Test-Path $ie)) { throw "innoextract fehlt: $ie" }
    $name = [IO.Path]::GetFileNameWithoutExtension($exe) -replace '^setup_', '' -replace '_\(\d+\)$', ''
    $dst = Join-Path $platform "original-data\gog\$name"
    if (-not (Test-Path $dst)) {
        Write-Host "Entpacke $exe -> $dst"
        & $ie -e -s -d $dst $exe
        if ($LASTEXITCODE -ne 0) { throw "innoextract fehlgeschlagen ($LASTEXITCODE)" }
    } else { Write-Host "Schon entpackt: $dst" }
    return $dst
}

# Rang einer Fundstelle: kleiner = besser. Original vor Neuauflage, flach vor tief.
function Rank([string]$rel) {
    $r = 0
    if ($rel -match '(?i)(^|\\)rerelease(\\|$)') { $r += 1000 }
    if ($rel -match '(?i)(^|\\)(__support|__redist|cloud_saves|tmp)(\\|$)') { $r += 5000 }
    return $r + ($rel.Split('\').Count * 10)
}

# Quake: PAK*.PAK eines Spielverzeichnisses klein geschrieben (Quake oeffnet "pak%i.pak") plus Musik
# music\trackNN.ogg. Die Original-Fassung hatte die Musik nur als CD-Audio; die Steam-/GOG-Neuauflage
# liefert sie als OGG unter rerelease\<dir>\music\ - TyrQuake spielt sie aus <spielordner>\<dir>\music\.
function Copy-QuakeDir([IO.DirectoryInfo]$from, [string]$to, [string]$dirName) {
    New-Item -ItemType Directory -Force $to | Out-Null
    $paks = @(Get-ChildItem $from.FullName -File | Where-Object { $_.Name -like 'PAK*.PAK' })
    foreach ($f in $paks) { Copy-Item $f.FullName (Join-Path $to $f.Name.ToLower()) }
    # je Titel der beste Fund (Original-Ordner vor rerelease\)
    $music = @($all | Where-Object { $_.Name -match '(?i)^track\d\d\.ogg$' -and $_.Directory.Name -ieq 'music' -and
        $_.Directory.Parent.Name -ieq $dirName } | Sort-Object { Rank $_.FullName.Substring($src.Length) } |
        Group-Object { $_.Name.ToLower() } | ForEach-Object { $_.Group[0] })
    if ($music.Count) {
        New-Item -ItemType Directory -Force (Join-Path $to 'music') | Out-Null
        foreach ($m in $music) { Copy-Item $m.FullName (Join-Path (Join-Path $to 'music') $m.Name.ToLower()) }
    }
    return @($paks) + @($music)
}

$src = (Resolve-Path $Source).Path
if ((Get-Item $src) -isnot [IO.DirectoryInfo]) {
    if ($src -notmatch '(?i)\.exe$') { throw "-Source muss ein Ordner oder ein GOG-Installer (.exe) sein: $src" }
    $src = Expand-Installer $src
}
$all = @(Get-ChildItem $src -Recurse -File -ErrorAction SilentlyContinue)
$found = 0
$done = @{}   # Ordner schon belegt (DOOM.WAD und DOOMU.WAD sind dasselbe Spiel: erster Fund gewinnt)
foreach ($g in $games) {
    $hits = @($all | Where-Object { $_.Name -ieq $g.File -and (-not $g.Sub -or $_.Directory.Name -ieq $g.Sub) })
    if ($hits.Count -eq 0) { continue }
    $key = "$($g.System)\$($g.Folder)"
    if ($done.ContainsKey($key)) { Write-Host ("{0,-28} {1} uebersprungen ({2} schon aus {3})" -f $g.Title, $g.File, $key, $done[$key]); continue }
    $done[$key] = $g.File
    $best = $hits | Sort-Object { Rank $_.FullName.Substring($src.Length) } | Select-Object -First 1
    $rel = $best.FullName.Substring($src.Length).TrimStart('\')
    $note = ''
    if ($knownSizes.ContainsKey($g.File)) {
        $note = if ($knownSizes[$g.File] -contains $best.Length) { 'DOS-Original' } else { "unbekannte Groesse $($best.Length) B" }
    }
    if ($rel -match '(?i)(^|\\)rerelease\\') {
        Write-Warning "$($g.Title): nur Neuauflage gefunden ($rel) - Kompatibilitaet mit dem Core im Headset pruefen."
    }
    Write-Host ("{0,-28} {1}  {2}" -f $g.Title, $rel, $note)
    $found++
    if ($List) { continue }

    $dst = Join-Path $Out "$($g.System)\$($g.Folder)"
    if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
    $dataDir = if ($g.Sub) { Join-Path $dst $g.Sub } else { $dst }
    New-Item -ItemType Directory -Force $dataDir | Out-Null
    if ($g.System -eq 'quake') {
        $files = @(Copy-QuakeDir $best.Directory $dataDir $g.Sub)
        if ($g.Base) {
            # Missionspaket: Basisspiel (id1, Original vor Neuauflage) daneben, Start ueber das Paket.
            $baseHit = @($all | Where-Object { $_.Name -ieq 'PAK0.PAK' -and $_.Directory.Name -ieq $g.Base }) |
                Sort-Object { Rank $_.FullName.Substring($src.Length) } | Select-Object -First 1
            if (-not $baseHit) { throw "$($g.Title): $($g.Base)\PAK0.PAK fehlt" }
            $files += @(Copy-QuakeDir $baseHit.Directory (Join-Path $dst $g.Base) $g.Base)
            [IO.File]::WriteAllText("$dst\xrshell.start", "$($g.Sub)/pak0.pak`n", $ascii)
        }
    } else {
        $files = @(if ($g.Companions) { Get-ChildItem $best.DirectoryName -File | Where-Object { $_.Name -like $g.Companions } } else { $best })
        foreach ($f in $files) { Copy-Item $f.FullName (Join-Path $dataDir $f.Name.ToUpper()) }
        if ($g.Base) {
            # PWAD: Basis-IWAD (Original vor Neuauflage) daneben, Start ueber die PWAD.
            $baseHit = @($all | Where-Object { $_.Name -ieq $g.Base }) |
                Sort-Object { Rank $_.FullName.Substring($src.Length) } | Select-Object -First 1
            if (-not $baseHit) { Write-Warning "$($g.Title): $($g.Base) fehlt - Ordner unvollstaendig"; }
            else {
                Copy-Item $baseHit.FullName (Join-Path $dataDir $g.Base)
                $files += $baseHit
            }
            [IO.File]::WriteAllText("$dst\xrshell.start", "$($g.File)`n", $ascii)
        }
    }
    [IO.File]::WriteAllText("$dst\xrshell.title", $g.Title + "`n", $ascii)
    if ($g.System -eq 'doom') {
        # PrBoom-Grundeinstellung (72 fps, Ja-Taste im Gamepad-Modus) als <IWAD>.prboom.cfg neben der WAD.
        $base = [IO.Path]::GetFileNameWithoutExtension($g.File)
        Copy-Item (Join-Path $repo 'profiles\doom.prboom.cfg') (Join-Path $dataDir "$base.prboom.cfg")
    }
    $size = ($files | Measure-Object Length -Sum).Sum
    Write-Host ("  -> {0} ({1} Dateien, {2:N1} MB)" -f $dst, $files.Count, ($size / 1MB))
}
if ($found -eq 0) { throw "Keine Shooter-Daten in $src gefunden (gesucht: $(($games | ForEach-Object File) -join ', '))." }

$exe = @($all | Where-Object { $_.Name -match '(?i)^(doom|doom2|heretic|hexen|wolf3d|spear|duke3d|blood|sw|quake)\.exe$' })
if ($exe.Count) {
    Write-Host "`nDOS-EXE gefunden ($(($exe | ForEach-Object Name | Sort-Object -Unique) -join ', ')) - auch als DOS-Original fuer xr.dosgames moeglich:"
    Write-Host "  powershell -File tools\prepare-dos-games.ps1 -Source `"$src`" -Game <doom|doom2|doom-tnt|doom-plutonia|quake>"
}
if (-not $List) {
    Write-Host "`nAuf die Quest (xr.shooter), je Ordner:"
    Write-Host "  APP_ID=xr.shooter SYSTEM=<doom|wolf3d|quake> bash tools/push-game.sh `"$Out/<system>/<Ordner>`" <Ordner>"
}
