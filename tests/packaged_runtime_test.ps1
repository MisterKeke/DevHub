param(
    [Parameter(Mandatory = $true)][string]$StageDirectory,
    [Parameter(Mandatory = $true)][string]$ReportDirectory
)

$ErrorActionPreference = 'Stop'
$runtimeRoot = (Resolve-Path -LiteralPath $StageDirectory).Path
$ReportDirectory = [IO.Path]::GetFullPath($ReportDirectory)
New-Item -ItemType Directory -Path $ReportDirectory -Force | Out-Null
$checker = Join-Path $PSScriptRoot '../tools/check-packaged-runtime.ps1'
$executable = Join-Path $runtimeRoot 'bin/devhub.exe'
& $checker -Executable $executable -ReportPath (Join-Path $ReportDirectory 'complete.txt')

# Run only against an isolated install/staging directory. Restore each plugin
# even if the assertion fails; missing DLLs must not be masked by the Qt SDK.
$cases = @(
    @{ name = 'missing-qml-plugin'; path = 'qml/QtQuick/Layouts/qquicklayoutsplugin.dll'; diagnostic = 'QtQuick.Layouts' },
    @{ name = 'missing-sqlite-plugin'; path = 'plugins/sqldrivers/qsqlite.dll'; diagnostic = 'SQLite runtime failed' }
)
foreach ($case in $cases) {
    $plugin = (Resolve-Path -LiteralPath (Join-Path $runtimeRoot $case.path)).Path
    if (!$plugin.StartsWith($runtimeRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Plugin resolved outside the runtime under test: $plugin"
    }
    $disabled = $plugin + '.runtime-check-disabled'
    if (Test-Path -LiteralPath $disabled) { throw "Temporary plugin path already exists: $disabled" }
    $report = Join-Path $ReportDirectory ($case.name + '.txt')
    Move-Item -LiteralPath $plugin -Destination $disabled
    try {
        $rejected = $false
        try { & $checker -Executable $executable -ReportPath $report }
        catch {
            $rejected = $true
            if (!(Test-Path -LiteralPath $report)) { throw }
            $diagnostics = Get-Content -LiteralPath $report -Raw
            if (!$diagnostics.Contains($case.diagnostic) -or !$diagnostics.Contains('FAIL: packaged runtime')) {
                throw "The incomplete runtime failed without the expected diagnostic:`n$diagnostics"
            }
        }
        if (!$rejected) { throw "Runtime check accepted a package without $($case.path)" }
        Write-Host "PASS: rejected $($case.name) with diagnostic report"
    }
    finally { Move-Item -LiteralPath $disabled -Destination $plugin }
}
