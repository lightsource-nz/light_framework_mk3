# Builds a target, configuring its tree first if it does not exist yet.
#
# WHY THIS EXISTS: knowing which preset builds a given target, and which tree that preset owns,
# was folklore -- eight flashable targets across screen-test alone, spread over five build trees,
# with two of the build presets naming a target ('screentest') that does not exist in any
# CMakeLists. Here the mapping is declared once in the project's scripts/project.config.ps1 and
# every wrapper reads it.
#
# The linker warning filter is not cosmetic: every RP2350 link emits two warnings about the
# FLASH memory region that are inherent to how the pico-sdk linker scripts are composed. Left in,
# they train you to ignore linker warnings, which is the last habit you want on an embedded
# project. -Verbose shows everything.
#
# USAGE:  light-build.ps1 [-Target <name>] [-Preset <name>] [-Clean] [-Verbose]
param(
        [string]$Target,
        [string]$Preset,
        [switch]$Clean,
        [string]$ProjectRoot
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib/LightProject.psm1') -Force
. (Join-Path $PSScriptRoot 'light-env.ps1') -Quiet

$config = Get-LightProjectConfig -ProjectRoot ($ProjectRoot ? $ProjectRoot : (Get-LightProjectRoot))
#   defaults to the project's DefaultTarget, so a bare invocation does the obvious thing from
# a terminal and CI need not encode a target name it would have to keep in step
if (-not $Target) { $Target = Resolve-LightDefaultTarget -Config $config }
if (-not $Preset) { $Preset = Resolve-LightTargetPreset -Config $config -Target $Target }
$tree = Resolve-LightTree -Config $config -Preset $Preset

if (-not (Test-Path (Join-Path $tree 'CMakeCache.txt'))) {
        & (Join-Path $PSScriptRoot 'light-configure.ps1') -Preset $Preset -ProjectRoot $config.Root
}

$buildArgs = @($tree, '--target', $Target)
if ($Clean) { $buildArgs += '--clean-first' }

Write-Host "building $Target ($Preset) in $tree"

#   known-harmless and unavoidable: objects.rp2350.ld declares no FLASH region of its own, and
# pico_flash_region.ld then redeclares the one the SDK provides. Neither indicates a problem
# with the image, and both fire on every single link.
$noise = 'warning: memory region `FLASH'' not declared|warning: redeclaration of memory region `FLASH'''

if ($VerbosePreference -ne 'SilentlyContinue') {
        cmake --build @buildArgs
} else {
        cmake --build @buildArgs 2>&1 | Where-Object { $_ -notmatch $noise } | ForEach-Object { Write-Host $_ }
}

# $LASTEXITCODE survives the pipeline above; PowerShell sets it from the native command
if ($LASTEXITCODE -ne 0) {
        throw "build of '$Target' failed with exit code $LASTEXITCODE"
}
Write-Host "built $Target"
