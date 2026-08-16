# Starts an OpenOCD server, and optionally gdb, against a built target.
#
# WHY THIS EXISTS: the debug setup was reachable only through VS Code's cortex-debug extension,
# and its launch.json leaned on ${command:cmake.launchTargetPath} -- a variable only that
# extension can resolve. So there was no way to start a debug session from a shell, and no way
# for a script to do it either.
#
# It also encodes which OpenOCD config and SVD belong to which board, because getting that wrong
# is not a clean failure: attaching an rp2040 configuration to an rp2350 image produces confusing
# misbehaviour rather than an error, and screen-test's launch.json named the rp2040 SVD for every
# configuration including the RP2350 ones.
#
# PARTLY HARDWARE-VERIFIED. Run against a Pico 2 through a CMSIS-DAP probe, -ServerOnly reaches a
# live OpenOCD listening on 3333: the probe is detected and named, and the config, SVD and ELF all
# resolve. What is still untested is the gdb half -- the default path that launches
# arm-none-eabi-gdb, loads the image and hands over a session.
#
# USAGE:  light-debug.ps1 [-Target <name>] [-Preset <name>] [-ServerOnly] [-Attach] [-NoBuild]
param(
        [string]$Target,
        [string]$Preset,
        [switch]$ServerOnly,
        [switch]$Attach,
        [switch]$NoBuild,
        [string]$ProjectRoot
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib/LightProject.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'lib/LightPlatform.psm1') -Force
. (Join-Path $PSScriptRoot 'light-env.ps1') -Quiet

$config = Get-LightProjectConfig -ProjectRoot ($ProjectRoot ? $ProjectRoot : (Get-LightProjectRoot))
#   defaults to the project's DefaultTarget, so a bare invocation does the obvious thing from
# a terminal and CI need not encode a target name it would have to keep in step
if (-not $Target) { $Target = Resolve-LightDefaultTarget -Config $config }
if (-not $Preset) { $Preset = Resolve-LightTargetPreset -Config $config -Target $Target }
$tree = Resolve-LightTree -Config $config -Preset $Preset

if (-not $config.Debug -or -not $config.Debug.ContainsKey($Preset)) {
        throw "no Debug entry for preset '$Preset' in scripts/project.config.ps1 -- it must name the OpenOCD config and SVD for this board"
}
$debug = $config.Debug[$Preset]

if (-not $NoBuild) {
        & (Join-Path $PSScriptRoot 'light-build.ps1') -Target $Target -Preset $Preset -ProjectRoot $config.Root
}

#   the executable is named differently per toolchain: pico_add_extra_outputs() gives the pico
# targets a .elf suffix, while the CMSIS targets are plain CMake executables and carry no suffix
# at all (their .bin and .hex are objcopy'd from this file by the post-build hook in cmsis.cmake)
$elf = @("module/$Target/$Target.elf", "module/$Target/$Target") |
        ForEach-Object { Join-Path $tree $_ } |
        Where-Object { Test-Path $_ -PathType Leaf } |
        Select-Object -First 1
if (-not $elf) {
        throw "no executable found for '$Target' in '$tree' (looked for $Target.elf and $Target) -- has it been built?"
}

$ocdConfig = Join-Path $config.Root $debug.Config
if (-not (Test-Path $ocdConfig)) { throw "no OpenOCD config at '$ocdConfig'" }

$exe = Get-LightExeSuffix
$tools = Get-LightToolRoot
#   named in project.config.ps1 if it is somewhere unusual; otherwise an unpacked xpack build
# under the tools root, otherwise whatever is on PATH -- which is the normal case on Linux,
# where openocd is a distro package
$openocd = Find-LightTool -Name "openocd$exe" -Candidates @(
        $debug.Server,
        "$tools/xpack-openocd-0.12.0-7/bin/openocd$exe",
        "$tools/xpack-openocd/bin/openocd$exe"
)
if (-not $openocd) { throw "no openocd found: not named in project.config.ps1 Debug.Server, not under '$tools', and not on PATH" }

#   check a probe is actually present, so "openocd exits with a transport error" becomes
# something that names the real problem. Note VID_2E8A&PID_0009 is the RP2 board's own CDC
# interface and is NOT a debug port -- only PID_000C is a CMSIS-DAP probe. ST-Link is a
# different vendor entirely (VID_0483), which is what the STM32 board is debugged through.
#
#   the two platforms enumerate USB completely differently, so this is one of the few places
# that genuinely branches rather than calling a shared helper: WMI has no Linux equivalent, and
# sysfs exposes idVendor/idProduct per device with no vendor string to match on
$probeNames = @()
if ($IsWindows) {
        $probes = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object {
                $_.DeviceID -like '*VID_2E8A&PID_000C*' -or   # Raspberry Pi CMSIS-DAP / debugprobe
                $_.DeviceID -like '*VID_0483&PID_37*' -or      # ST-Link V2.1 / V3
                $_.DeviceID -like '*VID_1366*'                 # SEGGER J-Link
        }
        if ($probes) {
                # these enumerate as composite devices, so prefer a child whose name actually says
                # what it is over the generic "USB Composite Device" parent
                $named = $probes | Where-Object { $_.Name -notmatch 'Composite' } | Select-Object -First 1
                if (-not $named) { $named = $probes | Select-Object -First 1 }
                $probeNames = @($named.Name)
        }
} else {
        # (vendor, product-prefix, label); an empty product prefix matches the whole vendor
        $known = @(
                @('2e8a', '000c', 'Raspberry Pi CMSIS-DAP'),
                @('0483', '37',   'ST-Link V2.1/V3'),
                @('1366', '',     'SEGGER J-Link')
        )
        foreach ($d in @(Get-ChildItem /sys/bus/usb/devices -ErrorAction SilentlyContinue)) {
                $vf = Join-Path $d.FullName 'idVendor'
                $pf = Join-Path $d.FullName 'idProduct'
                if (-not (Test-Path $vf) -or -not (Test-Path $pf)) { continue }
                $v = (Get-Content $vf -Raw).Trim(); $p = (Get-Content $pf -Raw).Trim()
                foreach ($k in $known) {
                        if ($v -eq $k[0] -and $p.StartsWith($k[1])) {
                                $pn = Join-Path $d.FullName 'product'
                                $label = if (Test-Path $pn) { (Get-Content $pn -Raw).Trim() } else { $k[2] }
                                $probeNames += "$label ($v`:$p)"
                        }
                }
        }
}
if ($probeNames) {
        Write-Host "probe:   $($probeNames | Select-Object -First 1)"
} else {
        Write-Warning "no debug probe found (looked for CMSIS-DAP, ST-Link and J-Link). OpenOCD will fail to connect."
}

#   OpenOCD's own script library, which its board/target configs `source` by relative name.
# An unpacked xpack build keeps it at <prefix>/openocd/scripts; a distro package puts it at
# <prefix>/share/openocd/scripts. Passing -s for a directory that does not exist makes every
# `source` fail with a message about the inner file, never about the search path, so the
# candidates are probed and -s is simply omitted when neither is found -- a packaged openocd
# already knows its own default
$prefix = Split-Path (Split-Path $openocd -Parent) -Parent
$searchDir = @(
        (Join-Path $prefix 'openocd/scripts'),
        (Join-Path $prefix 'share/openocd/scripts')
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$ocdArgs = @()
if ($searchDir) { $ocdArgs += @('-s', $searchDir) }
$ocdArgs += @('-f', $ocdConfig)

Write-Host "openocd: $openocd"
Write-Host "config:  $ocdConfig"
Write-Host "elf:     $elf"
if ($debug.Svd) { Write-Host "svd:     $(Join-Path $config.Root $debug.Svd)" }

if ($ServerOnly) {
        Write-Host "`nstarting OpenOCD in the foreground -- attach a debugger to localhost:3333, Ctrl-C to stop"
        & $openocd @ocdArgs
        return
}

$server = Start-Process -FilePath $openocd -ArgumentList $ocdArgs -PassThru -NoNewWindow
try {
        Start-Sleep -Seconds 2
        if ($server.HasExited) { throw "OpenOCD exited immediately (code $($server.ExitCode)) -- is a probe connected and not already in use?" }

        #   LIGHT_ARM_GDB_DIR wins, then an unpacked toolchain under the tools root, then PATH.
        # On Linux gdb-multiarch is a common substitute where arm-none-eabi-gdb is not packaged
        $gdb = Find-LightTool -Name "arm-none-eabi-gdb$exe" -Candidates @(
                ($env:LIGHT_ARM_GDB_DIR ? (Join-Path $env:LIGHT_ARM_GDB_DIR "arm-none-eabi-gdb$exe") : $null),
                "$tools/arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi/bin/arm-none-eabi-gdb$exe",
                "$tools/arm-gnu-toolchain/bin/arm-none-eabi-gdb$exe"
        )
        if (-not $gdb) { $gdb = Find-LightTool -Name "gdb-multiarch$exe" }
        if (-not $gdb) { throw "no arm-none-eabi-gdb (or gdb-multiarch) found: set LIGHT_ARM_GDB_DIR, unpack a toolchain under '$tools', or install one on PATH" }

        # -Attach leaves the image on the board alone, which is what you want when debugging
        # something already running; the default loads the ELF you just built
        $gdbArgs = @('-ex', 'target extended-remote localhost:3333')
        if (-not $Attach) {
                $gdbArgs += @('-ex', 'monitor reset init', '-ex', 'load')
        }
        $gdbArgs += $elf

        & $gdb @gdbArgs
} finally {
        if (-not $server.HasExited) {
                Write-Host "stopping OpenOCD"
                Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        }
}
