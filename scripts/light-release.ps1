# Creates an annotated git tag, which is what mints a release version.
#
# WHY THIS EXISTS: with tags as the source of truth, tagging IS releasing -- so the checks
# belong here rather than in a checklist someone remembers. A tag on a dirty tree records a
# version that exists on no one else's machine; a tag that goes backwards breaks every ordering
# comparison downstream; and a duplicate tag silently does nothing useful.
#
# Deliberately does NOT push by default. A local tag is trivially deleted, a pushed one is not.
#
# USAGE:  light-release.ps1 -Version 1.2.0 [-PreRelease rc.1] [-Message <text>] [-Push] [-Path <repo>]
param(
        [Parameter(Mandatory)] [string]$Version,
        [string]$PreRelease,
        [string]$Message,
        [switch]$Push,
        [switch]$AllowDirty,
        [string]$Path
)

$ErrorActionPreference = 'Stop'

$repo = if ($Path) { $Path } else { $PWD.Path }
function git-in { param([string[]]$Arguments) & git -C $repo @Arguments 2>$null }

if (-not (git-in @('rev-parse', '--git-dir'))) { throw "'$repo' is not a git repository" }

$core = $Version -replace '^v', ''
if ($core -notmatch '^(?<maj>\d+)\.(?<min>\d+)\.(?<pat>\d+)$') {
        throw "version '$Version' must be MAJOR.MINOR.PATCH (put any pre-release in -PreRelease)"
}
$maj = [int]$Matches['maj']; $min = [int]$Matches['min']; $pat = [int]$Matches['pat']

if ($PreRelease) {
        if ($PreRelease -notmatch '^[0-9A-Za-z.-]+$') {
                throw "pre-release '$PreRelease' may contain only alphanumerics, dots and hyphens"
        }
        $core = "$core-$PreRelease"
}
$tag = "v$core"

if (-not $AllowDirty -and (git-in @('status', '--porcelain'))) {
        throw "working tree is dirty -- a release must describe a state that exists in the repository. Commit or stash first, or pass -AllowDirty if you know why you want this."
}

if (git-in @('rev-parse', '-q', '--verify', "refs/tags/$tag")) {
        throw "tag '$tag' already exists"
}

#   ordering check against the newest release tag. Pre-releases are excluded from the
# comparison base: v1.3.0-rc.1 does not stop v1.3.0 being released, and comparing against it
# would wrongly reject exactly that.
$previous = git-in @('tag', '--list', 'v[0-9]*', '--sort=-v:refname') | Where-Object { $_ -notmatch '-' } | Select-Object -First 1
if ($previous) {
        $prev = $previous -replace '^v', ''
        if ($prev -match '^(?<maj>\d+)\.(?<min>\d+)\.(?<pat>\d+)$') {
                $prevTuple = @([int]$Matches['maj'], [int]$Matches['min'], [int]$Matches['pat'])
                $newTuple = @($maj, $min, $pat)
                $ordered = $false
                for ($i = 0; $i -lt 3; $i++) {
                        if ($newTuple[$i] -gt $prevTuple[$i]) { $ordered = $true; break }
                        if ($newTuple[$i] -lt $prevTuple[$i]) { break }
                }
                # equal tuples are fine only when this is a pre-release OF that version
                if (-not $ordered -and -not ($PreRelease -and ($newTuple -join '.') -eq ($prevTuple -join '.'))) {
                        throw "version $core does not come after the current release $prev"
                }
        }
}

if (-not $Message) { $Message = "release $core" }

Write-Host "tagging $tag at $(git-in @('rev-parse','--short=7','HEAD'))"
git -C $repo tag -a $tag -m $Message
if ($LASTEXITCODE -ne 0) { throw "git tag failed with exit code $LASTEXITCODE" }

if ($Push) {
        git -C $repo push origin $tag
        if ($LASTEXITCODE -ne 0) { throw "git push failed with exit code $LASTEXITCODE" }
        Write-Host "pushed $tag to origin"
} else {
        Write-Host "created $tag locally. Push it with: git -C $repo push origin $tag"
}
