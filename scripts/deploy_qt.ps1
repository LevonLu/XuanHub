# deploy_qt.ps1 — 手动重新部署 Qt 依赖（正常构建已由 POST_BUILD 自动完成，Qt 升级或未重新链接时才需要）
# 定位 windeployqt：PATH → 构建目录 CMakeCache.txt 的 Qt6_DIR 反推，不写死 Qt 安装路径
param(
    [Parameter(Mandatory = $false)]
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",

    [Parameter(Mandatory = $false)]
    [string]$BuildDir = "build/default"
)

$ErrorActionPreference = "Stop"

function Resolve-WinDeployQt {
    # 1) PATH 上直接能找到（Qt bin 已入 PATH 的开发机）
    $cmd = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    # 2) 从 CMake 缓存的 Qt6_DIR（形如 <qt>/lib/cmake/Qt6）反推 bin 目录
    $cache = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path $cache) {
        $match = Select-String -Path $cache -Pattern '^Qt6_DIR:PATH=(.+)$' | Select-Object -First 1
        if ($match) {
            $qtRoot = $match.Matches[0].Groups[1].Value -replace '[/\\]lib[/\\]cmake[/\\]Qt6$', ''
            $candidate = Join-Path $qtRoot "bin/windeployqt.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }

    throw "未找到 windeployqt.exe：请把 Qt 的 bin 目录加入 PATH，或先执行 pixi run configure"
}

$windeployqt = Resolve-WinDeployQt
$exe = Join-Path $BuildDir "dist/$Config/XuanHub.exe"
if (-not (Test-Path $exe)) { throw "找不到 $exe，请先构建（pixi run build）" }

# --qmldir 不可少：不带它 windeployqt 不扫描 QML import，会漏 QML 插件 DLL
$mode = "--release"
if ($Config -eq "Debug") { $mode = "--debug" }
& $windeployqt $mode --qmldir app/qml $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败（exit $LASTEXITCODE）" }
