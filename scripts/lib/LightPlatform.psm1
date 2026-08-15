# Host-platform differences, in one place.
#
# WHY THIS EXISTS: these scripts were written on Windows and had Windows baked into them in ways
# that were easy to miss -- Win32_SerialPort, Get-Volume, a hardcoded C:/Users/.../tools, a ';'
# PATH separator, wsl.exe as the only route to clang. None of that is conceptually
# Windows-specific; every one has a Linux answer. Scattering `if ($IsWindows)` through nine
# scripts would work and would rot, because the next script would copy whichever branch its
# author happened to be running.
#
# So each difference is answered ONCE here, and the scripts ask a question rather than test a
# platform. The test to apply when adding to this file: if a caller has to know which OS it is
# on to use your function, the abstraction is in the wrong place.
#
# $IsWindows/$IsLinux/$IsMacOS are automatic variables in PowerShell 6+. They do NOT exist in
# Windows PowerShell 5.1, where every one reads as $null and every check silently takes the
# wrong branch -- hence the version guard at the bottom, which fails loudly instead.

Set-StrictMode -Version Latest

function Get-LightPlatform {
        if ($IsWindows) { return 'Windows' }
        if ($IsLinux)   { return 'Linux' }
        if ($IsMacOS)   { return 'MacOS' }
        throw "cannot determine host platform"
}

# '.exe' on Windows, '' elsewhere. For composing tool paths that are otherwise identical.
function Get-LightExeSuffix {
        if ($IsWindows) { return '.exe' } else { return '' }
}

#   ';' on Windows, ':' everywhere else. Splitting $env:PATH on a hardcoded ';' silently yields
# one enormous element on Linux, so the "is this already on PATH" test never matches and PATH
# grows without bound on every dot-source
function Get-LightPathSeparator {
        return [System.IO.Path]::PathSeparator
}

#   where third-party toolchains live. LIGHT_TOOLS_PATH overrides; otherwise a per-platform
# default that at least exists as a convention. This used to be a hardcoded absolute path
# containing one developer's username, which is not something a second machine can satisfy
function Get-LightToolRoot {
        if ($env:LIGHT_TOOLS_PATH) { return $env:LIGHT_TOOLS_PATH }
        return (Join-Path $HOME 'tools')
}

# First of `Candidates` that exists, else the first found on PATH by `Name`, else $null. Lets a
# caller state its preferences without also writing the search.
function Find-LightTool {
        param(
                [Parameter(Mandatory)] [string]$Name,
                [string[]]$Candidates = @()
        )

        foreach ($c in $Candidates) {
                if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path }
        }
        $cmd = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue |
                Select-Object -First 1
        if ($cmd) { return $cmd.Source }
        return $null
}

# --- WSL, which is a Windows-only concept -------------------------------------------------

#   true only when a usable WSL distro is present. On Linux this is always false, and that is
# the point: the coverage script runs its Linux worker natively there rather than looking for a
# Linux VM it is already inside
function Test-LightWsl {
        param([string]$Distro = 'Debian')

        if (-not $IsWindows) { return $false }
        $wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
        if (-not $wsl) { return $false }
        # -l -q lists installed distros; wsl.exe emits UTF-16, which PowerShell handles, but the
        # names carry stray nulls on some builds
        $distros = (& wsl.exe -l -q 2>$null) -replace "`0", '' | ForEach-Object { $_.Trim() }
        return [bool]($distros -contains $Distro)
}

# C:\Users\x\y -> /mnt/c/Users/x/y. Windows-only by definition; on Linux a path is already the
# path WSL would see, because there is no WSL.
function ConvertTo-LightWslPath {
        param([Parameter(Mandatory)] [string]$Path)

        $full = (Resolve-Path $Path).Path
        if (-not $IsWindows) { return $full }
        $drive = $full.Substring(0, 1).ToLower()
        return "/mnt/$drive" + ($full.Substring(2) -replace '\\', '/')
}

# --- USB serial devices --------------------------------------------------------------------

#   every serial device matching a USB VID/PID, as objects with .Device (what you open) and
# .Description. Matching the PID as well as the VID is not optional: a CMSIS-DAP probe shares
# RP2's vendor ID, and a 1200-baud reset sent to the probe does nothing while looking exactly
# like a board that ignored it.
#
#   Windows reads WMI. Linux walks sysfs rather than parsing /dev/serial/by-id names, because
# the by-id string is composed from USB descriptor text -- it carries the product NAME, which
# firmware chooses and which differs between an app and the bootloader, while idVendor/idProduct
# are the numbers actually being matched on.
function Find-LightSerialPort {
        param(
                [Parameter(Mandatory)] [string]$VendorId,     # '2E8A'
                [Parameter(Mandatory)] [string]$ProductId     # '0009'
        )

        if ($IsWindows) {
                $pattern = "*VID_$($VendorId.ToUpper())&PID_$($ProductId.ToUpper())*"
                return @(Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
                        Where-Object { $_.PNPDeviceID -like $pattern } |
                        ForEach-Object {
                                [pscustomobject]@{ Device = $_.DeviceID; Description = $_.Name }
                        })
        }

        $vid = $VendorId.ToLower().TrimStart('0').PadLeft(4, '0')
        $pid = $ProductId.ToLower().TrimStart('0').PadLeft(4, '0')
        $found = @()
        foreach ($tty in @(Get-ChildItem /sys/class/tty -ErrorAction SilentlyContinue |
                        Where-Object { $_.Name -match '^tty(ACM|USB)' })) {
                #   the tty's `device` link points at the USB INTERFACE; idVendor/idProduct live
                # on the parent device, one level up. Walking a couple of levels covers both
                # layouts without hardcoding either
                $dev = Join-Path $tty.FullName 'device'
                if (-not (Test-Path $dev)) { continue }
                $node = $dev
                for ($i = 0; $i -lt 4; $i++) {
                        $vf = Join-Path $node 'idVendor'
                        $pf = Join-Path $node 'idProduct'
                        if ((Test-Path $vf) -and (Test-Path $pf)) {
                                if (((Get-Content $vf -Raw).Trim() -eq $vid) -and
                                    ((Get-Content $pf -Raw).Trim() -eq $pid)) {
                                        $nameFile = Join-Path $node 'product'
                                        $desc = if (Test-Path $nameFile) { (Get-Content $nameFile -Raw).Trim() } else { $tty.Name }
                                        $found += [pscustomobject]@{
                                                Device = "/dev/$($tty.Name)"; Description = $desc
                                        }
                                }
                                break
                        }
                        $node = Join-Path $node '..'
                }
        }
        return $found
}

# --- the RP2 BOOTSEL mass-storage volume ---------------------------------------------------

#   the mounted BOOTSEL volume as an object with .Path (a directory a UF2 can be copied INTO)
# and .Name, or $null. Returning a path rather than a drive letter is what makes the callers
# platform-free -- and on Windows it also sidesteps a real failure: Get-Volume can return a
# volume with no DriveLetter, and composing 'C:\...\:\' out of that yields a path that fails to
# copy while a careless script reports success.
function Get-LightBootselVolume {
        if ($IsWindows) {
                $v = Get-Volume -ErrorAction SilentlyContinue |
                        Where-Object { $_.FileSystemLabel -like 'RP*' -and $_.DriveLetter } |
                        Select-Object -First 1
                if (-not $v) { return $null }
                return [pscustomobject]@{ Path = "$($v.DriveLetter):\"; Name = $v.FileSystemLabel }
        }

        #   Linux may or may not automount it. Resolve the label to its device node, then look
        # that device up in /proc/mounts -- present-but-unmounted is a genuinely different
        # situation from absent, and the caller can say so
        $byLabel = '/dev/disk/by-label'
        $node = $null
        if (Test-Path $byLabel) {
                $link = Get-ChildItem $byLabel -ErrorAction SilentlyContinue |
                        Where-Object { $_.Name -like 'RPI-RP*' } | Select-Object -First 1
                if ($link) { $node = (Resolve-Path $link.FullName -ErrorAction SilentlyContinue).Path }
        }
        if (-not $node) { return $null }

        foreach ($line in (Get-Content /proc/mounts -ErrorAction SilentlyContinue)) {
                $parts = $line -split '\s+'
                if ($parts.Count -lt 2) { continue }
                if ($parts[0] -eq $node) {
                        # /proc/mounts octal-escapes spaces and friends
                        $mount = $parts[1] -replace '\\040', ' '
                        return [pscustomobject]@{ Path = $mount; Name = 'RPI-RP2'; Device = $node }
                }
        }
        # known to exist, but nothing has mounted it
        return [pscustomobject]@{ Path = $null; Name = 'RPI-RP2'; Device = $node }
}

#   PowerShell 5.1 has no $IsWindows, so every platform test above would read $null and quietly
# take the Linux branch on Windows. Refusing to load is the only safe answer
if ($PSVersionTable.PSVersion.Major -lt 6) {
        throw "these scripts require PowerShell 7 or later (found $($PSVersionTable.PSVersion)). Windows PowerShell 5.1 lacks the automatic platform variables every check here depends on."
}

Export-ModuleMember -Function Get-LightPlatform, Get-LightExeSuffix, Get-LightPathSeparator,
        Get-LightToolRoot, Find-LightTool, Test-LightWsl, ConvertTo-LightWslPath,
        Find-LightSerialPort, Get-LightBootselVolume
