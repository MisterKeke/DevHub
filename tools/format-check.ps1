$ErrorActionPreference = 'Stop'
$files = Get-ChildItem -Path src, tests -Recurse -File -Include *.cpp, *.h, *.hpp
if (-not $files) {
    Write-Host 'No C++ files found.'
    exit 0
}

$failed = $false
foreach ($file in $files) {
    & clang-format --dry-run --Werror $file.FullName
    if ($LASTEXITCODE -ne 0) {
        $failed = $true
    }
}

if ($failed) {
    exit 1
}
