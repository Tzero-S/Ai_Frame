#!/usr/bin/env bash
# 本地 CI：MinGW 严格自检 + CMake/Ninja 构建 + CTest。
set -u
# MinGW / CMake 均为原生 Windows 程序，路径必须用 Windows 形式（pwd -W），
# 否则 `/d/...` 会被当作无效路径。
ROOT="$(cd "$(dirname "$0")/.." && pwd -W)"

echo "=== [1/3] self-check (g++ strict) ==="
bash "$ROOT/engine/tests/run_selfcheck.sh" || exit 1

CMAKE="${CMAKE:-cmake}"
GPP="${GPP:-D:/PATH/MSYS2/mingw64/bin/g++.exe}"
NINJA="${NINJA:-D:/PATH/Python/Scripts/ninja.exe}"
ORT="${ORT_DIR:-D:/PATH/onnxruntime}"

# ORT 为可选依赖：存在则显式传 ORT_DIR（避免依赖 CMakeCache 里的历史值），
# 不存在则显式关闭，保证干净环境同样能构建。
if [ -f "$ORT/include/onnxruntime_cxx_api.h" ]; then
    ORT_ARG="-DORT_DIR=$ORT"
    echo "ORT: $ORT"
else
    ORT_ARG="-DAIM_WITH_ORT=OFF"
    echo "ORT: 未找到（$ORT）→ AIM_WITH_ORT=OFF"
fi

echo "=== [2/3] cmake configure + build ==="
"$CMAKE" -S "$ROOT/engine" -B "$ROOT/engine/build-mingw" -G Ninja \
    -DCMAKE_CXX_COMPILER="$GPP" -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DCMAKE_BUILD_TYPE=Debug $ORT_ARG || exit 1
"$CMAKE" --build "$ROOT/engine/build-mingw" --parallel || exit 1

echo "=== [3/3] ctest ==="
"$CMAKE" --build "$ROOT/engine/build-mingw" --target test || exit 1
