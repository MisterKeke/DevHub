param(
    [Parameter(Mandatory = $true)][string] $TestExecutable,
    [Parameter(Mandatory = $true)][string] $CodexExecutable,
    [Parameter(Mandatory = $true)][string] $CodexHome,
    [Parameter(Mandatory = $true)][ValidateSet('0.160.1', '0.162.0-alpha.17.2')][string] $ExpectedVersion,
    [Parameter(Mandatory = $true)][string] $QtRoot,
    [Parameter(Mandatory = $true)][string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
$liveTestExecutable = (Resolve-Path -LiteralPath $TestExecutable).Path
$liveCodexExecutable = (Resolve-Path -LiteralPath $CodexExecutable).Path
$liveCodexHome = (Resolve-Path -LiteralPath $CodexHome).Path
$liveQtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$liveDefaultHome = [IO.Path]::GetFullPath((Join-Path $env:USERPROFILE '.codex')).TrimEnd('\', '/')
$liveSelectedHome = $liveCodexHome.TrimEnd('\', '/')
if ($liveSelectedHome.Equals($liveDefaultHome, [StringComparison]::OrdinalIgnoreCase) -or
    $liveSelectedHome.StartsWith($liveDefaultHome + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $liveDefaultHome.StartsWith($liveSelectedHome + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use a separately authenticated disposable Codex home, outside the default profile.'
}
$liveMarker = Join-Path $liveCodexHome '.devhub-disposable-codex-profile'
if (!(Test-Path -LiteralPath $liveMarker -PathType Leaf) -or
    [Convert]::ToBase64String([IO.File]::ReadAllBytes($liveMarker)) -ne
        [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("DevHub live integration disposable profile`n"))) {
    throw 'The disposable profile marker is missing or invalid. Follow assistant-live-integration.md.'
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$liveLog = Join-Path (Resolve-Path -LiteralPath $OutputDirectory).Path 'live-tests.txt'
$liveEnvironment = @{}
foreach ($name in @('PATH', 'QT_PLUGIN_PATH', 'CODEX_HOME', 'DEVHUB_RUN_LIVE_ASSISTANT_TESTS',
    'DEVHUB_LIVE_CODEX_EXE', 'DEVHUB_LIVE_CODEX_HOME', 'DEVHUB_LIVE_DISPOSABLE_PROFILE_ACK')) {
    $liveEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:PATH = (Join-Path $liveQtRoot 'bin') + ';' + $env:PATH
    $env:QT_PLUGIN_PATH = Join-Path $liveQtRoot 'plugins'
    $env:CODEX_HOME = $liveCodexHome
    $liveVersionOutput = @(& $liveCodexExecutable --version)
    if ($LASTEXITCODE -ne 0 -or ($liveVersionOutput -join "`n").Trim() -ne "codex-cli $ExpectedVersion") {
        throw "The selected native executable must report codex-cli $ExpectedVersion."
    }
    $env:DEVHUB_RUN_LIVE_ASSISTANT_TESTS = 'I_ACKNOWLEDGE_REAL_CODEX_INFERENCE'
    $env:DEVHUB_LIVE_CODEX_EXE = $liveCodexExecutable
    $env:DEVHUB_LIVE_CODEX_HOME = $liveCodexHome
    $env:DEVHUB_LIVE_DISPOSABLE_PROFILE_ACK = 'I_CREATED_THIS_DISPOSABLE_PROFILE'
    if (Test-Path -LiteralPath $liveLog) { Remove-Item -LiteralPath $liveLog }
    & $liveTestExecutable -o "${liveLog},txt"
    $liveExitCode = $LASTEXITCODE
    if (!(Test-Path -LiteralPath $liveLog)) { throw 'The live test did not produce its sanitized report.' }
    $liveResults = Get-Content -LiteralPath $liveLog -Raw
    $liveCompletion = 'DEVHUB_LIVE_RESULT version=\s*' + [regex]::Escape($ExpectedVersion) +
        '\s+handshake=passed account=chatgpt profile=accepted\s+stream=completed stop=interrupted resume=passed' +
        '\s+duplicate_history=none read_tool=\s*passed\s+read_lifecycle=\s*passed'
    if ($liveExitCode -ne 0 -or $liveResults -notmatch 'Totals: \d+ passed, 0 failed, 0 skipped' -or
        $liveResults -notmatch $liveCompletion) {
        Get-Content -LiteralPath $liveLog
        throw "Live Assistant verification for $ExpectedVersion is incomplete or failed. See $liveLog."
    }
    Write-Output "Live Assistant verification passed for $ExpectedVersion; $liveLog"
}
finally {
    foreach ($name in $liveEnvironment.Keys) {
        if ($null -eq $liveEnvironment[$name]) {
            Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($name, $liveEnvironment[$name], 'Process')
        }
    }
}
