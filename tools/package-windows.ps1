param(
    [string]$QtRoot = $env:QT_ROOT_DIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$CompilerBin,
    [string]$CMakePath,
    [string]$InnoCompiler,
    [string]$SignToolPath,
    [string]$CertificateThumbprint,
    [string]$TimestampServer = "http://timestamp.digicert.com",
    [switch]$StageOnly,
    [switch]$PortableOnly
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($QtRoot)) { throw "Set QT_ROOT_DIR or pass -QtRoot with the Qt kit directory." }
if (!(Test-Path -LiteralPath $QtRoot -PathType Container)) { throw "Qt kit directory does not exist: $QtRoot" }
if (($SignToolPath -and !$CertificateThumbprint) -or (!$SignToolPath -and $CertificateThumbprint)) {
    throw "Pass both -SignToolPath and -CertificateThumbprint to enable signing."
}

if (!$StageOnly -and !$PortableOnly -and [string]::IsNullOrWhiteSpace($InnoCompiler)) {
    $innoCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($innoCommand) { $InnoCompiler = $innoCommand.Source }
    else {
        $defaultInno = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"
        if (Test-Path -LiteralPath $defaultInno) { $InnoCompiler = $defaultInno }
    }
}
if (!$StageOnly -and !$PortableOnly -and ([string]::IsNullOrWhiteSpace($InnoCompiler) -or !(Test-Path -LiteralPath $InnoCompiler -PathType Leaf))) {
    throw "Inno Setup 6 ISCC.exe was not found. Install Inno Setup 6 or pass -InnoCompiler."
}

$env:QT_ROOT_DIR = (Resolve-Path -LiteralPath $QtRoot).Path
if ($VcpkgRoot) { $env:VCPKG_ROOT = (Resolve-Path -LiteralPath $VcpkgRoot).Path }
$vcpkgCheckout = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $repoRoot ".tools/vcpkg" }
$vcpkgToolchain = Join-Path $vcpkgCheckout "scripts/buildsystems/vcpkg.cmake"
if (!(Test-Path -LiteralPath $vcpkgToolchain -PathType Leaf)) { throw "vcpkg toolchain was not found: $vcpkgToolchain" }
$qtBaseDirectory = Split-Path (Split-Path $env:QT_ROOT_DIR -Parent) -Parent
if ([string]::IsNullOrWhiteSpace($CMakePath)) {
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmakeCommand) { $CMakePath = $cmakeCommand.Source }
    else { $CMakePath = Join-Path $qtBaseDirectory "Tools/CMake_64/bin/cmake.exe" }
}
if (!(Test-Path -LiteralPath $CMakePath -PathType Leaf)) { throw "CMake executable was not found: $CMakePath" }
$ninjaDirectory = Join-Path $qtBaseDirectory "Tools/Ninja"
if (!(Get-Command ninja.exe -ErrorAction SilentlyContinue) -and (Test-Path (Join-Path $ninjaDirectory "ninja.exe"))) {
    $env:PATH = "$ninjaDirectory;$env:PATH"
}
$compilerDirectories = @()
if ($CompilerBin) {
    $compilerDirectories = @((Resolve-Path -LiteralPath $CompilerBin).Path)
} else {
    $compilerDirectories = @(Get-ChildItem (Join-Path $qtBaseDirectory "Tools") -Directory -Filter "mingw*" |
        Where-Object { Test-Path (Join-Path $_.FullName "bin/g++.exe") } |
        ForEach-Object { Join-Path $_.FullName "bin" })
}
if ($compilerDirectories.Count -ne 1) {
    throw "Could not select one Qt-matched MinGW compiler. Pass -CompilerBin with the matching compiler's bin directory."
}
$CompilerBin = $compilerDirectories[0]
$cCompiler = Join-Path $CompilerBin "gcc.exe"
$cxxCompiler = Join-Path $CompilerBin "g++.exe"
$resourceCompiler = Join-Path $CompilerBin "windres.exe"
foreach ($compiler in @($cCompiler, $cxxCompiler, $resourceCompiler)) {
    if (!(Test-Path -LiteralPath $compiler -PathType Leaf)) { throw "Required Qt-matched compiler tool was not found: $compiler" }
}
$env:PATH = "$CompilerBin;$env:PATH"
$qtPackagePrefix = $env:QT_ROOT_DIR.Replace('\', '/')
$cCompiler = $cCompiler.Replace('\', '/')
$cxxCompiler = $cxxCompiler.Replace('\', '/')
$resourceCompiler = $resourceCompiler.Replace('\', '/')
$buildDirectory = Join-Path $repoRoot "out/build/windows-package"
$stageDirectory = if ($PortableOnly) {
    Join-Path $repoRoot "out/portable-stage"
} else {
    Join-Path $repoRoot "out/release-stage"
}
$releaseDirectory = Join-Path $repoRoot "out/release"
$binaryCacheDirectory = Join-Path $repoRoot ".tools/vcpkg_binary_cache"
$env:VCPKG_DEFAULT_BINARY_CACHE = $binaryCacheDirectory
New-Item -ItemType Directory -Path $binaryCacheDirectory -Force | Out-Null
$version = (Select-String -Path (Join-Path $repoRoot "CMakeLists.txt") -Pattern '^project\(DevHub VERSION ([^) ]+)').Matches.Groups[1].Value
if ([string]::IsNullOrWhiteSpace($version)) { throw "Could not read the project version from CMakeLists.txt." }

Push-Location $repoRoot
try {
    & $CMakePath --preset windows-package `
        "-DCMAKE_PREFIX_PATH=$qtPackagePrefix" `
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain" `
        "-DCMAKE_C_COMPILER=$cCompiler" `
        "-DCMAKE_CXX_COMPILER=$cxxCompiler" `
        "-DCMAKE_RC_COMPILER=$resourceCompiler"
    if ($LASTEXITCODE -ne 0) { throw "CMake package configuration failed with exit code $LASTEXITCODE." }
    & $CMakePath --build --preset windows-package-release
    if ($LASTEXITCODE -ne 0) { throw "Release build failed with exit code $LASTEXITCODE." }
    if (Test-Path -LiteralPath $stageDirectory) {
        $resolvedOut = (Resolve-Path -LiteralPath (Join-Path $repoRoot "out")).Path
        $resolvedStage = (Resolve-Path -LiteralPath $stageDirectory).Path
        if (!$resolvedStage.StartsWith($resolvedOut + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase) -or
            ((Get-Item -LiteralPath $stageDirectory).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Refusing to clean an unexpected release staging directory: $resolvedStage"
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
    & $CMakePath --install $buildDirectory --prefix $stageDirectory --config Release
    if ($LASTEXITCODE -ne 0) { throw "CMake install/deployment failed with exit code $LASTEXITCODE." }
    $stagedExecutable = Join-Path $stageDirectory "bin/devhub.exe"
    if (!(Test-Path -LiteralPath $stagedExecutable)) {
        throw "The DevHub executable was not staged in bin. Refusing to create a package with a broken runtime layout."
    }
    foreach ($runtimeFile in @(
        "bin/Qt6Core.dll", "bin/Qt6Gui.dll", "bin/Qt6Network.dll",
        "bin/Qt6Sql.dll", "bin/Qt6Svg.dll", "bin/Qt6Widgets.dll",
        "bin/Qt6Qml.dll", "bin/Qt6Quick.dll", "bin/Qt6QuickControls2.dll",
        "bin/libgcc_s_seh-1.dll", "bin/libstdc++-6.dll", "bin/libwinpthread-1.dll",
        "bin/qt.conf", "plugins/platforms/qwindows.dll", "plugins/sqldrivers/qsqlite.dll",
        "qml/QtQuick/Controls/qtquickcontrols2plugin.dll",
        "qml/QtQuick/Controls/Basic/qtquickcontrols2basicstyleplugin.dll"
    )) {
        if (!(Test-Path -LiteralPath (Join-Path $stageDirectory $runtimeFile) -PathType Leaf)) {
            throw "Required portable runtime file was not staged: $runtimeFile"
        }
    }
    $configuredTriplet = (Select-String -Path (Join-Path $buildDirectory "CMakeCache.txt") `
        -Pattern '^VCPKG_TARGET_TRIPLET:STRING=(.+)$').Matches.Groups[1].Value
    if ($configuredTriplet -eq "x64-mingw-dynamic" -and
        !(Test-Path -LiteralPath (Join-Path $stageDirectory "bin/libsodium-26.dll"))) {
        throw "The dynamically linked libsodium runtime was not staged beside DevHub."
    }
    $forbidden = Get-ChildItem -LiteralPath $stageDirectory -Recurse -File | Where-Object {
        $_.Extension -in @(".pdb", ".lib", ".a", ".o", ".obj", ".ilk", ".exp") -or
        $_.Name -match '\.dll\.a$' -or $_.Name -match '^Qt6.*d\.dll$'
    }
    if ($forbidden) { throw "Development artifacts were found in the staging folder: $($forbidden.FullName -join ', ')" }

    # Launch only Qt's platform and SQLite plugins. This exits before DevHub
    # opens Local AppData, so an existing user session is left untouched.
    $originalPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $runtimeCheck = Start-Process -FilePath $stagedExecutable -ArgumentList "--check-runtime" `
            -WorkingDirectory (Split-Path $stagedExecutable -Parent) -WindowStyle Hidden -PassThru -Wait
        if ($runtimeCheck.ExitCode -ne 0) {
            throw "The staged DevHub executable did not start with only its packaged runtime (exit $($runtimeCheck.ExitCode))."
        }
    }
    finally {
        $env:PATH = $originalPath
    }

    $shareRoot = Join-Path $repoRoot ".tools/vcpkg_installed/$configuredTriplet/share"
    $qtLicenseDirectory = Join-Path $qtBaseDirectory "Licenses"
    $inventory = @()
    $notices = [System.Collections.Generic.List[string]]::new()
    $notices.Add("DevHub third-party notices")
    $notices.Add("Generated for DevHub $version. Review docs/dependencies/licenses.md before redistribution.")
    $qtVersion = Split-Path -Leaf (Split-Path $env:QT_ROOT_DIR -Parent)
    $inventory += [pscustomobject]@{
        name = "Qt"
        version = $qtVersion
        licenseFileIncluded = (Test-Path -LiteralPath $qtLicenseDirectory)
    }
    if (Test-Path -LiteralPath $shareRoot) {
        foreach ($component in Get-ChildItem -LiteralPath $shareRoot -Directory | Sort-Object Name) {
            $spdxPath = Join-Path $component.FullName "vcpkg.spdx.json"
            $licensePath = Join-Path $component.FullName "copyright"
            $metadata = $null
            if (Test-Path -LiteralPath $spdxPath) {
                try { $metadata = Get-Content -LiteralPath $spdxPath -Raw | ConvertFrom-Json } catch { $metadata = $null }
            }
            $packageName = $component.Name
            $packageVersion = "See vcpkg manifest/baseline"
            if ($metadata -and $metadata.name -and $metadata.name -match '^([^:]+):[^@]+@([^ #]+)') {
                $packageName = $Matches[1]
                $packageVersion = $Matches[2]
            }
            $inventory += [pscustomobject]@{
                name = $packageName
                version = $packageVersion
                licenseFileIncluded = (Test-Path -LiteralPath $licensePath)
            }
            if (Test-Path -LiteralPath $licensePath) {
                $notices.Add("")
                $notices.Add("===== $($component.Name) =====")
                $notices.Add((Get-Content -LiteralPath $licensePath -Raw))
            }
        }
    }
    $metadataDirectory = Join-Path $stageDirectory "licenses"
    New-Item -ItemType Directory -Path $metadataDirectory -Force | Out-Null
    if (Test-Path -LiteralPath $qtLicenseDirectory) {
        $qtLicenseDestination = Join-Path $metadataDirectory "Qt"
        New-Item -ItemType Directory -Path $qtLicenseDestination -Force | Out-Null
        Copy-Item -Path (Join-Path $qtLicenseDirectory "*") -Destination $qtLicenseDestination -Recurse -Force
    } else {
        Write-Warning "Qt's license directory was not found at $qtLicenseDirectory; verify and add the applicable notices before redistribution."
    }
    $notices.Add("")
    $notices.Add("===== DevHub license inventory and Qt distribution notes =====")
    $notices.Add((Get-Content -LiteralPath (Join-Path $repoRoot "docs/dependencies/licenses.md") -Raw))
    Set-Content -LiteralPath (Join-Path $metadataDirectory "THIRD_PARTY_NOTICES.txt") -Value $notices -Encoding utf8
    $inventory | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $metadataDirectory "dependency-inventory.json") -Encoding utf8

    if ($StageOnly) {
        Write-Host "Deployment staged and verified: $stageDirectory"
        Write-Host "Archive and installer creation skipped because -StageOnly was selected."
        return
    }

    New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null
    $stagedExecutable = Join-Path $stageDirectory "bin/devhub.exe"
    if ($SignToolPath) {
        & $SignToolPath sign /fd SHA256 /sha1 $CertificateThumbprint /tr $TimestampServer /td SHA256 $stagedExecutable
        if ($LASTEXITCODE -ne 0) { throw "Signing the application failed with exit code $LASTEXITCODE." }
    }

    $portableArchive = Join-Path $releaseDirectory "DevHub-Portable-$version-x64.zip"
    if (Test-Path -LiteralPath $portableArchive) { Remove-Item -LiteralPath $portableArchive -Force }
    [IO.Compression.ZipFile]::CreateFromDirectory($stageDirectory, $portableArchive,
        [IO.Compression.CompressionLevel]::Optimal, $false)

    if (!$PortableOnly) {
        & $InnoCompiler "/DAppVersion=$version" (Join-Path $repoRoot "packaging/DevHub.iss")
        if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed with exit code $LASTEXITCODE." }
        $installer = Join-Path $releaseDirectory "DevHub-Setup-$version-x64.exe"
        if ($SignToolPath) {
            & $SignToolPath sign /fd SHA256 /sha1 $CertificateThumbprint /tr $TimestampServer /td SHA256 $installer
            if ($LASTEXITCODE -ne 0) { throw "Signing the installer failed with exit code $LASTEXITCODE." }
        }
    }

    $checksumFile = Join-Path $releaseDirectory "SHA256SUMS.txt"
    Get-ChildItem -LiteralPath $releaseDirectory -File | Where-Object Name -ne "SHA256SUMS.txt" |
        Get-FileHash -Algorithm SHA256 |
        ForEach-Object { "{0} *{1}" -f $_.Hash.ToLowerInvariant(), (Split-Path $_.Path -Leaf) } |
        Set-Content -LiteralPath $checksumFile -Encoding ascii
    Write-Host "Portable package ready: $portableArchive"
    if (!$PortableOnly) { Write-Host "Installer ready: $installer" }
}
finally {
    Pop-Location
}
