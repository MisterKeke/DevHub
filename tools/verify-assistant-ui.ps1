param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,
    [Parameter(Mandatory = $true)]
    [string] $QtRoot,
    [Parameter(Mandatory = $true)]
    [string] $OutputDirectory,
    [string] $ExpectedQtVersion = '6.11.2'
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
    # Native account/profile probes are deliberately opt-in and are not DPI/UI
    # release checks. Select every other function, still requiring zero skips.
    $assistantFunctions = @(& $assistantExecutable -functions)
    if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate Assistant UI test functions.' }
    $assistantOfflineFunctions = @($assistantFunctions | ForEach-Object {
        if ($_ -match '^([A-Za-z_][A-Za-z0-9_]*)\(\)$' -and
            $Matches[1] -ne 'installedNativeReconnectWithVisibleChat') { $Matches[1] }
    })
    if ($assistantOfflineFunctions.Count -eq 0) { throw 'No offline Assistant UI tests were discovered.' }
    foreach ($scale in @('1.0', '1.25', '1.5', '2.0')) {
        $env:QT_SCALE_FACTOR = $scale
        $percentage = [int]([double]::Parse($scale, [Globalization.CultureInfo]::InvariantCulture) * 100)
        $env:DEVHUB_ASSISTANT_SNAPSHOT = Join-Path $assistantOutputDirectory "scale-${percentage}.png"
        $assistantLog = Join-Path $assistantOutputDirectory "scale-${percentage}-tests.txt"
        # A previous run's image must not satisfy this run's snapshot gate.
        if (Test-Path -LiteralPath $env:DEVHUB_ASSISTANT_SNAPSHOT) {
            Remove-Item -LiteralPath $env:DEVHUB_ASSISTANT_SNAPSHOT
        }
        & $assistantExecutable @assistantOfflineFunctions -o "${assistantLog},txt"
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
            Get-Content -LiteralPath $assistantLog
            throw "Incomplete Assistant UI verification at ${percentage}%: require zero failures/skips and a fresh snapshot. See $assistantLog."
        }
        Write-Output "Qt $ExpectedQtVersion at ${percentage}%: all UI checks passed; $assistantLog"
    }
}
finally {
    foreach ($name in $assistantPreviousEnvironment.Keys) {
        if ($null -eq $assistantPreviousEnvironment[$name]) {
            Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($name, $assistantPreviousEnvironment[$name], 'Process')
        }
    }
}
