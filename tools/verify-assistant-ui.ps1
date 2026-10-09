param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,
    [Parameter(Mandatory = $true)]
    [string] $QtRoot,
    [Parameter(Mandatory = $true)]
    [string] $OutputDirectory,
    [string] $ExpectedQtVersion = '6.8.3'
)

$ErrorActionPreference = 'Stop'
$assistantExecutable = (Resolve-Path -LiteralPath $Executable).Path
$assistantQtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$assistantOutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$assistantPreviousEnvironment = @{}
foreach ($name in @('PATH', 'QT_QPA_PLATFORM', 'QT_PLUGIN_PATH', 'QT_SCALE_FACTOR', 'DEVHUB_ASSISTANT_SNAPSHOT')) {
    $assistantPreviousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    $env:PATH = (Join-Path $assistantQtRoot 'bin') + ';' + $env:PATH
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_PLUGIN_PATH = Join-Path $assistantQtRoot 'plugins'
    foreach ($scale in @('1.0', '1.25', '1.5', '2.0')) {
        $env:QT_SCALE_FACTOR = $scale
        $percentage = [int]([double]::Parse($scale, [Globalization.CultureInfo]::InvariantCulture) * 100)
        $env:DEVHUB_ASSISTANT_SNAPSHOT = Join-Path $assistantOutputDirectory "scale-${percentage}.png"
        $assistantLog = Join-Path $assistantOutputDirectory "scale-${percentage}-tests.txt"
        & $assistantExecutable -o "${assistantLog},txt"
        if ($LASTEXITCODE -ne 0) {
            if (Test-Path -LiteralPath $assistantLog) { Get-Content -LiteralPath $assistantLog }
            throw "Assistant UI verification at ${percentage}% failed with exit code $LASTEXITCODE."
        }
        $assistantResults = Get-Content -LiteralPath $assistantLog -Raw
        if ($assistantResults -notmatch ('Qt ' + [regex]::Escape($ExpectedQtVersion) + ' \(')) {
            throw "The test did not run with the expected Qt $ExpectedQtVersion runtime."
        }
        if ($assistantResults -notmatch 'Totals: \d+ passed, 0 failed, 0 skipped' -or
            !(Test-Path -LiteralPath $env:DEVHUB_ASSISTANT_SNAPSHOT)) {
            throw "Incomplete Assistant UI verification at ${percentage}%."
        }
        Write-Output "Qt $ExpectedQtVersion at ${percentage}%: all UI checks passed; $assistantLog"
    }
}
finally {
    foreach ($name in $assistantPreviousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $assistantPreviousEnvironment[$name], 'Process')
    }
}
