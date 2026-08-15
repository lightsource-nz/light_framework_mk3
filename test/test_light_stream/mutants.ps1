# Mutation check for the stream message queue.
#
# WHY THIS EXISTS: unlike test_light_list, this suite found no bugs -- the queue was already
# correct. That makes the mutation check MORE important, not less: a suite written against
# working code has nothing to prove it would have noticed had the code been wrong, and coverage
# alone cannot tell the difference between exercising a line and checking it.
#
# The mutants are drawn from what this circular buffer could plausibly get wrong: an off-by-one
# in the full condition (which on a single-core drain path is the difference between a wasted
# slot and a hard deadlock), a missing modulo so indices never wrap, a peek that consumes, an
# advance that does not, and FIFO becoming LIFO.
#
# HOW IT WORKS: patches src/stream.c in place, rebuilds, runs the light_stream suite, restores.
# The source is always restored, including on Ctrl-C, but it IS edited in your working tree
# while this runs.
#
# USAGE:  pwsh test/test_light_stream/mutants.ps1 [-BuildDir <path>]
param(
        [string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build')
)

$ErrorActionPreference = 'Stop'
$src = (Resolve-Path (Join-Path $PSScriptRoot '..\..\module\light_core\src\stream.c')).Path

if (-not (Test-Path $BuildDir)) {
        Write-Error "no build directory at $BuildDir -- configure a HOST build first, or pass -BuildDir"
}
$BuildDir = (Resolve-Path $BuildDir).Path

# single-line search strings only: the patcher is a literal replace, not a parser
$mutants = @(
 @('is_full is one slot late (would let a caller into a blocking claim)',
   '        return (atomic_load(&queue->count) >= LIGHT_STREAM_MQUEUE_DEPTH);',
   '        return (atomic_load(&queue->count) > LIGHT_STREAM_MQUEUE_DEPTH);'),
 @('is_full is one slot early (wastes a slot)',
   '        return (atomic_load(&queue->count) >= LIGHT_STREAM_MQUEUE_DEPTH);',
   '        return (atomic_load(&queue->count) >= LIGHT_STREAM_MQUEUE_DEPTH - 1);'),
 @('is_empty never reports empty',
   '        return (atomic_load(&queue->count) == 0);',
   '        return false;'),
 @('the write index never wraps',
   '        queue->head = (queue->head + 1) % LIGHT_STREAM_MQUEUE_DEPTH;',
   '        queue->head = queue->head + 1;'),
 @('the read index never wraps',
   '        uint8_t message_idx = (LIGHT_STREAM_MQUEUE_DEPTH + (queue->head - queue->count)) % LIGHT_STREAM_MQUEUE_DEPTH;',
   '        uint8_t message_idx = queue->head - queue->count;'),
 @('peek returns the newest rather than the oldest (FIFO becomes LIFO)',
   '        uint8_t message_idx = (LIGHT_STREAM_MQUEUE_DEPTH + (queue->head - queue->count)) % LIGHT_STREAM_MQUEUE_DEPTH;',
   '        uint8_t message_idx = (LIGHT_STREAM_MQUEUE_DEPTH + (queue->head - 1)) % LIGHT_STREAM_MQUEUE_DEPTH;'),
 @('advance does not consume',
   '        queue->count--;',
   '        ;'),
 @('claim_slot does not advance the write index',
   '        queue->head = (queue->head + 1) % LIGHT_STREAM_MQUEUE_DEPTH;',
   '        ;')
)

$original = Get-Content $src -Raw
$restored = $false
function Restore-Source {
        if (-not $script:restored) {
                Set-Content $script:src -Value $script:original -NoNewline
                $script:restored = $true
        }
}
trap { Restore-Source; break }

$survivors = @()
try {
        foreach ($m in $mutants) {
                $name = $m[0]; $find = $m[1]; $replace = $m[2]

                if (-not $original.Contains($find)) {
                        Write-Host "SKIP  $name -- anchor not found, this harness has drifted from stream.c" -ForegroundColor Yellow
                        $survivors += "$name (anchor missing)"
                        continue
                }
                Set-Content $src -Value $original.Replace($find, $replace) -NoNewline

                cmake --build $BuildDir --target test_light_stream 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) {
                        # a mutant that will not compile is still killed -- it cannot ship
                        Write-Host "killed (build) $name" -ForegroundColor Green
                        continue
                }
                #   --timeout matters here in a way it did not for the list suite: several of
                # these mutants can make a producer wait on a queue that never drains, and an
                # unbounded hang would stall this harness rather than report a survivor
                ctest --test-dir $BuildDir -R light_stream --timeout 20 2>&1 | Out-Null
                if ($LASTEXITCODE -eq 0) {
                        Write-Host "SURVIVED       $name" -ForegroundColor Red
                        $survivors += $name
                } else {
                        Write-Host "killed         $name" -ForegroundColor Green
                }
        }
} finally {
        Restore-Source
        cmake --build $BuildDir --target test_light_stream 2>&1 | Out-Null
}

Write-Host ""
if ($survivors.Count -gt 0) {
        Write-Host "$($survivors.Count) of $($mutants.Count) mutants survived:" -ForegroundColor Red
        $survivors | ForEach-Object { Write-Host "  $_" }
        exit 1
}
Write-Host "all $($mutants.Count) mutants killed" -ForegroundColor Green
