param(
    [Parameter(Mandatory = $true)][string]$Evidence,  # new folder for captures + log excerpt
    [string]$AutotestText = '',                       # non-empty: write ux0:data/noctopage/autotest.txt
    [string]$CaptureOn = 'AUTOTEST',                  # capture when the new log text contains this
    [int]$CaptureDelayMs = 700,                       # after the trigger line, before capturing
    [int]$TimeoutSec = 120,
    [string[]]$FreshWork = @()                        # files under ux0:data/noctopage/work a run creates
)
# run.ps1 - boot NOCT00001 in Vita3K, capture the screen at each "shot NAME" of the autotest script,
# then let the app exit (autotest) or stop MY Vita3K process. Never touches other Vita3K processes
# (other sessions use Vita3K too) and never redirects Vita3K's stdout.
# The app renames autotest.txt itself when it starts. The only files removed are the -FreshWork files a
# scenario creates (so the next run starts the same way), and they go to the Recycle Bin.
$ErrorActionPreference = 'Stop'
$exe   = 'C:\Users\steve\vita-dev\Vita3K\Vita3K.exe'
$root  = 'C:\Users\steve\AppData\Roaming\Vita3K\Vita3K'
$data  = Join-Path $root 'ux0\data\noctopage'
$log   = Join-Path $data 'log.txt'
$trig  = Join-Path $data 'autotest.txt'
$shots = Join-Path (Split-Path $exe) 'screenshots'

if (Test-Path $Evidence) { "evidence dir exists, refusing to overwrite: $Evidence"; exit 4 }
New-Item -ItemType Directory -Path $Evidence | Out-Null
New-Item -ItemType Directory -Force -Path $data | Out-Null

# Captures use Vita3K's own screenshot, which reads the frame back from the GPU: it works without focus
# and with the display asleep, where PrintWindow and screen copies of the Vulkan window come back white.
# Vita3K runs on a copy of the user's config with F12 bound to it; the user's config.yml is never written.
$cfg = Join-Path $Evidence 'vita3k-config.yml'
$lines = Get-Content (Join-Path (Split-Path $exe) 'config.yml')
if ($lines | Where-Object { $_ -match '^keyboard-[a-z0-9-]+: F12\s*$' -and $_ -notmatch '^keyboard-take-screenshot:' }) {
    "F12 is bound to something else in Vita3K's config.yml: pick another key here"; exit 5
}
[IO.File]::WriteAllLines($cfg, [string[]]($lines -replace '^keyboard-take-screenshot: .*$', 'keyboard-take-screenshot: F12'))

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class NpWin {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static List<IntPtr> WindowsOf(uint pid) {
        var list = new List<IntPtr>();
        EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid && IsWindowVisible(h)) list.Add(h); return true; }, IntPtr.Zero);
        return list;
    }
    public static string Title(IntPtr h) { var sb = new StringBuilder(512); GetWindowText(h, sb, 512); return sb.ToString(); }
    // F12 as window messages (scan code 0x58), not as input: no focus needed, and the PC's idle timer
    // and display are left alone.
    public static void PressF12(IntPtr h) {
        PostMessage(h, 0x100, (IntPtr)0x7B, (IntPtr)0x00580001L);
        System.Threading.Thread.Sleep(100);
        PostMessage(h, 0x101, (IntPtr)0x7B, (IntPtr)0xC0580001L);
    }
}
"@

# A capture that is one colour all over is a capture failure, never a picture: say so.
function Uniform([System.Drawing.Bitmap]$b) {
    $first = $b.GetPixel(0, 0)
    for ($y = 0; $y -lt $b.Height; $y += [Math]::Max(1, [int]($b.Height / 24))) {
        for ($x = 0; $x -lt $b.Width; $x += [Math]::Max(1, [int]($b.Width / 24))) {
            if ($b.GetPixel($x, $y) -ne $first) { return $null }
        }
    }
    return $first
}

function ShotFiles { Get-ChildItem $shots -Recurse -File -ErrorAction SilentlyContinue }

# Press the screenshot key, wait for Vita3K's new file, and move it (it is this run's) into the evidence.
function Capture([IntPtr]$h, [string]$name) {
    $before = @{}; ShotFiles | ForEach-Object { $before[$_.FullName] = 1 }
    [NpWin]::PressF12($h)
    $deadline = (Get-Date).AddSeconds(3); $new = $null
    while (-not $new -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 150
        $new = ShotFiles | Where-Object { -not $before.ContainsKey($_.FullName) } | Select-Object -First 1
    }
    if (-not $new) { "NO SCREENSHOT for $name"; return }
    do { $len = $new.Length; Start-Sleep -Milliseconds 150; $new.Refresh() } while ($new.Length -ne $len)
    $dest = Join-Path $Evidence ($name + $new.Extension)
    Move-Item $new.FullName $dest
    $b = New-Object System.Drawing.Bitmap $dest
    $blank = Uniform $b; $size = "$($b.Width)x$($b.Height)"; $b.Dispose()
    $note = if ($blank) { " BLANK (all $($blank.Name))" } else { '' }
    "captured $(Split-Path -Leaf $dest): $size$note"
}

function NewLogText([long]$from) {
    if (-not (Test-Path $log)) { return '' }
    $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    try {
        if ($fs.Length -le $from) { return '' }
        [void]$fs.Seek($from, 'Begin')
        $buf = New-Object byte[] ($fs.Length - $from)
        $n = $fs.Read($buf, 0, $buf.Length)
        return [Text.Encoding]::UTF8.GetString($buf, 0, $n)
    } finally { $fs.Close() }
}

foreach ($f in $FreshWork) {
    if ($f -match '\.\.|^[\\/]|:') { throw "FreshWork takes plain names under work\, not '$f'" }
    $p = Join-Path (Join-Path $data 'work') $f
    if (Test-Path -LiteralPath $p -PathType Leaf) {
        Add-Type -AssemblyName Microsoft.VisualBasic
        [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($p, 'OnlyErrorDialogs', 'SendToRecycleBin')
        "recycled work\$f"
    }
}
$logStart = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }
if ($AutotestText -ne '') { [IO.File]::WriteAllText($trig, $AutotestText); "wrote autotest.txt: '$AutotestText'" }

$p = Start-Process -FilePath $exe -ArgumentList @('-c', $cfg, '-r', 'NOCT00001') -PassThru
$t0 = Get-Date
"started Vita3K PID $($p.Id) at $($t0.ToString('HH:mm:ss'))"
$win = $null; $shot = 0; $text = ''; $named = @{}
while (((Get-Date) - $t0).TotalSeconds -lt $TimeoutSec -and -not $p.HasExited) {
    if (-not $win) {
        foreach ($h in [NpWin]::WindowsOf([uint32]$p.Id)) {
            if ([NpWin]::Title($h) -match 'NOCT00001|Noctopage') { $win = $h }
        }
        if ($win) { "game window '$([NpWin]::Title($win))' after $([int]((Get-Date) - $t0).TotalSeconds) s" }
    }
    $text = NewLogText $logStart
    if ($win -and $shot -eq 0 -and $CaptureOn -ne '' -and $text.Contains($CaptureOn)) {
        Start-Sleep -Milliseconds $CaptureDelayMs
        $shot++
        Capture $win "shot$shot"
    }
    # The script's "shot NAME" logs "AUTOTEST shot NAME" and holds the screen for 1500 ms: capture each
    # name once.
    if ($win) {
        foreach ($m in [regex]::Matches($text, 'AUTOTEST shot ([A-Za-z0-9_-]+)')) {
            $name = $m.Groups[1].Value
            if (-not $named.ContainsKey($name)) {
                $named[$name] = 1
                Start-Sleep -Milliseconds 300
                Capture $win $name
            }
        }
    }
    if ($text -match '\] exit\r?\n') { break }
    Start-Sleep -Milliseconds 400
}
$deadline = (Get-Date).AddSeconds(8)
while (-not $p.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 300 }
if ($p.HasExited) { "PID $($p.Id) exited by itself, code $($p.ExitCode)" }
else {
    if ($win -and $text -notmatch '\] exit\r?\n') {
        "window list before close: $(([NpWin]::WindowsOf([uint32]$p.Id) | ForEach-Object { "'" + [NpWin]::Title($_) + "'" }) -join ', ')"
        $shot++; Capture $win "shot$shot"
    }
    # Ask first (WM_CLOSE) so Vita3K flushes vita3k.log; it buffers 4 KiB and a kill loses the tail.
    [void]$p.CloseMainWindow()
    $deadline = (Get-Date).AddSeconds(8)
    while (-not $p.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 300 }
    if ($p.HasExited) { "closed PID $($p.Id) via its window" }
    else { Stop-Process -Id $p.Id; Start-Sleep -Seconds 1
           "stopped PID $($p.Id); gone=$($null -eq (Get-Process -Id $p.Id -ErrorAction SilentlyContinue))" }
}
# vita3k.log is rewritten by every launch (other sessions too): keep it only if it is from this one.
$vlog = Join-Path (Split-Path $exe) 'vita3k.log'
if ((Test-Path $vlog) -and ((Get-Content $vlog -TotalCount 3) -join "`n") -match 'NOCT00001') {
    Copy-Item $vlog (Join-Path $Evidence 'vita3k.log'); "copied vita3k.log ($((Get-Item $vlog).Length) bytes)"
}
$text = NewLogText $logStart
[IO.File]::WriteAllText((Join-Path $Evidence 'log.txt'), $text)
"--- new log lines ($($text.Length) chars) ---"
$text
