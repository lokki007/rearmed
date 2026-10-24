# REARMED setup: finds your Quake III Arena install and builds the Team Arena weapon pack
# from your own copy of Team Arena (nothing from id Software ships with REARMED).
# Run by "Play REARMED.bat" on the first start. Safe to run again: it only writes inside this folder.
param([string]$Q3Path = "")

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Test-Q3([string]$p) {
    return $p -and (Test-Path (Join-Path $p "baseq3\pak0.pk3"))
}

function Find-Q3 {
    $candidates = @()
    # Steam: every library folder
    foreach ($key in "HKCU:\Software\Valve\Steam", "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam") {
        try {
            $steam = (Get-ItemProperty $key -ErrorAction Stop)
            $root = if ($steam.SteamPath) { $steam.SteamPath } else { $steam.InstallPath }
            if ($root) {
                $candidates += Join-Path $root "steamapps\common\Quake 3 Arena"
                $vdf = Join-Path $root "steamapps\libraryfolders.vdf"
                if (Test-Path $vdf) {
                    foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                        $candidates += Join-Path ($m.Groups[1].Value -replace '\\\\', '\') "steamapps\common\Quake 3 Arena"
                    }
                }
            }
        } catch {}
    }
    try { $candidates += (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 2200" -ErrorAction Stop).InstallLocation } catch {}
    # GOG
    foreach ($key in "HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games\1441704920", "HKLM:\SOFTWARE\GOG.com\Games\1441704920") {
        try { $candidates += (Get-ItemProperty $key -ErrorAction Stop).path } catch {}
    }
    # REARMED unpacked inside the game folder
    $candidates += Split-Path -Parent $Here
    foreach ($c in $candidates) { if (Test-Q3 $c) { return (Resolve-Path $c).Path } }
    return $null
}

if (-not (Test-Q3 $Q3Path)) { $Q3Path = Find-Q3 }
if (-not $Q3Path) {
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = "REARMED needs Quake III Arena. Pick the folder that contains baseq3\pak0.pk3."
    if ($dlg.ShowDialog() -eq "OK" -and (Test-Q3 $dlg.SelectedPath)) { $Q3Path = $dlg.SelectedPath }
}
if (-not $Q3Path) {
    Write-Host "Quake III Arena was not found. Run: powershell -File setup.ps1 -Q3Path ""C:\path\to\Quake 3 Arena"""
    exit 1
}
Set-Content -Path (Join-Path $Here "q3path.txt") -Value $Q3Path -NoNewline
Write-Host "Quake III Arena: $Q3Path"

# Team Arena weapons and DESCENT arenas (mptourney, mpteam maps): copy the listed files out of the player's own missionpack\pak0.pk3
$ta = Join-Path $Q3Path "missionpack\pak0.pk3"
$out = Join-Path $Here "home\rearmed\zz-rearmed-ta.pk3"
if (-not (Test-Path $ta)) {
    Write-Host "Team Arena not found: the nailgun, chaingun and prox launcher will have no models."
    exit 0
}
$manifest = Get-Content (Join-Path $Here "ta_manifest.txt") | Where-Object { $_ -and -not $_.StartsWith("#") }
$files = $manifest | Where-Object { $_.StartsWith("file ") } | ForEach-Object { $_.Substring(5) }
$shaders = $manifest | Where-Object { $_.StartsWith("shader ") } | ForEach-Object { $_.Substring(7).ToLower() }

$src = [System.IO.Compression.ZipFile]::OpenRead($ta)
try {
    $byName = @{}
    foreach ($e in $src.Entries) { $byName[$e.FullName.ToLower()] = $e }

    # shader blocks by name, from the Team Arena shader scripts
    $blocks = @{}
    foreach ($e in $src.Entries | Where-Object { $_.FullName -like "scripts/*.shader" }) {
        $r = New-Object System.IO.StreamReader($e.Open(), [System.Text.Encoding]::GetEncoding(28591))
        $txt = [regex]::Replace($r.ReadToEnd(), '//[^\n]*', '')
        $r.Close()
        $i = 0
        while ($true) {
            $m = [regex]::new('\s*([^\s{}]+)\s*\{').Match($txt, $i)
            if (-not $m.Success -or $m.Index -ne $i) { break }
            $j = $m.Index + $m.Length - 1; $depth = 0
            while ($j -lt $txt.Length) {
                if ($txt[$j] -eq '{') { $depth++ } elseif ($txt[$j] -eq '}') { $depth--; if ($depth -eq 0) { break } }
                $j++
            }
            $blocks[$m.Groups[1].Value.ToLower()] = $txt.Substring($m.Groups[1].Index, $j + 1 - $m.Groups[1].Index)
            $i = $j + 1
        }
    }

    $tmp = "$out.tmp"
    if (Test-Path $tmp) { Remove-Item $tmp }
    $dst = [System.IO.Compression.ZipFile]::Open($tmp, "Create")
    $stamp = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
    try {
        $missing = 0
        foreach ($f in ($files | Sort-Object)) {
            $e = $byName[$f.ToLower()]
            if (-not $e) { Write-Host "missing in Team Arena: $f"; $missing++; continue }
            $n = $dst.CreateEntry($f, [System.IO.Compression.CompressionLevel]::Optimal)
            $n.LastWriteTime = $stamp
            $a = $e.Open(); $b = $n.Open(); $a.CopyTo($b); $b.Close(); $a.Close()
        }
        $text = "// Team Arena weapon and map shaders, copied from your Team Arena install by REARMED setup`n`n"
        foreach ($s in $shaders) {
            if ($blocks[$s]) { $text += $blocks[$s] + "`n`n" } else { Write-Host "missing shader: $s"; $missing++ }
        }
        $n = $dst.CreateEntry("scripts/rearmed_ta.shader", [System.IO.Compression.CompressionLevel]::Optimal)
        $n.LastWriteTime = $stamp
        $w = New-Object System.IO.StreamWriter($n.Open(), [System.Text.Encoding]::GetEncoding(28591))
        $w.Write($text); $w.Close()
    } finally { $dst.Dispose() }
    Move-Item -Force $tmp $out
    Write-Host ("Team Arena weapons and maps: {0} files{1}" -f $files.Count, $(if ($missing) { ", $missing missing" } else { "" }))
} finally { $src.Dispose() }
