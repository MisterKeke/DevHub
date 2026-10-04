param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$ReportPath
)

$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$ReportPath = [IO.Path]::GetFullPath($ReportPath)
if (Test-Path -LiteralPath $ReportPath) { Remove-Item -LiteralPath $ReportPath }
$variables = @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML_IMPORT_PATH',
    'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM', 'QT_QUICK_CONTROLS_STYLE',
    'QT_QUICK_CONTROLS_CONF', 'QT_STYLE_OVERRIDE')
$original = @{}
foreach ($name in $variables) { $original[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    # Start-Process joins argument arrays; quote the entire report argument for
    # workspace paths containing spaces. The diagnostic mode never opens user data.
    $arguments = '--check-runtime "--check-runtime-report={0}"' -f $ReportPath
    $runtime = Start-Process -FilePath $Executable -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $Executable -Parent) -WindowStyle Hidden -PassThru
    if (!$runtime.WaitForExit(20000)) {
        $runtime.Kill()
        throw 'Packaged runtime check timed out after 20 seconds.'
    }
    $diagnostics = if (Test-Path -LiteralPath $ReportPath) { Get-Content -LiteralPath $ReportPath -Raw } else { 'No runtime diagnostic report was written.' }
    if ($runtime.ExitCode -ne 0) { throw "Packaged runtime check failed (exit $($runtime.ExitCode)):`n$diagnostics" }
    if ($diagnostics -notmatch 'PASS: packaged runtime') { throw "Packaged runtime check did not confirm success:`n$diagnostics" }
    Write-Host $diagnostics
}
finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $original[$name], 'Process') }
}
