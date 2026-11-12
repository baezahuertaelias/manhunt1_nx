# Controlled same-binary experiments. No SD configuration or save edits.
param(
    [ValidateSet('rollback','cmp','streaming','ubo','state','queue','hrtf','cs','contig','all')]
    [string]$Experiment = 'rollback',
    [switch]$Rollback,
    [switch]$EnableExperimentalGpuPaths,
    [ValidateScript({ $_ -eq -1 -or ($_ -ge 0 -and $_ -le 1) })]
    [double]$AmbientLight = -1,
    [string]$Address = '192.168.1.107',
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$nro = Join-Path $repo 'game/build/switch/manhunt.nro'
if (!(Test-Path -LiteralPath $nro)) { throw "Build missing: $nro" }
if ($EnableExperimentalGpuPaths) {
    if ($PSBoundParameters.ContainsKey('Experiment')) { throw 'Choose Experiment or EnableExperimentalGpuPaths.' }
    $Experiment = 'all'
}
if ($Rollback -and $Experiment -ne 'rollback') { throw 'Choose rollback or an experiment.' }
$flags = @{}
foreach ($name in @('cmp','streaming','ubo','state','queue','hrtf','cs','contig')) {
    $flags[$name] = [int]($Experiment -eq $name -or $Experiment -eq 'all')
}
$ambient = $AmbientLight.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$settings = @(
    'MANHUNT_PERF_LOG=2', 'RECOMP_GL_THREAD=1', 'MANHUNT_GPU_BACKEND=2', 'MANHUNT_GL_SYNC=gpu',
    "MANHUNT_GL_PERSISTENT=$($flags.streaming)", "MANHUNT_GL_UBO=$($flags.ubo)", "MANHUNT_GL_STATE_CACHE=$($flags.state)", "MANHUNT_GL_CMP=$($flags.cmp)",
    "MANHUNT_GL_QUEUE_PACK=$($flags.queue)", "MANHUNT_APU_FAST_HRTF=$($flags.hrtf)", "MANHUNT_CS_CACHE=$($flags.cs)", "MANHUNT_CONTIG_RECLAIM=$($flags.contig)", "MANHUNT_AMBIENT_LIGHT=$ambient", 'RECOMP_RENDER_PROFILE=0', 'MANHUNT_GL_VALIDATE=1', 'MANHUNT_TRANSFORM_TRACE=1', 'MANHUNT_CPU_MHZ=1020', 'MANHUNT_GPU_MHZ=307',
    'MANHUNT_DUMP_SECS=0', 'MANHUNT_LOG_SYNC=0'
)
$manifest = [ordered]@{experiment=$Experiment;address=$Address;nro_sha256=(Get-FileHash -LiteralPath $nro -Algorithm SHA256).Hash.ToLower();arguments=$settings}
if ($DryRun) { $manifest | ConvertTo-Json -Depth 4; return }
$run = Join-Path $repo ("game/build/hw-test/gpu-offload/{0}-{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $Experiment)
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath $nro -Destination (Join-Path $run 'manhunt.nro')
$copy = Join-Path $run 'manhunt.nro'
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $run 'manifest.json') -Encoding utf8
Write-Host "Sending $Experiment; evidence: $run"
$dockerArgs = @('run','--rm','--name','manhunt-gpu-offload-nxlink','-p','28771:28771','--mount',"type=bind,src=$copy,dst=/nro/manhunt.nro,readonly",
    'devkitpro/devkita64:20260219','nxlink','-a',$Address,'-r','5','-s','/nro/manhunt.nro') + $settings
& docker @dockerArgs 2>&1 | Tee-Object -FilePath (Join-Path $run 'run.log')
exit $LASTEXITCODE
