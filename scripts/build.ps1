# 本地构建（§10.2）。工具链策略：
#   主路径 = MinGW-w64 g++ 16.2 + Ninja
#   可选路径 = MSVC v143 + Windows SDK（DirectML EP 需要它）
#
# 依赖策略：ONNX Runtime 可选（与 CMake 的 AIM_WITH_ORT 一致）。
#   找到 ORT → -DORT_DIR=<path>；未找到 → -DAIM_WITH_ORT=OFF（仍可构建，ORT 用例跳过）。
#   MSVC 下若 Windows SDK 的 DirectML.h 可用，自动开 AIM_ENABLE_DML=ON。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/build.ps1
#   powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Toolchain MinGW
#   powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Toolchain Msvc -Config Release
param(
    [ValidateSet('Auto', 'MinGW', 'Msvc')][string]$Toolchain = 'Auto',
    [string]$Config = 'Release'
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$mingwGxx = if ($env:MINGW_GXX) { $env:MINGW_GXX } else { 'D:/PATH/MSYS2/mingw64/bin/g++.exe' }
$ninja    = if ($env:NINJA)     { $env:NINJA }     else { 'D:/PATH/Python/Scripts/ninja.exe' }
$ortDir   = if ($env:ORT_DIR)   { $env:ORT_DIR }   else { 'D:/PATH/onnxruntime' }

# ORT 可选：只按头文件存在与否判定，避免"有 lib 无头"的假阳性
if (Test-Path (Join-Path $ortDir 'include/onnxruntime_cxx_api.h')) {
    $ortArgs = @("-DORT_DIR=$ortDir")
    Write-Host "ORT: $ortDir"
} else {
    $ortArgs = @("-DAIM_WITH_ORT=OFF")
    Write-Host "ORT: 未找到（$ortDir）→ AIM_WITH_ORT=OFF"
}

# 解析 MSVC 工具目录：优先 vswhere（标准 VS 安装），否则用便携安装（可用 MSVC_DIR 覆盖）
function Resolve-Msvc {
    $candidates = @()
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $p = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
        if ($p) { $candidates += $p }
    }
    $candidates += $(if ($env:MSVC_DIR) { $env:MSVC_DIR } else { 'D:/PATH/msvc/2022' })

    foreach ($base in $candidates) {
        $tools = Join-Path $base 'VC/Tools/MSVC'
        if (-not (Test-Path $tools)) { continue }
        $ver = (Get-ChildItem $tools -Directory |
                Sort-Object Name -Descending | Select-Object -First 1).Name
        if (-not $ver) { continue }
        $bin = Join-Path $tools "$ver/bin/Hostx64/x64/cl.exe"
        if (Test-Path $bin) { return @{ Root = (Join-Path $tools $ver); Base = $base } }
    }
    return $null
}

# 解析 Windows SDK：需要 um/ucrt 的 include 与 lib
function Resolve-Sdk {
    $sdkRoot = if ($env:WINDOWS_SDK_DIR) { $env:WINDOWS_SDK_DIR } else { "${env:ProgramFiles(x86)}\Windows Kits\10" }
    if (-not (Test-Path $sdkRoot)) { return $null }
    $incRoot = Join-Path $sdkRoot 'Include'
    if (-not (Test-Path $incRoot)) { return $null }
    $ver = (Get-ChildItem $incRoot -Directory |
            Where-Object { Test-Path (Join-Path $_.FullName 'um/windows.h') } |
            Sort-Object Name -Descending | Select-Object -First 1).Name
    if (-not $ver) { return $null }
    if (-not (Test-Path (Join-Path $sdkRoot "Lib/$ver/um/x64/kernel32.lib"))) { return $null }
    if (-not (Test-Path (Join-Path $sdkRoot "Lib/$ver/ucrt/x64/ucrt.lib"))) { return $null }
    return @{ Root = $sdkRoot; Version = $ver }
}

$msvc = Resolve-Msvc
$sdk  = Resolve-Sdk

if ($Toolchain -eq 'Auto') {
    # 只有 MSVC + SDK 都齐才走 MSVC（否则 MSVC 无法 link）
    $Toolchain = if ($msvc -and $sdk) { 'Msvc' } else { 'MinGW' }
}

if ($Toolchain -eq 'Msvc') {
    if (-not $msvc) {
        throw 'MSVC 不可用：未找到 VC/Tools/MSVC（可用环境变量 MSVC_DIR 指定便携安装根目录）。'
    }
    if (-not $sdk) {
        throw 'Windows SDK 不可用：缺少 um/ucrt 的 include 或 lib（可用 WINDOWS_SDK_DIR 指定）。'
    }

    $env:PATH = "$($msvc.Root)\bin\Hostx64\x64;$($sdk.Root)\bin\$($sdk.Version)\x64;$env:PATH"
    $env:INCLUDE = @(
        "$($msvc.Root)\include",
        "$($sdk.Root)\Include\$($sdk.Version)\ucrt",
        "$($sdk.Root)\Include\$($sdk.Version)\shared",
        "$($sdk.Root)\Include\$($sdk.Version)\um",
        "$($sdk.Root)\Include\$($sdk.Version)\winrt",
        "$($sdk.Root)\Include\$($sdk.Version)\cppwinrt"
    ) -join ';'
    $env:LIB = @(
        "$($msvc.Root)\lib\x64",
        "$($sdk.Root)\Lib\$($sdk.Version)\ucrt\x64",
        "$($sdk.Root)\Lib\$($sdk.Version)\um\x64"
    ) -join ';'

    Write-Host "MSVC: $($msvc.Root)"
    Write-Host "SDK : $($sdk.Root) $($sdk.Version)"

    if (-not (Test-Path $ninja)) { throw "未找到 ninja：$ninja（可用环境变量 NINJA 覆盖）" }

    $build = Join-Path $root 'engine/build-msvc'
    # 注意：PowerShell 对"-Dxxx=$var"这类形似参数的 token 不做变量展开（会原样传给 cmake），
    # 因此必须先拼成字符串再放进数组，用数组 splat 调用。
    $cmakeArgs = @(
        '-S', "$root/engine",
        '-B', $build,
        '-G', 'Ninja',
        '-DCMAKE_CXX_COMPILER=cl',
        "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_BUILD_TYPE=$Config"
    ) + $ortArgs
    cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    cmake --build $build --parallel
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    ctest --test-dir $build --output-on-failure
    exit $LASTEXITCODE
}

# ---- MinGW 主路径 ----
if (-not (Test-Path $mingwGxx)) { throw "未找到 MinGW g++：$mingwGxx（可用环境变量 MINGW_GXX 覆盖）" }
if (-not (Test-Path $ninja))    { throw "未找到 ninja：$ninja（可用环境变量 NINJA 覆盖）" }

$build = Join-Path $root 'engine/build-mingw'
$cmakeArgs = @(
    '-S', "$root/engine",
    '-B', $build,
    '-G', 'Ninja',
    "-DCMAKE_CXX_COMPILER=$mingwGxx",
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_BUILD_TYPE=$Config"
) + $ortArgs
cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $build --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir $build --output-on-failure
exit $LASTEXITCODE
