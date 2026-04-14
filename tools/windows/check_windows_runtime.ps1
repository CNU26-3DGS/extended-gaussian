param(
    [string]$AppRoot = "",
    [string]$ManifestPath = "",
    [string]$ExpectedViewerExeName = "",
    [switch]$SkipDataCheck,
    [switch]$SkipGpuCheck
)

$ErrorActionPreference = "Stop"

$ExitCodeRuntimeFailure = 1
$ExitCodeMissingAssets = 2
$ExitCodeManifestFailure = 3

function Resolve-FullPath {
    param([string]$PathValue)
    return [System.IO.Path]::GetFullPath($PathValue)
}

function Fail-ManifestValidation {
    param([string]$Message)

    Write-Warning $Message
    exit $ExitCodeManifestFailure
}

function Get-ViewerExecutableCandidates {
    return @(
        "extended_gaussianViewer_app_rwdi.exe",
        "extended_gaussianViewer_app.exe",
        "extended_gaussianViewer_app_msr.exe",
        "extended_gaussianViewer_app_d.exe"
    )
}

function Resolve-SelectedViewerExeName {
    param([string]$AppRootValue)

    $candidateRoots = @(
        $PSScriptRoot,
        (Resolve-FullPath (Join-Path $AppRootValue "..")),
        (Resolve-FullPath (Join-Path $PSScriptRoot ".."))
    ) | Select-Object -Unique

    foreach ($root in $candidateRoots) {
        $markerPath = Join-Path $root "selected_viewer_exe.txt"
        if (-not (Test-Path $markerPath -PathType Leaf)) {
            continue
        }

        $markerValue = (Get-Content -LiteralPath $markerPath -TotalCount 1).Trim()
        if (-not [string]::IsNullOrWhiteSpace($markerValue)) {
            return $markerValue
        }
    }

    return ""
}

function Resolve-DefaultAppRoot {
    $candidateRoots = @(
        $PSScriptRoot,
        (Join-Path $PSScriptRoot "install"),
        (Join-Path $PSScriptRoot ".."),
        (Join-Path $PSScriptRoot "..\install"),
        (Join-Path $PSScriptRoot "..\.."),
        (Join-Path $PSScriptRoot "..\..\install"),
        (Join-Path $PSScriptRoot "..\..\.."),
        (Join-Path $PSScriptRoot "..\..\..\install")
    ) | ForEach-Object { Resolve-FullPath $_ } | Select-Object -Unique

    foreach ($candidate in $candidateRoots) {
        foreach ($viewerExeName in (Get-ViewerExecutableCandidates)) {
            if (Test-Path (Join-Path $candidate ("bin\" + $viewerExeName)) -PathType Leaf) {
                return $candidate
            }
        }
    }

    throw "Failed to resolve install root near $PSScriptRoot"
}

function Resolve-ManifestPath {
    param(
        [string]$AppRootValue,
        [string]$CurrentManifestPath
    )

    if (-not [string]::IsNullOrWhiteSpace($CurrentManifestPath)) {
        return Resolve-FullPath $CurrentManifestPath
    }

    $candidateRoots = @(
        (Resolve-FullPath (Join-Path $AppRootValue "..")),
        (Resolve-FullPath (Join-Path $PSScriptRoot ".")),
        (Resolve-FullPath (Join-Path $PSScriptRoot "..\..")),
        (Resolve-FullPath (Join-Path $PSScriptRoot "..\..\.."))
    ) | Select-Object -Unique

    foreach ($root in $candidateRoots) {
        $candidate = Join-Path $root "manifests\mc_small_aerial_c36_neighbors_3x3.json"
        $sampleDataRoot = Join-Path $root "swaptest\mc_small_aerial_c36"
        if ((Test-Path $candidate -PathType Leaf) -and (Test-Path $sampleDataRoot -PathType Container)) {
            return $candidate
        }
    }

    return ""
}

function Get-ExpectedRuntimePatterns {
    param([string]$ViewerExeName)

    $expectedSuffix = ""
    if ($ViewerExeName -like "*_d.exe") {
        $expectedSuffix = "_d"
    } elseif ($ViewerExeName -like "*_rwdi.exe") {
        $expectedSuffix = "_rwdi"
    } elseif ($ViewerExeName -like "*_msr.exe") {
        $expectedSuffix = "_msr"
    }

    $patterns = New-Object System.Collections.Generic.List[string]
    $patterns.Add(("extended_gaussian{0}.dll" -f $expectedSuffix))
    $patterns.Add(("sibr_view{0}.dll" -f $expectedSuffix))
    $patterns.Add(("sibr_renderer{0}.dll" -f $expectedSuffix))
    $patterns.Add(("sibr_system{0}.dll" -f $expectedSuffix))
    $patterns.Add(("xatlas{0}.dll" -f $expectedSuffix))
    $patterns.Add("assimp-vc140-mt.dll")
    $patterns.Add("embree3.dll")
    $patterns.Add("opencv_core*.dll")
    $patterns.Add("cudart64_*.dll")

    if ($expectedSuffix -eq "_d") {
        $patterns.Add("glew32d.dll")
    } else {
        $patterns.Add("glew32.dll")
    }

    $patterns.Add("vcruntime140.dll")
    return $patterns
}

$manifestPathProvided = -not [string]::IsNullOrWhiteSpace($ManifestPath)

if ([string]::IsNullOrWhiteSpace($AppRoot)) {
    $AppRoot = Resolve-DefaultAppRoot
} else {
    $AppRoot = Resolve-FullPath $AppRoot
}

$binDir = Join-Path $AppRoot "bin"
if (-not (Test-Path $binDir -PathType Container)) {
    throw "Install bin directory not found: $binDir"
}

if (-not [string]::IsNullOrWhiteSpace($ExpectedViewerExeName)) {
    $viewerExe = Join-Path $binDir $ExpectedViewerExeName
    if (-not (Test-Path $viewerExe -PathType Leaf)) {
        Fail-ManifestValidation -Message ("Expected viewer executable not found: " + $viewerExe)
    }
} else {
    $selectedViewerExeName = Resolve-SelectedViewerExeName -AppRootValue $AppRoot
    if (-not [string]::IsNullOrWhiteSpace($selectedViewerExeName)) {
        $viewerExe = Join-Path $binDir $selectedViewerExeName
    } else {
        $viewerExe = Get-ViewerExecutableCandidates | ForEach-Object { Join-Path $binDir $_ } | Where-Object { Test-Path $_ -PathType Leaf } | Select-Object -First 1
    }
}

if (-not $viewerExe -or -not (Test-Path $viewerExe -PathType Leaf)) {
    throw "No viewer executable found under $binDir"
}

$viewerExeName = Split-Path -Leaf $viewerExe

if ($manifestPathProvided) {
    $ManifestPath = Resolve-FullPath $ManifestPath
    if (-not (Test-Path $ManifestPath -PathType Leaf)) {
        Fail-ManifestValidation -Message ("Manifest file not found: " + $ManifestPath)
    }
}

if (-not $SkipDataCheck -and -not $manifestPathProvided) {
    $ManifestPath = Resolve-ManifestPath -AppRootValue $AppRoot -CurrentManifestPath $ManifestPath
}

$requiredPatterns = Get-ExpectedRuntimePatterns -ViewerExeName $viewerExeName

$missingRuntime = New-Object System.Collections.Generic.List[string]
foreach ($pattern in $requiredPatterns) {
    $matches = Get-ChildItem -Path $binDir -Filter $pattern -ErrorAction SilentlyContinue
    if (-not $matches) {
        $missingRuntime.Add($pattern)
    }
}

$gpuNames = @()
if (-not $SkipGpuCheck) {
    try {
        $gpuNames = Get-CimInstance Win32_VideoController | Where-Object { $_.Name -match "NVIDIA" } | ForEach-Object { $_.Name }
    } catch {
        $gpuNames = @()
    }
}

$manifest = $null
$assetProperties = @()
$missingAssets = New-Object System.Collections.Generic.List[string]
if (-not $SkipDataCheck -and -not [string]::IsNullOrWhiteSpace($ManifestPath) -and (Test-Path $ManifestPath -PathType Leaf)) {
    $manifestDir = Split-Path -Parent $ManifestPath
    try {
        $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
        Fail-ManifestValidation -Message ("Failed to parse manifest JSON: " + $ManifestPath)
    }

    if ($null -eq $manifest.assets -or $manifest.assets -isnot [pscustomobject]) {
        Fail-ManifestValidation -Message ("Manifest assets section must be an object: " + $ManifestPath)
    }

    $assetProperties = @($manifest.assets.PSObject.Properties)
    foreach ($assetProperty in $assetProperties) {
        $modelDir = $assetProperty.Value.model_dir
        if ([string]::IsNullOrWhiteSpace($modelDir)) {
            continue
        }

        if ([System.IO.Path]::IsPathRooted($modelDir)) {
            $resolvedModelDir = Resolve-FullPath $modelDir
        } else {
            $resolvedModelDir = Resolve-FullPath (Join-Path $manifestDir $modelDir)
        }

        if (-not (Test-Path $resolvedModelDir -PathType Container)) {
            $missingAssets.Add(($assetProperty.Name + " -> " + $resolvedModelDir))
        }
    }
}

Write-Output ("Viewer executable: " + $viewerExe)
Write-Output ("Install root: " + $AppRoot)

if (-not [string]::IsNullOrWhiteSpace($ManifestPath)) {
    Write-Output ("Manifest path: " + $ManifestPath)
}

if ($SkipGpuCheck) {
    Write-Output "Skipped NVIDIA GPU detection."
} elseif ($gpuNames.Count -gt 0) {
    Write-Output ("Detected NVIDIA GPU: " + ($gpuNames -join ", "))
} else {
    Write-Warning "No NVIDIA GPU was detected through Win32_VideoController."
}

if ($missingRuntime.Count -gt 0) {
    Write-Warning ("Missing runtime patterns: " + ($missingRuntime -join ", "))
}

if ($missingAssets.Count -gt 0) {
    Write-Warning "Manifest data roots are missing:"
    $missingAssets | ForEach-Object { Write-Warning ("  " + $_) }
}

if ($missingRuntime.Count -gt 0 -or ((-not $SkipGpuCheck) -and $gpuNames.Count -eq 0)) {
    exit $ExitCodeRuntimeFailure
}

if ($missingAssets.Count -gt 0) {
    exit $ExitCodeMissingAssets
}

Write-Output "Runtime preflight check passed."
exit 0
