[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('owner', 'private-beta', 'free', 'pro')]
    [string] $Profile
)

$ErrorActionPreference = 'Stop'
$sourceDir = Split-Path -Parent $PSScriptRoot

$profiles = @{
    'owner' = @{ Name = 'Owner' }
    'private-beta' = @{ Name = 'Private-Beta' }
    'free' = @{ Name = 'Free' }
    'pro' = @{ Name = 'Pro' }
}

$profileInfo = $profiles[$Profile]
$presetName = "windows-$Profile"
$presets = Get-Content -LiteralPath (Join-Path $sourceDir 'CMakePresets.json') -Raw | ConvertFrom-Json
$preset = $presets.configurePresets | Where-Object { $_.name -eq $presetName } | Select-Object -First 1
if (-not $preset) {
    throw "Configure preset '$presetName' was not found."
}
$version = [string] $preset.cacheVariables.RS_BETA_VERSION
$installerChannel = [string] $preset.cacheVariables.RS_BETA_CHANNEL
if ([string]::IsNullOrWhiteSpace($version) -or [string]::IsNullOrWhiteSpace($installerChannel)) {
    throw "Configure preset '$presetName' must define RS_BETA_VERSION and RS_BETA_CHANNEL."
}
$artifactRoot = Join-Path $sourceDir "artifacts\$Profile\$version"
$presetArtifactRoot = [System.IO.Path]::GetFullPath(([string] $preset.cacheVariables.RS_ARTIFACT_DIR).Replace('${sourceDir}', $sourceDir))
if ($presetArtifactRoot -ne [System.IO.Path]::GetFullPath($artifactRoot)) {
    throw "Configure preset '$presetName' must stage artifacts in '$artifactRoot'."
}
$prerequisiteRoot = Join-Path $sourceDir '.deps\installer-prerequisites'
$prerequisites = @(
    @{
        Name = 'Microsoft Edge WebView2 Runtime (x64)'
        Uri = 'https://go.microsoft.com/fwlink/?linkid=2124701'
        File = 'MicrosoftEdgeWebView2RuntimeInstallerX64.exe'
    },
    @{
        Name = 'Microsoft Visual C++ Redistributable (x64)'
        Uri = 'https://aka.ms/vs/17/release/vc_redist.x64.exe'
        File = 'vc_redist.x64.exe'
    }
)

New-Item -ItemType Directory -Path $prerequisiteRoot -Force | Out-Null
foreach ($prerequisite in $prerequisites) {
    $destination = Join-Path $prerequisiteRoot $prerequisite.File
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
        $temporary = "$destination.download"
        Write-Output "Downloading $($prerequisite.Name) from Microsoft..."
        try {
            Invoke-WebRequest -Uri $prerequisite.Uri -OutFile $temporary -UseBasicParsing
            Move-Item -LiteralPath $temporary -Destination $destination -Force
        } finally {
            if (Test-Path -LiteralPath $temporary) {
                Remove-Item -LiteralPath $temporary -Force
            }
        }
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $destination
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
        $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') {
        throw "$($prerequisite.Name) is not validly signed by Microsoft: $destination"
    }
    Write-Output "Verified prerequisite: $($prerequisite.Name)"
}

$requiredFiles = @(
    (Join-Path $artifactRoot 'obs-plugins\64bit\RearSilver-Stream-Suite.dll'),
    (Join-Path $artifactRoot 'control-hub\RearSilver-Stream-Suite-Control-Hub.exe'),
    (Join-Path $artifactRoot 'control-hub\RearSilver-Stream-Suite-Updater.exe'),
    (Join-Path $artifactRoot 'data\obs-plugins\RearSilver-Stream-Suite\locale\en-GB.ini')
)
foreach ($requiredFile in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required clean artifact is missing: $requiredFile"
    }
}

$makensisCandidates = @(
    (Get-Command makensis.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),
    "$env:ProgramFiles\NSIS\makensis.exe",
    "${env:ProgramFiles(x86)}\NSIS\makensis.exe"
) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -Unique
$makensis = $makensisCandidates | Select-Object -First 1
if (-not $makensis) {
    throw 'NSIS 3.x was not found. Install NSIS or add makensis.exe to PATH.'
}

# Compare against the version actually bundled, not a permanently hardcoded
# runtime version. An unknown version falls back to running the prerequisite.
$vcRuntimeInfo = (Get-Item -LiteralPath (Join-Path $prerequisiteRoot 'vc_redist.x64.exe')).VersionInfo
$vcRuntimeVersion = if ($vcRuntimeInfo.FileVersionRaw) { $vcRuntimeInfo.FileVersionRaw.ToString() } else { '0' }
if ($vcRuntimeVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { $vcRuntimeVersion = '0' }

$installerDir = Join-Path $artifactRoot 'installer'
New-Item -ItemType Directory -Path $installerDir -Force | Out-Null
$outputFile = Join-Path $installerDir "RearSilver-Stream-Suite-$($profileInfo.Name)-$version-Setup.exe"
$installerSource = Join-Path $sourceDir 'installer.nsi'

Push-Location -LiteralPath $sourceDir
try {
	Write-Output 'Packaging the bundled Control Hub runtime...'
	& $makensis /NOCD "/DRS_ARTIFACT_ROOT=$artifactRoot" "/DRS_PREREQUISITE_ROOT=$prerequisiteRoot" "/DRS_VERSION=$version" "/DRS_CHANNEL=$installerChannel" "/DRS_OUTPUT_FILE=$outputFile" "/DRS_VC_RUNTIME_MIN_VERSION=$vcRuntimeVersion" $installerSource
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}

Write-Output "Installer created: $outputFile"
