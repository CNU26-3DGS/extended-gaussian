param(
    [string]$ProjectRoot = "",
    [string]$BuildRoot = "",
    [string]$Config = "RelWithDebInfo",
    [string]$VsDevCmd = "",
    [switch]$ZipBundle,
    [switch]$IncludeSwaptestData
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath {
    param([string]$PathValue)
    return [System.IO.Path]::GetFullPath($PathValue)
}

function Get-CMakeCacheValue {
    param(
        [string]$CachePath,
        [string]$Key
    )

    $match = Select-String -Path $CachePath -Pattern ("^{0}:[^=]*=(.*)$" -f [regex]::Escape($Key)) | Select-Object -First 1
    if ($match) {
        return $match.Matches[0].Groups[1].Value
    }
    return ""
}

function Get-BuildTreeInfo {
    param([string]$BuildDirectory)

    if (-not (Test-Path $BuildDirectory -PathType Container)) {
        throw "Build root not found: $BuildDirectory"
    }

    $cachePath = Join-Path $BuildDirectory "CMakeCache.txt"
    if (-not (Test-Path $cachePath -PathType Leaf)) {
        throw "CMakeCache.txt not found under build root: $BuildDirectory"
    }

    $generator = Get-CMakeCacheValue -CachePath $cachePath -Key "CMAKE_GENERATOR"
    $buildType = Get-CMakeCacheValue -CachePath $cachePath -Key "CMAKE_BUILD_TYPE"
    $configTypes = Get-CMakeCacheValue -CachePath $cachePath -Key "CMAKE_CONFIGURATION_TYPES"
    $projectCudaArchitectures = Get-CMakeCacheValue -CachePath $cachePath -Key "EXTENDED_GAUSSIAN_CUDA_ARCHITECTURES"
    $cudaArchitectures = $projectCudaArchitectures
    if ([string]::IsNullOrWhiteSpace($cudaArchitectures)) {
        $cudaArchitectures = Get-CMakeCacheValue -CachePath $cachePath -Key "CMAKE_CUDA_ARCHITECTURES"
    }
    $isMultiConfig = -not [string]::IsNullOrWhiteSpace($configTypes)

    if (-not $isMultiConfig -and $generator -match "Visual Studio|Xcode|Ninja Multi-Config") {
        $isMultiConfig = $true
    }

    return [pscustomobject]@{
        BuildRoot = $BuildDirectory
        CachePath = $cachePath
        Generator = $generator
        BuildType = $buildType
        IsMultiConfig = $isMultiConfig
        ProjectCudaArchitectures = $projectCudaArchitectures
        CudaArchitectures = $cudaArchitectures
    }
}

function Assert-CompatibleBuildTree {
    param(
        [pscustomobject]$BuildTreeInfo,
        [string]$RequestedConfig
    )

    if ($BuildTreeInfo.IsMultiConfig) {
        return
    }

    if ([string]::IsNullOrWhiteSpace($BuildTreeInfo.BuildType)) {
        throw ("Single-config build tree is missing CMAKE_BUILD_TYPE. BuildRoot: {0}; Generator: {1}" -f $BuildTreeInfo.BuildRoot, $BuildTreeInfo.Generator)
    }

    if ($BuildTreeInfo.BuildType -ine $RequestedConfig) {
        throw ("Single-config build tree mismatch. BuildRoot: {0}; Generator: {1}; Cached CMAKE_BUILD_TYPE: {2}; Requested -Config: {3}" -f $BuildTreeInfo.BuildRoot, $BuildTreeInfo.Generator, $BuildTreeInfo.BuildType, $RequestedConfig)
    }
}

function Test-CudaArchitecturePresent {
    param(
        [string]$ArchitectureList,
        [string]$Architecture
    )

    return $ArchitectureList -match ("(^|;){0}($|;|-)" -f [regex]::Escape($Architecture))
}

function Assert-PortableCudaArchitectures {
    param([pscustomobject]$BuildTreeInfo)

    if ([string]::IsNullOrWhiteSpace($BuildTreeInfo.CudaArchitectures)) {
        throw ("CUDA architectures are not recorded in build tree cache. BuildRoot: {0}" -f $BuildTreeInfo.BuildRoot)
    }

    $requiredArchitectures = @("75", "86", "89", "120")
    $missingArchitectures = @()
    foreach ($architecture in $requiredArchitectures) {
        if (-not (Test-CudaArchitecturePresent -ArchitectureList $BuildTreeInfo.CudaArchitectures -Architecture $architecture)) {
            $missingArchitectures += $architecture
        }
    }

    if ($missingArchitectures.Count -gt 0) {
        throw ("Portable bundle requires CUDA architectures covering Turing, Ampere, Ada, and Blackwell. BuildRoot: {0}; Recorded architectures: {1}; Missing: {2}. Reconfigure with -DEXTENDED_GAUSSIAN_CUDA_ARCHITECTURES=""75-real;86-real;89-real;120"" and rebuild." -f $BuildTreeInfo.BuildRoot, $BuildTreeInfo.CudaArchitectures, ($missingArchitectures -join ", "))
    }
}

function Resolve-VsDevCmd {
    param([string]$CurrentPath)

    if (-not [string]::IsNullOrWhiteSpace($CurrentPath)) {
        $resolvedPath = Resolve-FullPath $CurrentPath
        if (Test-Path $resolvedPath -PathType Leaf) {
            return $resolvedPath
        }
        throw "VsDevCmd.bat not found: $resolvedPath"
    }

    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw "Failed to locate VsDevCmd.bat. Pass -VsDevCmd explicitly."
}

function Invoke-VsBuild {
    param(
        [string]$VsDevCmdPath,
        [string]$WorkingDirectory,
        [string]$BuildDirectory,
        [string]$BuildConfig,
        [string]$TargetName,
        [bool]$UseMultiConfig
    )

    $buildCommand = "cmake --build `"$BuildDirectory`" --target $TargetName"
    if ($UseMultiConfig) {
        $buildCommand += " --config $BuildConfig"
    }

    $command = "`"$VsDevCmdPath`" -arch=x64 && $buildCommand"
    Push-Location $WorkingDirectory
    try {
        & cmd.exe /d /s /c $command
        if ($LASTEXITCODE -ne 0) {
            throw "Build target failed: $TargetName"
        }
    } finally {
        Pop-Location
    }
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Resolve-FullPath (Join-Path $PSScriptRoot "..\..")
} else {
    $ProjectRoot = Resolve-FullPath $ProjectRoot
}

if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $ProjectRoot "build"
}
$BuildRoot = Resolve-FullPath $BuildRoot

$buildTreeInfo = Get-BuildTreeInfo -BuildDirectory $BuildRoot
Assert-CompatibleBuildTree -BuildTreeInfo $buildTreeInfo -RequestedConfig $Config

$VsDevCmd = Resolve-VsDevCmd -CurrentPath $VsDevCmd
$packageScript = Join-Path $ProjectRoot "tools\windows\package_windows_portable_bundle.ps1"

Invoke-VsBuild -VsDevCmdPath $VsDevCmd -WorkingDirectory $ProjectRoot -BuildDirectory $BuildRoot -BuildConfig $Config -TargetName "extended_gaussianViewer_app" -UseMultiConfig $buildTreeInfo.IsMultiConfig
$buildTreeInfo = Get-BuildTreeInfo -BuildDirectory $BuildRoot
Assert-PortableCudaArchitectures -BuildTreeInfo $buildTreeInfo
Invoke-VsBuild -VsDevCmdPath $VsDevCmd -WorkingDirectory $ProjectRoot -BuildDirectory $BuildRoot -BuildConfig $Config -TargetName "extended_gaussianViewer_app_install" -UseMultiConfig $buildTreeInfo.IsMultiConfig

$packageArgs = @{
    ProjectRoot = $ProjectRoot
    Config = $Config
}
if ($ZipBundle) {
    $packageArgs["ZipBundle"] = $true
}
if ($IncludeSwaptestData) {
    $packageArgs["IncludeSwaptestData"] = $true
}

& $packageScript @packageArgs
