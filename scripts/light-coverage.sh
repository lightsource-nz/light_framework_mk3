#!/bin/bash
#   Linux-side worker for light-coverage.ps1. The SAME script on both platforms: on Linux it is
# run directly, on Windows it is run inside WSL. Nothing in here knows the difference, and that
# is deliberate -- the platform decision is made once, in light-coverage.ps1, and everything
# below operates on Linux paths that the caller has already translated (a no-op on Linux).
#
#   Kept as its own file, invoked as `bash <this> <params>`, because passing a multi-line script
# through `wsl.exe -- bash -c '...'` mangles variable expansion, and calling it from Git Bash
# path-translates /mnt/c into C:/Program Files/Git/mnt/c. A file and positional arguments
# sidestep both.
#
#   Takes ONE argument: a parameter file to source. Not positional arguments -- wsl.exe joins
# whatever it is given into a single command line which bash then re-parses, so an ignore-regex
# containing '(' or '|' is read as shell syntax and the invocation dies before it starts. A
# sourced file is quoted once, by the writer, and never re-parsed.
#
# USAGE: light-coverage.sh <params-file>
set -u

# shellcheck disable=SC1090
. "$1"        # defines: src, build, html, objglob, ignore, light, functions, reuse, extra_args
# tolerated missing so an older params file (or a caller that predates these) still runs
: "${light:=}" "${functions:=}" "${reuse:=0}"

PROFDATA=llvm-profdata-19
COV=llvm-cov-19
command -v $PROFDATA >/dev/null 2>&1 || { PROFDATA=llvm-profdata; COV=llvm-cov; }

#   -fprofile-instr-generate/-fcoverage-mapping are clang spellings, so CMAKE_CXX_COMPILER has
# to be clang++ as well: these projects enable C, CXX and ASM, and CMake otherwise picks
# /usr/bin/c++ (g++), which rejects the flag while linking its own compiler test.
#   the warning suppressions are not cosmetic -- this code is built with gcc day to day and
# clang is stricter about several things that are not what we are measuring here
#   -Reuse reports off whatever is already in the tree. Guarded on the profile actually being
# there: falling back to a full run is right, silently reporting nothing is not
if [ "$reuse" = "1" ] && [ -f "$build/cov.profdata" ]; then
        echo "== reusing existing profile data =="
        skip_build=1
else
        skip_build=0
fi

if [ "$skip_build" = "0" ]; then
echo "== configuring coverage build =="
cmake -S "$src" -B "$build" -G Ninja \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_BUILD_TYPE=Debug -DLIGHT_RUN_MODE=DEBUG \
        -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping -g -Wno-implicit-function-declaration -Wno-int-conversion" \
        -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" \
        "${extra_args[@]}" >"$build.cfg.log" 2>&1 || { echo "CONFIGURE FAILED"; tail -25 "$build.cfg.log"; exit 1; }

echo "== building =="
cmake --build "$build" >"$build.build.log" 2>&1 || { echo "BUILD FAILED"; tail -25 "$build.build.log"; exit 1; }

echo "== running tests =="
rm -rf "$build/prof"; mkdir -p "$build/prof"
# %p gives each test PROCESS its own counter file; ctest runs many, and a single shared file
# would have them overwrite each other
export LLVM_PROFILE_FILE="$build/prof/%p.profraw"
ctest --test-dir "$build" >"$build.ctest.log" 2>&1
tests=$(grep -E "tests passed" "$build.ctest.log" | head -1)
echo "   $tests"

count=$(ls "$build"/prof/*.profraw 2>/dev/null | wc -l)
if [ "$count" -eq 0 ]; then
        echo "NO PROFILE DATA -- the suite ran no instrumented binaries"
        exit 1
fi
echo "   $count profile files"

$PROFDATA merge -sparse "$build"/prof/*.profraw -o "$build/cov.profdata" || exit 1
fi   # skip_build

#   every test binary has to be named: llvm-cov reads the coverage map out of the objects, so a
# binary left out simply does not appear, silently understating the result
#   'auto' discovers every instrumented executable in the tree, which is what projects whose
# test binaries sit at several different depths need (screen-test puts them under
# light_audio/, rend/ and light_framework/test/ alike). CMakeFiles holds the compiler-probe
# binaries, which carry no coverage map and would only add noise
#   collected as plain paths first, then decorated. -show-functions below needs exactly ONE
# binary given positionally with the rest as -object, and passing them all as -object makes
# llvm-cov read the SOURCE argument as the main binary -- which fails with
# 'not a valid object file' and looks like a broken source path rather than a missing binary
#   the executable BIT is not a reliable filter, and this is the single most important line in
# the file. A build tree on a Windows drive -- /mnt/c under WSL, or the project directory itself
# on a Linux host mounting one -- reports EVERY file as executable, so cmake_install.cmake and
# context.json sail through `-x`. Handing either to llvm-cov fails the whole run with
# 'not recognized as a valid object file' or 'invalid tapi_tbd_version section', neither of
# which points at the actual problem.
#   this check used to guard only the `auto` branch, and the glob branch got away with it purely
# because the tree happened to live on ext4. Both go through it now
is_elf() {
        [ -f "$1" ] || return 1
        [ "$(head -c 4 "$1" 2>/dev/null | tr -d '\0')" = "$(printf '\177ELF')" ]
}

bins=()
if [ "$objglob" = "auto" ]; then
        while IFS= read -r f; do
                is_elf "$f" || continue
                bins+=("$f")
        done < <(find "$build" -type f -executable \
                        ! -path '*/CMakeFiles/*' ! -path '*_deps*' \
                        ! -name '*.so' ! -name '*.so.*' ! -name '*.a' ! -name '*.cmake' 2>/dev/null)
else
        for f in $(eval "ls $build/$objglob" 2>/dev/null); do
                is_elf "$f" || continue
                bins+=("$f")
        done
fi
objs=()
for f in "${bins[@]}"; do objs+=(-object "$f"); done
if [ ${#objs[@]} -eq 0 ]; then
        echo "NO OBJECTS matched '$objglob' under $build"
        exit 1
fi
echo "   $(( ${#objs[@]} / 2 )) instrumented binaries"

echo ""
#   stderr deliberately NOT discarded. llvm-cov reports "functions have mismatched data" and
# similar on stderr while still printing a table, and swallowing that turns a wrong number into
# a silent one
$COV report "${objs[@]}" -instr-profile="$build/cov.profdata" -ignore-filename-regex="$ignore" \
        || { echo "REPORT FAILED"; exit 1; }

#   the per-function view. Sources are searched for under the project AND the framework,
# because a project's build pulls light_core's sources in from outside its own tree and the
# file you want to break down is as often one of those as one of its own
if [ -n "$functions" ]; then
        srcs=()
        while IFS= read -r f; do srcs+=("$f"); done < <(
                find "$src" ${light:+"$light"} -type f -name "$functions" \
                        ! -path '*/build*' ! -path '*_deps*' 2>/dev/null | sort -u)
        echo ""
        if [ ${#srcs[@]} -eq 0 ]; then
                echo "no source file matching '$functions' under $src${light:+ or $light}"
        else
                echo "== per-function coverage: $functions =="
                # first binary positional, the rest as -object: see the note above bins=()
                $COV report "${bins[0]}" "${objs[@]:2}" -instr-profile="$build/cov.profdata" \
                        -show-functions "${srcs[@]}" || echo "PER-FUNCTION REPORT FAILED"
        fi
fi

rm -rf "$html"
$COV show "${objs[@]}" -instr-profile="$build/cov.profdata" -ignore-filename-regex="$ignore" \
        -format=html -output-dir="$html" -show-line-counts-or-regions >/dev/null 2>&1
echo ""
echo "html report: $html/index.html"
