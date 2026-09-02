[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Preset,

    [ValidateSet('Configure', 'Build')]
    [string] $Mode = 'Configure',

    [switch] $Fresh
)

$ErrorActionPreference = 'Stop'

# Some launch environments expose both `Path` and `PATH`. MSBuild's legacy
# process launcher treats those names case-insensitively and fails before CL.exe
# starts. Preserve the combined value, remove every casing variant, and expose
# exactly one canonical entry to CMake and its child processes.
$pathEntries = @(Get-ChildItem Env: | Where-Object { $_.Name -ieq 'Path' })
$pathValue = ($pathEntries | ForEach-Object { $_.Value } | Where-Object { $_ } | Select-Object -First 1)

foreach ($entry in $pathEntries) {
    Remove-Item -LiteralPath ("Env:" + $entry.Name)
}

if ($pathValue) {
    Set-Item -LiteralPath 'Env:Path' -Value $pathValue
}

$remainingPathNames = @(Get-ChildItem Env: | Where-Object { $_.Name -ieq 'Path' } | ForEach-Object { $_.Name })
if ($remainingPathNames.Count -ne 1) {
    throw "Unable to normalize the process PATH environment. Found $($remainingPathNames.Count) entries."
}

$sourceDir = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $sourceDir 'CMakePresets.json'))) {
    throw "CMakePresets.json was not found in the repository root: $sourceDir"
}

if ($Mode -eq 'Build' -and $Fresh) {
    throw '-Fresh can only be used in Configure mode.'
}

$arguments = if ($Mode -eq 'Build') {
    @('--build', '--preset', $Preset)
} else {
    @('--preset', $Preset)
}

if ($Fresh) {
    $arguments += '--fresh'
}

Push-Location -LiteralPath $sourceDir
try {
    & cmake @arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}
