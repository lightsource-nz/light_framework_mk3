#!/bin/bash
#   WSL-side worker for light-coverage.ps1. Kept as its own file, invoked as
# `wsl -d <distro> -- bash <this> ...`, because passing a multi-line script through
# `wsl.exe -- bash -c '...'` mangles variable expansion, and calling it from Git Bash
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
. "$1"        # defines: src, build, html, objglob, ignore, extra_args (array)

PROFDATA=llvm-profdata-19
COV=llvm-cov-19
command -v $PROFDATA >/dev/null 2>&1 || { PROFDATA=llvm-profdata; COV=llvm-cov; }

#   -fprofile-instr-generate/-fcoverage-mapping are clang spellings, so CMAKE_CXX_COMPILER has
# to be clang++ as well: these projects enable C, CXX and ASM, and CMake otherwise picks
# /usr/bin/c++ (g++), which rejects the flag while linking its own compiler test.
#   the warning suppressions are not cosmetic -- this code is built with gcc day to day and
# clang is stricter about several things that are not what we are measuring here
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

#   every test binary has to be named: llvm-cov reads the coverage map out of the objects, so a
# binary left out simply does not appear, silently understating the result
#   'auto' discovers every instrumented executable in the tree, which is what projects whose
# test binaries sit at several different depths need (screen-test puts them under
# light_audio/, rend/ and light_framework/test/ alike). CMakeFiles holds the compiler-probe
# binaries, which carry no coverage map and would only add noise
objs=()
if [ "$objglob" = "auto" ]; then
        while IFS= read -r f; do
                #   the executable BIT is not enough. Files copied from /mnt/c commonly carry it
                # regardless of what they are, and handing llvm-cov a context.json fails the
                # whole run with 'invalid tapi_tbd_version section'. Check the ELF magic instead
                [ "$(head -c 4 "$f" 2>/dev/null | tr -d '\0')" = "$(printf '\177ELF')" ] || continue
                objs+=(-object "$f")
        done < <(find "$build" -type f -executable \
                        ! -path '*/CMakeFiles/*' ! -path '*_deps*' \
                        ! -name '*.so' ! -name '*.so.*' ! -name '*.a' ! -name '*.cmake' 2>/dev/null)
else
        for f in $(eval "ls $build/$objglob" 2>/dev/null); do
                [ -f "$f" ] && [ -x "$f" ] && objs+=(-object "$f")
        done
fi
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

rm -rf "$html"
$COV show "${objs[@]}" -instr-profile="$build/cov.profdata" -ignore-filename-regex="$ignore" \
        -format=html -output-dir="$html" -show-line-counts-or-regions >/dev/null 2>&1
echo ""
echo "html report: $html/index.html"
