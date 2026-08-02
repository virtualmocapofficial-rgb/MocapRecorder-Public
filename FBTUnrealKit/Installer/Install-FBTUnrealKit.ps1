[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Project,

    [string]$EngineRoot,

    [switch]$ForceCppConversion,

    [switch]$SkipBuild,

    [switch]$PlanOnly,

    [switch]$NonInteractive,

    [string]$InstallerPassword
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Confirm-InstallerPassword {
    param([string]$ProvidedPassword)

    $expectedHash = 'fb0ebba62977a00d4fd427faa5dad1332a12a4ec3469f6012347e7a612e32ef9'
    if ([string]::IsNullOrEmpty($ProvidedPassword)) {
        if ($NonInteractive) {
            throw 'Installer password required. Read Installer/README.md in this package.'
        }

        $securePassword = Read-Host 'Enter the experimental installer password from Installer/README.md' -AsSecureString
        $passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
        try {
            $ProvidedPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
        }
        finally {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
        }
    }

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $passwordBytes = [Text.Encoding]::UTF8.GetBytes($ProvidedPassword)
        $actualHash = ($sha256.ComputeHash($passwordBytes) | ForEach-Object { $_.ToString('x2') }) -join ''
    }
    finally {
        $sha256.Dispose()
    }

    if ($actualHash -cne $expectedHash) {
        throw 'Incorrect installer password. Read Installer/README.md in this package.'
    }
}

function Write-InstallerStatus {
    param([string]$Message)
    Write-Host "[FBTUnrealKit Installer] $Message"
}

function Select-UnrealProject {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Select the Unreal project that should receive FBT Unreal Kit'
    $dialog.Filter = 'Unreal Engine Projects (*.uproject)|*.uproject'
    $dialog.CheckFileExists = $true
    $dialog.Multiselect = $false
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        throw 'Installation cancelled: no Unreal project was selected.'
    }
    return $dialog.FileName
}

function Resolve-UProjectPath {
    param([string]$RequestedPath)

    if ([string]::IsNullOrWhiteSpace($RequestedPath)) {
        if ($NonInteractive) {
            throw 'Project is required when -NonInteractive is used.'
        }
        $RequestedPath = Select-UnrealProject
    }

    $resolved = Resolve-Path -LiteralPath $RequestedPath -ErrorAction Stop
    if ((Get-Item -LiteralPath $resolved.Path).PSIsContainer) {
        $projects = @(Get-ChildItem -LiteralPath $resolved.Path -Filter '*.uproject' -File)
        if ($projects.Count -ne 1) {
            throw "Expected exactly one .uproject in '$($resolved.Path)', but found $($projects.Count). Select the .uproject file directly."
        }
        return $projects[0].FullName
    }

    if ([IO.Path]::GetExtension($resolved.Path) -ine '.uproject') {
        throw "The selected file is not a .uproject: $($resolved.Path)"
    }
    return $resolved.Path
}

function Read-JsonFile {
    param([string]$Path)
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Write-JsonFile {
    param(
        [string]$Path,
        [object]$Value
    )
    $json = $Value | ConvertTo-Json -Depth 100
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, $utf8WithoutBom)
}

function Set-ObjectProperty {
    param(
        [object]$Object,
        [string]$Name,
        [object]$Value
    )
    if ($Object.PSObject.Properties.Name -contains $Name) {
        $Object.$Name = $Value
    }
    else {
        $Object | Add-Member -MemberType NoteProperty -Name $Name -Value $Value
    }
}

function Resolve-EngineRoot {
    param(
        [string]$Association,
        [string]$RequestedRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        $candidate = (Resolve-Path -LiteralPath $RequestedRoot).Path
        if (Test-Path -LiteralPath (Join-Path $candidate 'Engine\Build\BatchFiles\Build.bat')) {
            return $candidate
        }
        throw "Engine root does not contain Engine\\Build\\BatchFiles\\Build.bat: $candidate"
    }

    $launcherCandidate = "C:\Program Files\Epic Games\UE_$Association"
    if (Test-Path -LiteralPath (Join-Path $launcherCandidate 'Engine\Build\BatchFiles\Build.bat')) {
        return $launcherCandidate
    }

    $registryPath = 'HKCU:\Software\Epic Games\Unreal Engine\Builds'
    if (Test-Path $registryPath) {
        $builds = Get-ItemProperty -Path $registryPath
        $property = $builds.PSObject.Properties | Where-Object { $_.Name -eq $Association } | Select-Object -First 1
        if ($null -ne $property -and
            (Test-Path -LiteralPath (Join-Path ([string]$property.Value) 'Engine\Build\BatchFiles\Build.bat'))) {
            return [string]$property.Value
        }
    }

    return $null
}

function Copy-PluginPackage {
    param(
        [string]$Source,
        [string]$Destination,
        [bool]$IncludeBinaries
    )

    $excludedDirectories = @('Intermediate', '.git', 'DerivedDataCache', 'Saved')
    if (-not $IncludeBinaries) {
        $excludedDirectories += 'Binaries'
    }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null

    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        if ($item.PSIsContainer -and $excludedDirectories -contains $item.Name) {
            continue
        }
        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse -Force
    }
}

function Enable-PluginInDescriptor {
    param([object]$Descriptor)

    $plugins = @()
    if ($Descriptor.PSObject.Properties.Name -contains 'Plugins') {
        $plugins = @($Descriptor.Plugins)
    }

    $entry = $plugins | Where-Object { $_.Name -eq 'FBTUnrealKit' } | Select-Object -First 1
    if ($null -eq $entry) {
        $plugins += [pscustomobject]@{
            Name = 'FBTUnrealKit'
            Enabled = $true
        }
    }
    else {
        Set-ObjectProperty -Object $entry -Name 'Enabled' -Value $true
    }

    Set-ObjectProperty -Object $Descriptor -Name 'Plugins' -Value $plugins
}

function Convert-BlueprintProjectToCpp {
    param(
        [object]$Descriptor,
        [string]$ProjectDirectory,
        [string]$ModuleName
    )

    if ($ModuleName -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
        throw "Project name '$ModuleName' cannot be used as a C++ module identifier. Rename the .uproject before automatic conversion."
    }

    $sourceRoot = Join-Path $ProjectDirectory 'Source'
    $moduleRoot = Join-Path $sourceRoot $ModuleName
    New-Item -ItemType Directory -Path $moduleRoot -Force | Out-Null

    $buildCs = @"
using UnrealBuildTool;

public class $ModuleName : ModuleRules
{
    public $ModuleName(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
    }
}
"@

    $header = @"
#pragma once

#include "CoreMinimal.h"
"@

    $source = @"
#include "$ModuleName.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, $ModuleName, "$ModuleName");
"@

    $gameTarget = @"
using UnrealBuildTool;
using System.Collections.Generic;

public class ${ModuleName}Target : TargetRules
{
    public ${ModuleName}Target(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("$ModuleName");
    }
}
"@

    $editorTarget = @"
using UnrealBuildTool;
using System.Collections.Generic;

public class ${ModuleName}EditorTarget : TargetRules
{
    public ${ModuleName}EditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("$ModuleName");
    }
}
"@

    $files = @{
        (Join-Path $moduleRoot "$ModuleName.Build.cs") = $buildCs
        (Join-Path $moduleRoot "$ModuleName.h") = $header
        (Join-Path $moduleRoot "$ModuleName.cpp") = $source
        (Join-Path $sourceRoot "$ModuleName.Target.cs") = $gameTarget
        (Join-Path $sourceRoot "$ModuleName`Editor.Target.cs") = $editorTarget
    }

    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    foreach ($pair in $files.GetEnumerator()) {
        if (-not (Test-Path -LiteralPath $pair.Key)) {
            [IO.File]::WriteAllText($pair.Key, ([string]$pair.Value).TrimStart() + [Environment]::NewLine, $utf8WithoutBom)
        }
    }

    $modules = @()
    if ($Descriptor.PSObject.Properties.Name -contains 'Modules') {
        $modules = @($Descriptor.Modules)
    }
    if (-not ($modules | Where-Object { $_.Name -eq $ModuleName })) {
        $modules += [pscustomobject]@{
            Name = $ModuleName
            Type = 'Runtime'
            LoadingPhase = 'Default'
        }
    }
    Set-ObjectProperty -Object $Descriptor -Name 'Modules' -Value $modules
}

Confirm-InstallerPassword -ProvidedPassword $InstallerPassword
$projectPath = Resolve-UProjectPath -RequestedPath $Project
$projectDirectory = Split-Path -Parent $projectPath
$projectName = [IO.Path]::GetFileNameWithoutExtension($projectPath)
$pluginRoot = Split-Path -Parent $PSScriptRoot
$pluginDescriptorPath = Join-Path $pluginRoot 'FBTUnrealKit.uplugin'
$packageManifestPath = Join-Path $PSScriptRoot 'package-manifest.json'

if (-not (Test-Path -LiteralPath $pluginDescriptorPath)) {
    throw "Installer package is incomplete: $pluginDescriptorPath is missing."
}

$descriptor = Read-JsonFile -Path $projectPath
$packageManifest = Read-JsonFile -Path $packageManifestPath
$engineAssociation = [string]$descriptor.EngineAssociation
$existingModules = @()
if ($descriptor.PSObject.Properties.Name -contains 'Modules') {
    $existingModules = @($descriptor.Modules)
}
$isBlueprintOnly = $existingModules.Count -eq 0
$binaryEngineVersions = @($packageManifest.BinaryEngineVersions | ForEach-Object { [string]$_ })
$hasMatchingBinaries =
    $binaryEngineVersions -contains $engineAssociation -and
    (Test-Path -LiteralPath (Join-Path $pluginRoot 'Binaries\Win64\UnrealEditor-FBTUnrealKit.dll'))
$shouldConvert = $ForceCppConversion -or ($isBlueprintOnly -and -not $hasMatchingBinaries)
$resolvedEngineRoot = Resolve-EngineRoot -Association $engineAssociation -RequestedRoot $EngineRoot
$shouldBuild = (-not $hasMatchingBinaries) -and -not $SkipBuild
$buildToolAvailable =
    -not [string]::IsNullOrWhiteSpace($resolvedEngineRoot) -and
    ((Test-Path -LiteralPath (Join-Path $resolvedEngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll')) -or
     (Test-Path -LiteralPath (Join-Path $resolvedEngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool.exe')))

$plan = [pscustomobject]@{
    Project = $projectPath
    ProjectType = if ($isBlueprintOnly) { 'Blueprint-only' } else { 'C++' }
    EngineAssociation = $engineAssociation
    MatchingPrecompiledBinaries = $hasMatchingBinaries
    ConvertProjectToCpp = $shouldConvert
    BuildAfterInstall = $shouldBuild
    BuildToolAvailable = $buildToolAvailable
    EngineRoot = $resolvedEngineRoot
    Destination = (Join-Path $projectDirectory 'Plugins\FBTUnrealKit')
}

Write-InstallerStatus ($plan | Format-List | Out-String)
if ($PlanOnly) {
    return
}

if ($shouldBuild -and -not $buildToolAvailable) {
    throw "Unreal Engine '$engineAssociation' is missing UnrealBuildTool. Verify this engine installation in Epic Games Launcher, or install a matching precompiled FBT Unreal Kit package."
}

if (-not $NonInteractive) {
    Add-Type -AssemblyName System.Windows.Forms
    $message = @"
Project: $projectPath
Detected type: $($plan.ProjectType)
Unreal version: $engineAssociation
Matching precompiled plugin: $hasMatchingBinaries
Automatic C++ conversion: $shouldConvert
Automatic compile: $shouldBuild

Continue with installation?
"@
    $answer = [System.Windows.Forms.MessageBox]::Show(
        $message,
        'Install FBT Unreal Kit',
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Question)
    if ($answer -ne [System.Windows.Forms.DialogResult]::Yes) {
        throw 'Installation cancelled by the user.'
    }
}

$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$backupRoot = Join-Path $projectDirectory "Saved\FBTUnrealKitInstallerBackups\$timestamp"
$destinationPlugin = Join-Path $projectDirectory 'Plugins\FBTUnrealKit'
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
Copy-Item -LiteralPath $projectPath -Destination (Join-Path $backupRoot ([IO.Path]::GetFileName($projectPath))) -Force

$samePluginLocation = $false
if (Test-Path -LiteralPath $destinationPlugin) {
    $sourceResolved = (Resolve-Path -LiteralPath $pluginRoot).Path.TrimEnd('\')
    $destinationResolved = (Resolve-Path -LiteralPath $destinationPlugin).Path.TrimEnd('\')
    $samePluginLocation = $sourceResolved -ieq $destinationResolved
    if (-not $samePluginLocation) {
        Copy-Item -LiteralPath $destinationPlugin -Destination (Join-Path $backupRoot 'FBTUnrealKit') -Recurse -Force
        Remove-Item -LiteralPath $destinationPlugin -Recurse -Force
    }
}

if (-not $samePluginLocation) {
    Copy-PluginPackage -Source $pluginRoot -Destination $destinationPlugin -IncludeBinaries $hasMatchingBinaries

    # Presets created by users live beside the bundled presets in current plugin versions.
    # Restore uniquely named presets after an update; same-name files remain in the backup
    # so a newer bundled preset is not silently replaced by stale content.
    $backupPresetRoot = Join-Path $backupRoot 'FBTUnrealKit\Source\Presets'
    $destinationPresetRoot = Join-Path $destinationPlugin 'Source\Presets'
    if (Test-Path -LiteralPath $backupPresetRoot) {
        New-Item -ItemType Directory -Path $destinationPresetRoot -Force | Out-Null
        foreach ($preset in Get-ChildItem -LiteralPath $backupPresetRoot -Filter '*.json' -File) {
            $destinationPreset = Join-Path $destinationPresetRoot $preset.Name
            if (-not (Test-Path -LiteralPath $destinationPreset)) {
                Copy-Item -LiteralPath $preset.FullName -Destination $destinationPreset -Force
            }
        }
    }
}

Enable-PluginInDescriptor -Descriptor $descriptor
if ($shouldConvert) {
    Convert-BlueprintProjectToCpp -Descriptor $descriptor -ProjectDirectory $projectDirectory -ModuleName $projectName
}
Write-JsonFile -Path $projectPath -Value $descriptor

if ($shouldBuild) {
    if ([string]::IsNullOrWhiteSpace($resolvedEngineRoot)) {
        throw "The project was installed and converted, but Unreal Engine '$engineAssociation' could not be located for automatic compilation. Re-run with -EngineRoot."
    }

    $buildScript = Join-Path $resolvedEngineRoot 'Engine\Build\BatchFiles\Build.bat'
    Write-InstallerStatus "Compiling ${projectName}Editor with Unreal Engine $engineAssociation..."
    $buildExitCode = 1
    Push-Location (Split-Path -Parent $buildScript)
    try {
        & $buildScript "${projectName}Editor" Win64 Development "-Project=$projectPath" -WaitMutex -FromMsBuild
        $buildExitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($buildExitCode -ne 0) {
        throw "UnrealBuildTool failed with exit code $buildExitCode. The original descriptor and plugin are backed up at '$backupRoot'."
    }
}

$logRoot = Join-Path $projectDirectory 'Saved\FBTUnrealKitInstaller'
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$result = [pscustomobject]@{
    InstalledAt = (Get-Date).ToString('o')
    PackageVersion = [string]$packageManifest.PackageVersion
    Project = $projectPath
    EngineAssociation = $engineAssociation
    WasBlueprintOnly = $isBlueprintOnly
    UsedMatchingPrecompiledBinaries = $hasMatchingBinaries
    ConvertedToCpp = $shouldConvert
    BuildCompleted = $shouldBuild
    Backup = $backupRoot
}
Write-JsonFile -Path (Join-Path $logRoot "install_$timestamp.json") -Value $result

Write-InstallerStatus "Installation completed successfully."
Write-InstallerStatus "Backup: $backupRoot"
if (-not $NonInteractive) {
    [System.Windows.Forms.MessageBox]::Show(
        "FBT Unreal Kit was installed successfully.`n`nBackup: $backupRoot",
        'FBT Unreal Kit Installed',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
}
