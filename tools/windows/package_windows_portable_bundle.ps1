param(
    [string]$ProjectRoot = "",
    [string]$InstallRoot = "",
    [string]$ManifestRoot = "",
    [string]$OutputRoot = "",
    [string]$SwaptestRoot = "",
    [string]$Config = "",
    [switch]$AllowFallbackExe,
    [switch]$IncludeSwaptestData,
    [switch]$ZipBundle
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath {
    param([string]$PathValue)
    return [System.IO.Path]::GetFullPath($PathValue)
}

function Get-ExecutableCandidates {
    param(
        [string]$RequestedConfig,
        [switch]$AllowFallback
    )

    $defaultCandidates = @(
        "extended_gaussianViewer_app_rwdi.exe",
        "extended_gaussianViewer_app.exe",
        "extended_gaussianViewer_app_msr.exe",
        "extended_gaussianViewer_app_d.exe"
    )

    if ([string]::IsNullOrWhiteSpace($RequestedConfig)) {
        return $defaultCandidates
    }

    switch ($RequestedConfig.ToLowerInvariant()) {
        "debug" { $primaryCandidate = "extended_gaussianViewer_app_d.exe" }
        "relwithdebinfo" { $primaryCandidate = "extended_gaussianViewer_app_rwdi.exe" }
        "release" { $primaryCandidate = "extended_gaussianViewer_app.exe" }
        "minsizerel" { $primaryCandidate = "extended_gaussianViewer_app_msr.exe" }
        default { throw "Unsupported config for executable selection: $RequestedConfig" }
    }

    if (-not $AllowFallback) {
        return @($primaryCandidate)
    }

    return @($primaryCandidate) + @($defaultCandidates | Where-Object { $_ -ne $primaryCandidate })
}

function Get-PowerShellHostPath {
    $processPath = (Get-Process -Id $PID).Path
    if ([string]::IsNullOrWhiteSpace($processPath) -or -not (Test-Path $processPath -PathType Leaf)) {
        throw "Failed to resolve the current PowerShell host path."
    }
    return $processPath
}

function Invoke-PortablePreflight {
    param(
        [string]$ScriptPath,
        [string]$AppRoot,
        [string]$ExpectedViewerExeName = "",
        [string]$ManifestPath = "",
        [switch]$SkipDataCheck,
        [switch]$SkipGpuCheck
    )

    $hostPath = Get-PowerShellHostPath
    $arguments = @(
        "-ExecutionPolicy", "Bypass",
        "-File", $ScriptPath,
        "-AppRoot", $AppRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($ExpectedViewerExeName)) {
        $arguments += @("-ExpectedViewerExeName", $ExpectedViewerExeName)
    }

    if ($SkipGpuCheck) {
        $arguments += "-SkipGpuCheck"
    }

    if ($SkipDataCheck) {
        $arguments += "-SkipDataCheck"
    } elseif (-not [string]::IsNullOrWhiteSpace($ManifestPath)) {
        $arguments += @("-ManifestPath", $ManifestPath)
    }

    & $hostPath @arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("Portable bundle preflight failed with exit code {0}." -f $LASTEXITCODE)
    }
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Resolve-FullPath (Join-Path $PSScriptRoot "..\..")
} else {
    $ProjectRoot = Resolve-FullPath $ProjectRoot
}

if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = Join-Path $ProjectRoot "install"
}
$InstallRoot = Resolve-FullPath $InstallRoot

if ([string]::IsNullOrWhiteSpace($ManifestRoot)) {
    $ManifestRoot = Join-Path $ProjectRoot "manifests"
}
$ManifestRoot = Resolve-FullPath $ManifestRoot

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ProjectRoot "build\windows-portable-bundle"
}
$OutputRoot = Resolve-FullPath $OutputRoot

$BundleLauncherPath = Join-Path $ProjectRoot "tools\windows\run_portable_bundle.cmd"
$RuntimeCheckPath = Join-Path $ProjectRoot "tools\windows\check_windows_runtime.ps1"
if (-not (Test-Path $InstallRoot -PathType Container)) {
    throw "Install root not found: $InstallRoot"
}
if (-not (Test-Path $ManifestRoot -PathType Container)) {
    throw "Manifest root not found: $ManifestRoot"
}
if (-not (Test-Path $BundleLauncherPath -PathType Leaf)) {
    throw "Bundle launcher not found: $BundleLauncherPath"
}
if (-not (Test-Path $RuntimeCheckPath -PathType Leaf)) {
    throw "Runtime check script not found: $RuntimeCheckPath"
}

if ([string]::IsNullOrWhiteSpace($SwaptestRoot)) {
    $SwaptestRoot = Join-Path $ProjectRoot "swaptest"
}
$SwaptestRoot = Resolve-FullPath $SwaptestRoot

$exeCandidates = Get-ExecutableCandidates -RequestedConfig $Config -AllowFallback:$AllowFallbackExe

$selectedExe = $null
foreach ($candidate in $exeCandidates) {
    $candidatePath = Join-Path $InstallRoot ("bin\" + $candidate)
    if (Test-Path $candidatePath -PathType Leaf) {
        $selectedExe = $candidate
        break
    }
}

if (-not $selectedExe) {
    throw "No installed viewer executable found under $InstallRoot\bin"
}

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$bundleRoot = Join-Path $OutputRoot "extended_gaussian-windows-portable"
if (Test-Path $bundleRoot) {
    Remove-Item -LiteralPath $bundleRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $bundleRoot | Out-Null

Copy-Item -LiteralPath $InstallRoot -Destination (Join-Path $bundleRoot "install") -Recurse
Copy-Item -LiteralPath $ManifestRoot -Destination (Join-Path $bundleRoot "manifests") -Recurse
Copy-Item -LiteralPath $RuntimeCheckPath -Destination (Join-Path $bundleRoot "check_windows_runtime.ps1")

$swaptestDir = Join-Path $bundleRoot "swaptest"
if ($IncludeSwaptestData -and (Test-Path $SwaptestRoot -PathType Container)) {
    Copy-Item -LiteralPath $SwaptestRoot -Destination $swaptestDir -Recurse
} else {
    New-Item -ItemType Directory -Path $swaptestDir | Out-Null
    @"
Place user-provided Gaussian model data here if you want the sample manifests to work without editing.

Expected example layout:
swaptest\mc_small_aerial_c36\cells\cell0
"@ | Set-Content -LiteralPath (Join-Path $swaptestDir "README.txt") -Encoding ASCII
}

Copy-Item -LiteralPath $BundleLauncherPath -Destination (Join-Path $bundleRoot "run_extended_gaussian_viewer.cmd")
Set-Content -LiteralPath (Join-Path $bundleRoot "selected_viewer_exe.txt") -Value $selectedExe -Encoding ASCII

$bundleInstallRoot = Join-Path $bundleRoot "install"
$bundleRuntimeCheckPath = Join-Path $bundleRoot "check_windows_runtime.ps1"
$bundleSampleManifestPath = Join-Path $bundleRoot "manifests\mc_small_aerial_c36_neighbors_3x3.json"
$bundleSampleDataRoot = Join-Path $bundleRoot "swaptest\mc_small_aerial_c36"

Invoke-PortablePreflight -ScriptPath $bundleRuntimeCheckPath -AppRoot $bundleInstallRoot -ExpectedViewerExeName $selectedExe -SkipDataCheck -SkipGpuCheck

$ranFullPreflight = $false
if ((Test-Path $bundleSampleManifestPath -PathType Leaf) -and (Test-Path $bundleSampleDataRoot -PathType Container)) {
    Invoke-PortablePreflight -ScriptPath $bundleRuntimeCheckPath -AppRoot $bundleInstallRoot -ExpectedViewerExeName $selectedExe -ManifestPath $bundleSampleManifestPath -SkipGpuCheck
    $ranFullPreflight = $true
}

Write-Output ("Bundle created at: " + $bundleRoot)
Write-Output ("Viewer executable: " + $selectedExe)
if (-not [string]::IsNullOrWhiteSpace($Config)) {
    Write-Output ("Requested config: " + $Config)
}
if ($IncludeSwaptestData -and (Test-Path $SwaptestRoot -PathType Container)) {
    Write-Output ("Included dataset root: " + $SwaptestRoot)
} else {
    Write-Output "Bundle does not include swaptest data."
}
Write-Output "Runtime-only preflight passed for the bundled install tree."
if ($ranFullPreflight) {
    Write-Output "Full preflight passed for the bundled sample manifest."
} else {
    Write-Output "Skipped full preflight because the bundled sample manifest or sample data root is missing."
}
Write-Output "Package-time preflight skipped NVIDIA GPU detection. Run bundle-side preflight on the target PC for hardware validation."

if ($ZipBundle) {
    $zipPath = Join-Path $OutputRoot "extended_gaussian-windows-portable.zip"
    if (Test-Path $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Compress-Archive -Path (Join-Path $bundleRoot "*") -DestinationPath $zipPath
    Write-Output ("Zip created at: " + $zipPath)
}
