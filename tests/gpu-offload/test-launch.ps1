# Windows-only: validate launch isolation without Docker/network/file mutations.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$launcher=Join-Path $root 'scripts/gpu-offload-run.ps1'
$keys=@{streaming='MANHUNT_GL_PERSISTENT';ubo='MANHUNT_GL_UBO';state='MANHUNT_GL_STATE_CACHE';cmp='MANHUNT_GL_CMP';queue='MANHUNT_GL_QUEUE_PACK';hrtf='MANHUNT_APU_FAST_HRTF';cs='MANHUNT_CS_CACHE';contig='MANHUNT_CONTIG_RECLAIM'}
$hash=$null
foreach($mode in @('rollback','cmp','streaming','ubo','state','queue','hrtf','cs','contig','all')) {
    $manifest=(& $launcher -Experiment $mode -DryRun) | ConvertFrom-Json
    foreach($path in $keys.Keys) {
        $on=[int]($mode -eq $path -or $mode -eq 'all')
        if($manifest.arguments -notcontains "$($keys[$path])=$on") {throw "wrong flags: $mode / $path"}
    }
    if($manifest.arguments -notcontains 'RECOMP_RENDER_PROFILE=0' -or $manifest.arguments -notcontains 'MANHUNT_AMBIENT_LIGHT=-1') {throw "confounded $mode"}
    if($hash -and $hash -ne $manifest.nro_sha256) {throw 'experiments use different binaries'}
    $hash=$manifest.nro_sha256
    Write-Output "PASS controlled launch $mode"
}
$culture=[System.Globalization.CultureInfo]::CurrentCulture
try {
    [System.Globalization.CultureInfo]::CurrentCulture=[System.Globalization.CultureInfo]::GetCultureInfo('es-CL')
    $manifest=(& $launcher -AmbientLight 0.33 -DryRun) | ConvertFrom-Json
    if($manifest.arguments -notcontains 'MANHUNT_AMBIENT_LIGHT=0.33') {throw 'ambient uses locale decimal separator'}
} finally {[System.Globalization.CultureInfo]::CurrentCulture=$culture}
Write-Output 'PASS invariant numeric launch arguments'
