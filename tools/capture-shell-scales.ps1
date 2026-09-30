param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,
    [Parameter(Mandatory = $true)]
    [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$previousScale = $env:QT_SCALE_FACTOR
try {
    foreach ($scale in @('1.0', '1.25', '1.5', '2.0')) {
        $env:QT_SCALE_FACTOR = $scale
        $percentage = [int]([double]::Parse($scale, [Globalization.CultureInfo]::InvariantCulture) * 100)
        $capturePath = Join-Path $OutputDirectory "shell-${percentage}pct.png"
        & $resolvedExecutable "--capture-shell=$capturePath"
        if ($LASTEXITCODE -ne 0) {
            throw "Shell capture at ${percentage}% failed with exit code $LASTEXITCODE."
        }
    }
}
finally {
    $env:QT_SCALE_FACTOR = $previousScale
}
