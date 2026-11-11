# Launch the safe rollback by default; experimental GPU paths require explicit opt-in.
# Never edits manhunt.env.
param(
    [switch]$Rollback,
    [switch]$EnableExperimentalGpuPaths,
    [string]$Address = '192.168.1.107'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$nro = Join-Path $repo 'game/build/switch/manhunt.nro'
if (!(Test-Path -LiteralPath $nro)) { throw "Build missing: $nro" }
if ($Rollback -and $EnableExperimentalGpuPaths) { throw 'Choose rollback or experimental GPU paths.' }
$mode = if ($EnableExperimentalGpuPaths) { 'candidate' } else { 'rollback' }
$enabled = if ($EnableExperimentalGpuPaths) { '1' } else { '0' }
$run = Join-Path $repo ("game/build/hw-test/gpu-offload/{0}-{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $mode)
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath $nro -Destination (Join-Path $run 'manhunt.nro')
$copy = Join-Path $run 'manhunt.nro'
$settings = @(
    'MANHUNT_PERF_LOG=2', 'RECOMP_GL_THREAD=1', 'MANHUNT_GPU_BACKEND=2', 'MANHUNT_GL_SYNC=gpu',
    "MANHUNT_GL_PERSISTENT=$enabled", "MANHUNT_GL_UBO=$enabled", "MANHUNT_GL_STATE_CACHE=$enabled", "MANHUNT_GL_CMP=$enabled",
    'MANHUNT_AMBIENT_LIGHT=0.33', 'RECOMP_RENDER_PROFILE=0', 'MANHUNT_GL_VALIDATE=1', 'MANHUNT_CPU_MHZ=1020', 'MANHUNT_GPU_MHZ=307',
    'MANHUNT_DUMP_SECS=0', 'MANHUNT_LOG_SYNC=0'
)
$manifest = [ordered]@{mode=$mode;address=$Address;nro_sha256=(Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash.ToLower();arguments=$settings}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $run 'manifest.json') -Encoding utf8
Write-Host "Sending $mode; evidence: $run"
$dockerArgs = @('run','--rm','--name','manhunt-gpu-offload-nxlink','-p','28771:28771','--mount',"type=bind,src=$copy,dst=/nro/manhunt.nro,readonly",
    'devkitpro/devkita64:20260219','nxlink','-a',$Address,'-r','5','-s','/nro/manhunt.nro') + $settings
& docker @dockerArgs 2>&1 | Tee-Object -FilePath (Join-Path $run 'run.log')
exit $LASTEXITCODE
