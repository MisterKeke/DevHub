param(
    [Parameter(Mandatory = $true)]
    [string] $TestExecutable
)

$ErrorActionPreference = 'Stop'
$resolvedExecutable = (Resolve-Path -LiteralPath $TestExecutable).Path
$previousSetting = $env:DEVHUB_RUN_ARGON2_BENCHMARK
try {
    $env:DEVHUB_RUN_ARGON2_BENCHMARK = '1'
    & $resolvedExecutable productionProfileMeasurement
    if ($LASTEXITCODE -ne 0) {
        throw "Argon2id measurement failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:DEVHUB_RUN_ARGON2_BENCHMARK = $previousSetting
}
