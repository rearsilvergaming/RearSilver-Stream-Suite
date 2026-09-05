[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('owner', 'private-beta', 'free', 'pro')]
    [string] $Profile
)

$ErrorActionPreference = 'Stop'
$sourceDir = Split-Path -Parent $PSScriptRoot

$profiles = @{
    'owner' = @{ Version = '1.0.0-owner'; Channel = 'Owner Build'; File = 'RearSilver-Stream-Suite-Owner-Setup.exe' }
    'private-beta' = @{ Version = '1.0.0-beta.1'; Channel = 'Private Beta'; File = 'RearSilver-Stream-Suite-Private-Beta-Setup.exe' }
    'free' = @{ Version = '1.0.0'; Channel = 'Free'; File = 'RearSilver-Stream-Suite-Free-Setup.exe' }
    'pro' = @{ Version = '1.0.0'; Channel = 'Pro'; File = 'RearSilver-Stream-Suite-Pro-Setup.exe' }
}

$profileInfo = $profiles[$Profile]
$artifactRoot = Join-Path $sourceDir "artifacts\$Profile"
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

$installerDir = Join-Path $artifactRoot 'installer'
New-Item -ItemType Directory -Path $installerDir -Force | Out-Null
$outputFile = Join-Path $installerDir $profileInfo.File

Push-Location -LiteralPath $sourceDir
try {
	Write-Output 'NSIS is compressing the bundled Control Hub runtime. This can take several minutes without updating the console.'
	& $makensis "/DRS_ARTIFACT_ROOT=$artifactRoot" "/DRS_PREREQUISITE_ROOT=$prerequisiteRoot" "/DRS_VERSION=$($profileInfo.Version)" "/DRS_CHANNEL=$($profileInfo.Channel)" "/DRS_OUTPUT_FILE=$outputFile" '.\installer.nsi'
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}

Write-Output "Installer created: $outputFile"
