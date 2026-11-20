# Play session with performance logging. No SD configuration or save edits.
# Sends the NRO with nxlink and keeps the console's output while you play;
# stop with Ctrl+C (or quit the game). Evidence goes to
# game/build/hw-test/perf-play/<time>/:
#   run.log    everything the console printed
#   perf.log   the [perf] / [perf-gl] lines (every -PerfSecs seconds) and the
#              [hitch] lines (frames over 50 ms)
#   prof.log   with -CallProfile: the [MH-PROF] hottest guest functions (10 s windows)
# -CallProfile needs a build made with -DMANHUNT_CALL_PROFILE=ON in game/build/switch-prof.
param(
    [switch]$CallProfile,
    [ValidateRange(1, 60)]
    [int]$PerfSecs = 2,
    [string]$Address = '192.168.1.107',
    [string]$Nro,                       # an earlier build for A/B; default is the current build
    [string[]]$Extra = @(),             # more KEY=VALUE settings, e.g. MANHUNT_FPS_CAP=30
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$default = if ($CallProfile) { 'game/build/switch-prof/manhunt.nro' } else { 'game/build/switch/manhunt.nro' }
$nro = if ($Nro) { (Resolve-Path -LiteralPath $Nro).Path } else { Join-Path $repo $default }
if (!(Test-Path -LiteralPath $nro)) { throw "Build missing: $nro" }
# The configuration every hardware measurement used (reports/PERF_INVESTIGATION_2026-11-16.md, item 1).
$settings = @(
    "MANHUNT_PERF_LOG=$PerfSecs", 'RECOMP_GL_THREAD=1', 'MANHUNT_DUMP_SECS=0', 'MANHUNT_LOG_SYNC=0'
) + $Extra
$manifest = [ordered]@{call_profile=[bool]$CallProfile;address=$Address;nro_sha256=(Get-FileHash -LiteralPath $nro -Algorithm SHA256).Hash.ToLower();arguments=$settings}
if ($DryRun) { $manifest | ConvertTo-Json -Depth 4; return }
$run = Join-Path $repo ("game/build/hw-test/perf-play/{0}{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $(if ($CallProfile) { '-prof' } else { '' }))
New-Item -ItemType Directory -Path $run | Out-Null
$copy = Join-Path $run 'manhunt.nro'
Copy-Item -LiteralPath $nro -Destination $copy
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $run 'manifest.json') -Encoding utf8
Write-Host "Sending; play, then Ctrl+C. Evidence: $run"
$log = Join-Path $run 'run.log'
$dockerArgs = @('run','--rm','--name','manhunt-perf-play-nxlink','-p','28771:28771','--mount',"type=bind,src=$copy,dst=/nro/manhunt.nro,readonly",
    'devkitpro/devkita64:20260219','nxlink','-a',$Address,'-r','5','-s','/nro/manhunt.nro') + $settings
try {
    & docker @dockerArgs 2>&1 | Tee-Object -FilePath $log
} finally {
    docker rm -f manhunt-perf-play-nxlink 2>$null | Out-Null
    if (Test-Path -LiteralPath $log) {
        Select-String -LiteralPath $log -Pattern '^\[perf', '^\[hitch\]' | ForEach-Object { $_.Line } |
            Set-Content -LiteralPath (Join-Path $run 'perf.log') -Encoding utf8
        if ($CallProfile) {
            Select-String -LiteralPath $log -Pattern '^\[MH-PROF\]' | ForEach-Object { $_.Line } |
                Set-Content -LiteralPath (Join-Path $run 'prof.log') -Encoding utf8
        }
        Write-Host "Logs: $run"
    }
}
