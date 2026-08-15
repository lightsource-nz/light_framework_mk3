# Mutation check for light_stream's message API.
#
# WHY THIS EXISTS: this suite found a real bug on its first run -- light_stream_message_vf_sync()
# passed a va_list into a VARIADIC handler, so every conversion in a synchronous message printed
# garbage ("value is %d" with 42 gave "value is 241170208"). It had sat there unnoticed because
# nothing calls the sync path, and because a message with no conversions came out fine.
#
# That is the argument for mutating rather than trusting the green run: the suite is new, the
# code it covers was untested for its whole life, and a suite written after the fact can easily
# assert only what the implementation already does.
#
# The mutants below are the ways this transport could plausibly break: reintroducing the va_list
# bug, writing the format string unformatted, the fast path writing through instead of queueing,
# a slot bound that does not bound, wrong flags, and a mode setter that does nothing.
#
# HOW IT WORKS: patches module/light_core/src/stream.c in place, rebuilds, runs the
# light_stream_message suite, restores. The source is always restored, including on Ctrl-C, but
# it IS edited in your working tree while this runs.
#
# USAGE:  pwsh test/test_light_stream_message/mutants.ps1 [-BuildDir <path>]
param(
        [string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build')
)

$ErrorActionPreference = 'Stop'
$src = (Resolve-Path (Join-Path $PSScriptRoot '..\..\module\light_core\src\stream.c')).Path

if (-not (Test-Path $BuildDir)) {
        Write-Error "no build directory at $BuildDir -- configure a HOST build first, or pass -BuildDir"
}
$BuildDir = (Resolve-Path $BuildDir).Path

# single-line search strings only: the file on disk may be CRLF while a PowerShell literal is
# LF, so a multi-line search silently never matches -- which looks exactly like a caught mutant
$mutants = @(
 @('sync hands a va_list to a variadic handler (the original bug)',
   '        stream->handler(stream, (const char *)text);',
   '        stream->handler_va(stream, format, args);'),
 @('sync writes the format string unformatted',
   '        stream->handler(stream, (const char *)text);',
   '        stream->handler(stream, (const char *)format);'),
 @('the fast path writes through instead of queueing',
   '        mqueue_put_formatted(&stream->lock, &stream->queue, LIGHT_MSG_FAST, format, args);',
   '        light_stream_message_vf_sync(stream, format, args);'),
 @('the queue slot bound does not bound',
   '        vsnprintf((char *)queue->message[index].text, LIGHT_STREAM_MAX_MSG_LENGTH, (const char *)format, args);',
   '        vsnprintf((char *)queue->message[index].text, LIGHT_STREAM_MAX_MSG_LENGTH * 4, (const char *)format, args);'),
 @('queued messages carry the wrong flags',
   '        queue->message[index].flags = flags;',
   '        queue->message[index].flags = LIGHT_MSG_FASTER;'),
 @('the background mode setter does nothing',
   '        atomic_store(&stream->mode, mode);',
   '        ;')
)

$original = Get-Content $src -Raw
$restored = $false
function Restore-Source {
        if (-not $script:restored) {
                Set-Content $script:src -Value $script:original -NoNewline
                $script:restored = $true
                Write-Host "source restored"
        }
}
trap { Restore-Source; break }

$survivors = @()
try {
        Write-Host "=== baseline (unmutated) ==="
        & cmake --build $BuildDir --target test_light_stream_message 2>&1 | Out-Null
        & ctest --test-dir $BuildDir -R '^light_stream_message\.' 2>&1 | Select-Object -Last 1

        Write-Host "`n=== mutants (each SHOULD fail) ==="
        foreach ($m in $mutants) {
                $name, $find, $replace = $m
                $patched = $original.Replace($find, $replace)
                if ($patched -eq $original) {
                        "{0,-46} !! anchor not found -- has stream.c moved on?" -f $name
                        $survivors += "$name (anchor missing)"
                        continue
                }
                Set-Content $src -Value $patched -NoNewline
                & cmake --build $BuildDir --target test_light_stream_message 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) {
                        # a mutant that will not compile is still killed -- it cannot ship
                        "{0,-46} -> killed (build)" -f $name
                } else {
                        #   --timeout because one of these puts the sync path back on a code
                        # route that takes the stream lock twice; an unbounded hang would stall
                        # the harness rather than report a result
                        & ctest --test-dir $BuildDir -R '^light_stream_message\.' --timeout 20 2>&1 | Out-Null
                        if ($LASTEXITCODE -eq 0) {
                                "{0,-46} -> *** SURVIVED ***" -f $name
                                $survivors += $name
                        } else {
                                "{0,-46} -> killed" -f $name
                        }
                }
                Set-Content $src -Value $original -NoNewline
        }
} finally {
        Restore-Source
        & cmake --build $BuildDir --target test_light_stream_message 2>&1 | Out-Null
}

Write-Host ""
if ($survivors.Count -gt 0) {
        Write-Host "$($survivors.Count) of $($mutants.Count) mutants survived:" -ForegroundColor Red
        $survivors | ForEach-Object { Write-Host "  $_" }
        exit 1
}
Write-Host "all $($mutants.Count) mutants killed" -ForegroundColor Green
