#!/usr/bin/env bash
# 单元自检脚本（Agent 模式自测用）。
# 工具链：MinGW-w64 g++ 16.2（本机 MSVC 缺 Windows SDK 导入库，无法 link；
# 代码保持标准 C++20，兼容 MSVC /W4 /permissive-；生产构建主路径为 MinGW+CMake（MSVC 可选）。
#
# ONNX Runtime 为可选依赖（与 CMake 的 AIM_WITH_ORT 一致）：
#   找到 ORT（含 include/onnxruntime_cxx_api.h）→ -DAIM_WITH_ORT=1 并链接真实后端；
#   未找到 → 按 AIM_WITH_ORT=OFF 自检，ORT 相关用例显式 SKIP，脚本仍然可绿。
#
# 已知工具链问题：MinGW GCC 16.2 的 cc1plus 在 waccess pass 偶发
# "internal compiler error: Segmentation fault"（同一文件重编即过，属编译器 bug）。
# 本脚本对"编译失败且 stderr 含 internal compiler error"的用例自动重试一次。
set -u
# 注意：MinGW g++ 为原生 Windows 程序，源文件/输出必须用 Windows 形式路径
# （/d/... 会被 cc1plus 当作无效路径）；因此 ROOT 统一取 pwd -W。
ROOT="$(cd "$(dirname "$0")/.." && pwd -W)"
GPP="${GPP:-/d/PATH/MSYS2/mingw64/bin/g++}"
ORT="${ORT_DIR:-D:/PATH/onnxruntime}"

INCS="-I$ROOT/src -I$ROOT/tests"
FIXTURE="-DAIM_TEST_FIXTURE_DIR=\"$ROOT/tests/fixtures\""
LIBS="-lkernel32 -luser32 -lgdi32 -ld3d11 -ldxgi -lshell32 -lws2_32 -lsetupapi -ladvapi32 -lgdiplus -lole32 -pthread"
FLAGS="-std=c++20 -Wall -Wextra -Wpedantic -O0"

HAVE_ORT=0
if [ -f "$ORT/include/onnxruntime_cxx_api.h" ]; then
  HAVE_ORT=1
  FLAGS="$FLAGS -DAIM_WITH_ORT=1 -D_stdcall=__stdcall -I$ORT/include"
  LIBS="$LIBS $ORT/lib/onnxruntime.lib"
  cp -f "$ORT/bin/onnxruntime.dll" "$ROOT/tests/unit/" 2>/dev/null || true
else
  echo "NOTE: 未找到 ONNX Runtime（$ORT），按 AIM_WITH_ORT=OFF 自检；ORT 相关用例将 SKIP"
fi

# 对象目录按 ORT 开关分桶：避免切换 AIM_WITH_ORT 后复用陈旧 .o（脚本只按 mtime 增量编译）
if [ "$HAVE_ORT" -eq 1 ]; then
  BUILD="$ROOT/tests/unit/.build"
else
  BUILD="$ROOT/tests/unit/.build-noort"
fi
mkdir -p "$BUILD"

FAIL=0

# 编译一次；若失败且原因是 cc1plus 内部错误（GCC 16.2 偶发 ICE，非代码问题），
# 最多重试 3 次（ICE 偶发，连续命中概率很低）。仍失败才算真失败。
try_compile() { # $1=label $2=errfile, 其余为 g++ 参数
  local label="$1" errf="$2"
  shift 2
  if "$GPP" "$@" 2>"$errf"; then return 0; fi
  local attempt=1
  while [ "$attempt" -le 3 ]; do
    if ! grep -q "internal compiler error" "$errf" 2>/dev/null; then break; fi
    echo "RETRY $label (cc1plus ICE，第 $attempt 次重试)"
    sleep 0.2
    if "$GPP" "$@" 2>"$errf"; then return 0; fi
    attempt=$((attempt + 1))
  done
  return 1
}

echo "=== self-check: compiling engine sources ==="
OBJS=""
for s in $(find "$ROOT/src" -name '*.cpp' ! -name 'main.cpp'); do
  rel="${s#$ROOT/src/}"
  obj="$BUILD/$(echo "$rel" | tr '/' '_' | sed 's/\.cpp$/.o/')"
  need_build=0
  if [ ! -f "$obj" ] || [ "$s" -nt "$obj" ]; then
    need_build=1
  elif [ -n "$(find "$ROOT/src" -name '*.h' -newer "$obj" -print -quit 2>/dev/null)" ]; then
    # 头文件（尤其 spi.h 这类含虚函数的接口）变更后必须重编，
    # 否则旧对象与新 vtable 布局不一致，会静默产生错误行为（ODR 违规）。
    need_build=1
  fi
  if [ "$need_build" -eq 1 ]; then
    if ! try_compile "src:$rel" "$obj.err" $FLAGS $INCS -c "$s" -o "$obj"; then
      echo "FAIL(compile-src) $rel"; tail -n 25 "$obj.err"; exit 1
    fi
  fi
  OBJS="$OBJS $obj"
done

echo "=== self-check: building & running tests ==="
for t in "$ROOT"/tests/unit/*_test.cpp; do
  [ -e "$t" ] || continue
  name=$(basename "$t" .cpp)
  if [ "$HAVE_ORT" -eq 0 ]; then
    case "$name" in
      ort_test|real_model_test|e2e_real_test|e2e_test|preview_test|model_import_test)
        echo "SKIP  $name (AIM_WITH_ORT=OFF)"
        continue
        ;;
    esac
  fi
  out="$ROOT/tests/unit/$name.out"
  err="$ROOT/tests/unit/$name.err"
  if try_compile "$name" "$err" $FLAGS $INCS "$FIXTURE" "$t" $OBJS -o "$out" $LIBS; then
    if "$out" > "$out.log" 2>&1; then
      echo "PASS  $name"
    else
      echo "FAIL(tests) $name"; tail -n 20 "$out.log"; FAIL=1
    fi
  else
    echo "FAIL(compile) $name"; tail -n 30 "$err"; FAIL=1
  fi
done

echo "=== self-check: building engine binary ==="
ENGINE="$ROOT/tests/unit/engine_selfcheck.out"
if try_compile "main.cpp" "$ROOT/tests/unit/engine.err" $FLAGS $INCS "$ROOT/src/main.cpp" $OBJS -o "$ENGINE" $LIBS; then
  if "$ENGINE" --selfcheck > "$ROOT/tests/unit/engine.log" 2>&1; then
    echo "PASS  engine --selfcheck"
  else
    echo "FAIL(engine --selfcheck)"; tail -n 20 "$ROOT/tests/unit/engine.log"; FAIL=1
  fi
else
  echo "FAIL(compile main)"; tail -n 30 "$ROOT/tests/unit/engine.err"; FAIL=1
fi

echo "=== self-check exit: $FAIL ==="
exit $FAIL
