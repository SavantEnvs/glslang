#!/usr/bin/env bash
#
# mayhem/build.sh — build glslang's fuzz harnesses, the fuzzed `glslang` CLI, and the oracle CLI.
#
# Runs inside the commit image as `mayhem` in /mayhem (SRC). Contract knobs come from the base ENV
# (CC/CXX/SANITIZER_FLAGS/LIB_FUZZING_ENGINE/DEBUG_FLAGS/STANDALONE_FUZZ_MAIN).
#
#   build/fuzz  — libglslang (+ HLSL + SPIR-V back end) and the `glslang` CLI, compiled with
#                 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link (instrumented).
#   build/test  — the `glslang` CLI with upstream's normal Release flags; mayhem/test.sh runs
#                 upstream's Test/runtests golden-output suite with it.
#   out/        — one libFuzzer binary + one -standalone reproducer per harness, and the CLI.
#
# Both build dirs live under build/ and out/ is the output dir: upstream's .gitignore covers both,
# so `git clean -ffdX` removes them. No network: ENABLE_OPT=0 and GLSLANG_TESTS=OFF mean nothing
# under External/ (spirv-tools, googletest) is needed.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT="$SRC/out"
mkdir -p "$OUT"

# SanCov on the library itself (not only the harness TU), unconditionally — also when
# SANITIZER_FLAGS is empty — so Mayhem records edges inside glslang.
FUZZ_FLAGS="$SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS"

common_cmake=(
  -DCMAKE_BUILD_TYPE=Release
  -DENABLE_OPT=0
  -DGLSLANG_TESTS=OFF
  -DBUILD_EXTERNAL=OFF
  -DBUILD_SHARED_LIBS=OFF
  -DENABLE_PCH=OFF
  -DGLSLANG_ENABLE_INSTALL=OFF
)

# 1) Instrumented library + CLI.
cmake -S "$SRC" -B build/fuzz "${common_cmake[@]}" \
  -DCMAKE_C_FLAGS="$FUZZ_FLAGS" \
  -DCMAKE_CXX_FLAGS="$FUZZ_FLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$FUZZ_FLAGS"
cmake --build build/fuzz -j"$MAYHEM_JOBS" \
  --target glslang glslang-default-resource-limits glslang-standalone

B="$SRC/build/fuzz"
LIBS=("$B/glslang/libglslang.a" "$B/glslang/libglslang-default-resource-limits.a")
# Older commits (bug mining) keep real code in these; at HEAD they are empty compatibility stubs.
for extra in SPIRV/libSPIRV.a glslang/libMachineIndependent.a glslang/libGenericCodeGen.a \
             glslang/OSDependent/Unix/libOSDependent.a; do
  [ -f "$B/$extra" ] && LIBS+=("$B/$extra")
done
INC=(-I"$SRC" -I"$B/include" -I"$SRC/mayhem/harnesses")
# Match the library's no-RTTI/no-exceptions build so UBSan's vptr check sees one ABI.
HFLAGS=(-std=c++17 -fno-rtti -fno-exceptions -DENABLE_HLSL -DENABLE_SPIRV -DGLSLANG_OSINCLUDE_UNIX)

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OUT/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -x c -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"

HARNESSES=(compile_fuzzer fuzz_glsl_spirv fuzz_hlsl fuzz_glsl_ast fuzz_preprocess fuzz_link
           fuzz_c_api fuzz_resource_limits)
for h in "${HARNESSES[@]}"; do
  $CXX $FUZZ_FLAGS "${HFLAGS[@]}" "${INC[@]}" -c "$SRC/mayhem/harnesses/$h.cc" -o "$OUT/$h.o"
  $CXX $FUZZ_FLAGS $LIB_FUZZING_ENGINE "$OUT/$h.o" "$OUT/lsan_off.o" "${LIBS[@]}" -lpthread \
    -o "$OUT/$h"
  $CXX $FUZZ_FLAGS "$OUT/standalone_main.o" "$OUT/$h.o" "$OUT/lsan_off.o" "${LIBS[@]}" -lpthread \
    -o "$OUT/$h-standalone"
done

# The fuzzed CLI (legacy `glslangvalidator` target). Relink with the LSan-off hook.
$CXX $FUZZ_FLAGS -o "$OUT/glslang" \
  $(find "$B/StandAlone/CMakeFiles/glslang-standalone.dir" -name '*.o' | sort) \
  "$OUT/lsan_off.o" "${LIBS[@]}" -lpthread

# 2) Oracle build: upstream's normal flags, CLI only.
cmake -S "$SRC" -B build/test "${common_cmake[@]}" \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$COVERAGE_FLAGS"
cmake --build build/test -j"$MAYHEM_JOBS" --target glslang-standalone
test -x build/test/StandAlone/glslang

for h in "${HARNESSES[@]}" glslang; do test -x "$OUT/$h"; done
echo "build.sh: OK"
