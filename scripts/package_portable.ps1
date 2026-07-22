<#
.SYNOPSIS
    Package XuanHub as portable distribution.

.DESCRIPTION
    Collects all dependencies (MSVC runtime DLLs, Python stdlib),
    creates portable marker file, prunes unnecessary files, and archives to zip.

.PARAMETER Config
    Build configuration: Release or Debug. Default: Release.

.PARAMETER ProjectRoot
    Project root directory. Default: parent of this script's directory.

.PARAMETER OutputDir
    Output directory for the zip archive. Default: dist/

.PARAMETER NoZip
    Only prepare portable directory, skip zip creation.

.PARAMETER PythonStrategy
    Python stdlib bundling strategy:
      None     - Skip stdlib. DEFAULT. Python 子插件依赖用户自装 Python 3.12+；
                 未安装的用户机器上 pybridge 自动停用并弹提示，其余功能不受影响。
      Bundle   - Copy Lib/ (excluding site-packages) and DLLs/ from system
                 Python install (~30 MB); Python 子插件开箱即用，无需用户安装。
      Embedded - Download python313.zip from python.org (~3 MB). Experimental.

.PARAMETER NoPrune
    Skip pruning of unnecessary files.
#>

param(
    [Parameter(Mandatory = $false)]
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",

    [Parameter(Mandatory = $false)]
    [string]$ProjectRoot = "",

    [Parameter(Mandatory = $false)]
    [string]$OutputDir = "",

    [Parameter(Mandatory = $false)]
    [switch]$NoZip,

    [Parameter(Mandatory = $false)]
    [ValidateSet("Embedded", "Bundle", "None")]
    [string]$PythonStrategy = "None",

    [Parameter(Mandatory = $false)]
    [switch]$NoPrune
)

$ErrorActionPreference = "Stop"

# 定位本机 Python ≥3.12 安装目录（nanobind 稳定 ABI 硬性下限）：
# 注册表枚举全部 PythonCore 版本（取最高）→ py launcher → PATH；
# 排除 WindowsApps 商店占位 stub
function Resolve-PythonHome {
    $candidates = @()
    foreach ($root in @('HKCU:\Software\Python\PythonCore', 'HKLM:\Software\Python\PythonCore')) {
        Get-ChildItem $root -ErrorAction SilentlyContinue | ForEach-Object {
            $ver = $_.PSChildName   # 形如 "3.13"（32 位安装带 "-32" 后缀）
            if ($ver -match '^3\.(\d+)') {
                $home = (Get-ItemProperty "$root\$ver\InstallPath" -ErrorAction SilentlyContinue).'(default)'
                if ($home -and (Test-Path (Join-Path $home 'python.exe'))) {
                    $candidates += [pscustomobject]@{ Minor = [int]$Matches[1]; Home = $home.TrimEnd('\', '/') }
                }
            }
        }
    }
    $best = $candidates | Where-Object { $_.Minor -ge 12 } |
            Sort-Object Minor -Descending | Select-Object -First 1
    if ($best) { return $best.Home }
    $pyLauncher = @(Get-Command py -ErrorAction SilentlyContinue)[0]
    if ($pyLauncher) {
        $exe = & $pyLauncher.Source -3 -c "import sys; print(sys.executable)" 2>$null
        $minor = & $pyLauncher.Source -3 -c "import sys; print(sys.version_info[1])" 2>$null
        if ($exe -and [int]$minor -ge 12 -and (Test-Path "$exe")) { return (Split-Path "$exe" -Parent) }
    }
    $cmd = @(Get-Command python -ErrorAction SilentlyContinue |
             Where-Object { $_.Source -notlike '*WindowsApps*' })[0]
    if ($cmd) {
        $exe = & $cmd.Source -c "import sys; print(sys.executable)" 2>$null
        $minor = & $cmd.Source -c "import sys; print(sys.version_info[1])" 2>$null
        if ($exe -and [int]$minor -ge 12 -and (Test-Path "$exe")) { return (Split-Path "$exe" -Parent) }
    }
    return $null
}

# Resolve ProjectRoot if not provided: use script's parent directory
if (-not $ProjectRoot) {
    $ProjectRoot = Split-Path $PSScriptRoot -Parent
}

# Resolve OutputDir if not provided
if (-not $OutputDir) {
    $OutputDir = Join-Path $ProjectRoot "dist"
}

$DistDir = Join-Path $ProjectRoot "build\default\dist\$Config"

# Read version from CMakeLists.txt
$CmakeFile = Join-Path $ProjectRoot "CMakeLists.txt"
$versionMatch = Select-String -Path $CmakeFile -Pattern 'VERSION\s+(\d+\.\d+\.\d+)'
if ($versionMatch) {
    $Version = $versionMatch.Matches[0].Groups[1].Value
} else {
    $Version = "0.1.0"
}

Write-Host "=============================================="
Write-Host "  XuanHub Portable Packaging Tool  v$Version"
Write-Host "=============================================="
Write-Host ""
Write-Host "[info] Config:    $Config"
Write-Host "[info] Version:   $Version"
Write-Host "[info] Dist dir:  $DistDir"

# ---- Step 1: Verify dist directory ----
Write-Host "`n[1/6] Verifying build artifacts..."
$exe = Join-Path $DistDir "XuanHub.exe"
if (-not (Test-Path $exe)) {
    Write-Error "XuanHub.exe not found. Build first: cmake --build --preset default"
    exit 1
}
Write-Host "        XuanHub.exe: OK"

# ---- Step 2: Copy MSVC runtime DLLs ----
Write-Host "`n[2/6] Collecting MSVC runtime DLLs..."

$vcrtDlls = @(
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll",
    "concrt140.dll",
    "vccorlib140.dll"
)

$vcrtSource = $null

# 1) Try vswhere for VS 2022
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -property installationPath 2>$null
    if ($vsPath) {
        $candidate = Join-Path $vsPath "VC\Redist\MSVC"
        $latest = Get-ChildItem $candidate -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($latest) {
            $testPath = Join-Path $latest.FullName "onecore\x64\Microsoft.VC143.CRT"
            if (Test-Path (Join-Path $testPath "vcruntime140.dll")) {
                $vcrtSource = $testPath
            }
        }
    }
}

# 2) Fallback: known VS install paths
if (-not $vcrtSource) {
    $knownPaths = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"
    )
    foreach ($base in $knownPaths) {
        if (Test-Path $base) {
            $latest = Get-ChildItem $base -Directory -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending | Select-Object -First 1
            if ($latest) {
                $testPath = Join-Path $latest.FullName "onecore\x64\Microsoft.VC143.CRT"
                if (Test-Path (Join-Path $testPath "vcruntime140.dll")) {
                    $vcrtSource = $testPath
                    break
                }
            }
        }
    }
}

# 3) Fallback: System32
if (-not $vcrtSource) {
    $sys32 = Join-Path $env:SystemRoot "System32"
    if (Test-Path (Join-Path $sys32 "vcruntime140.dll")) {
        $vcrtSource = $sys32
    }
}

if ($vcrtSource) {
    Write-Host "        Source: $vcrtSource"
    $copied = 0
    foreach ($dll in $vcrtDlls) {
        $src = Join-Path $vcrtSource $dll
        $dst = Join-Path $DistDir $dll
        if ((Test-Path $src) -and -not (Test-Path $dst)) {
            Copy-Item $src $dst -ErrorAction SilentlyContinue
            if (Test-Path $dst) {
                Write-Host "        + $dll"
                $copied++
            }
        }
    }
    Write-Host "        Copied $copied DLL(s)"
} else {
    Write-Warning "MSVC runtime DLLs not found. Package may not run on machines without VS."
}

# ---- Step 3: Python standard library ----
Write-Host "`n[3/6] Preparing Python standard library (strategy: $PythonStrategy)..."

# Clean up artifacts from a previous run with a different strategy
if ($PythonStrategy -ne "Bundle") {
    foreach ($d in @("Lib", "DLLs")) {
        $oldDir = Join-Path $DistDir $d
        if (Test-Path $oldDir) {
            Remove-Item $oldDir -Recurse -Force -ErrorAction SilentlyContinue
            Write-Host "        Removed stale $d/ directory"
        }
    }
    # 解释器 DLL 只在 Bundle 策略下随包分发，其余策略清掉旧产物
    Get-ChildItem $DistDir -Filter 'python3*.dll' -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
            Write-Host "        Removed stale $($_.Name)"
        }
}
if ($PythonStrategy -ne "Embedded") {
    foreach ($f in @("python313.zip", "python313._pth")) {
        $p = Join-Path $DistDir $f
        if (Test-Path $p) {
            Remove-Item $p -Force -ErrorAction SilentlyContinue
            Write-Host "        Removed stale $f"
        }
    }
}

if ($PythonStrategy -eq "Embedded") {
    # ---- Embedded: download python313.zip from python.org (~3 MB) ----
    $resolvedHome = Resolve-PythonHome
    if (-not $resolvedHome) {
        Write-Warning "Python 3.12+ not found; falling back to Bundle strategy"
        $PythonStrategy = "Bundle"
    } else {
        $pythonExe = Join-Path $resolvedHome 'python.exe'
        $pyVer = & $pythonExe -c "import sys; print('{}.{}.{}'.format(sys.version_info.major, sys.version_info.minor, sys.version_info.micro))" 2>$null
        if (-not $pyVer) {
            Write-Warning "Cannot determine Python version; falling back to Bundle strategy"
            $PythonStrategy = "Bundle"
        } else {
            Write-Host "        Target Python: $pyVer"

            $embedZip = "python-$pyVer-embed-amd64.zip"
            $embedUrl = "https://www.python.org/ftp/python/$pyVer/$embedZip"

            # Cache the downloaded embeddable zip in third_party/ (gitignored)
            $cacheDir = Join-Path $ProjectRoot "third_party"
            New-Item -ItemType Directory -Path $cacheDir -Force -ErrorAction SilentlyContinue | Out-Null
            $cachedZip = Join-Path $cacheDir $embedZip

            if (-not (Test-Path $cachedZip)) {
                Write-Host "        Downloading $embedUrl ..."
                try {
                    $ProgressPreference = 'SilentlyContinue'
                    Invoke-WebRequest -Uri $embedUrl -OutFile $cachedZip -ErrorAction Stop
                } catch {
                    Write-Warning "Download failed: $_"
                    Write-Warning "Falling back to Bundle strategy"
                    $PythonStrategy = "Bundle"
                }
            } else {
                Write-Host "        Using cached: $cachedZip"
            }

            if ($PythonStrategy -eq "Embedded") {
                $dstZip = Join-Path $DistDir "python313.zip"
                $pthFile = Join-Path $DistDir "python313._pth"
                try {
                    Add-Type -AssemblyName System.IO.Compression.FileSystem
                    $archive = [System.IO.Compression.ZipFile]::OpenRead($cachedZip)
                    $entry = $archive.Entries | Where-Object { $_.Name -eq "python313.zip" } | Select-Object -First 1
                    if ($entry) {
                        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dstZip, $true)
                        Write-Host ("        + python313.zip ({0:N1} MB)" -f ((Get-Item $dstZip).Length / 1MB))
                    } else {
                        Write-Warning "python313.zip not found in embeddable archive; falling back to Bundle"
                        $PythonStrategy = "Bundle"
                    }
                } catch {
                    Write-Warning "Extraction failed: $_"
                    Write-Warning "Falling back to Bundle strategy"
                    $PythonStrategy = "Bundle"
                } finally {
                    if ($archive) { $archive.Dispose() }
                }

                if ($PythonStrategy -eq "Embedded") {
                    # Write a minimal ._pth file so embedded CPython can locate
                    # python313.zip without PYTHONHOME being set.
                    #   Line 1: python313.zip  -> stdlib from the zip
                    #   Line 2: .              -> allow imports from exe dir
                    # We intentionally do NOT "import site" -- the portable
                    # distro has no site-packages, and enabling site would
                    # try to write .pyc caches to user directories.
                    "python313.zip" + [Environment]::NewLine + "." |
                        Out-File -FilePath $pthFile -Encoding ascii
                    Write-Host "        + python313._pth (embedded path config)"
                }
            }
        }
    }
}

if ($PythonStrategy -eq "Bundle") {
    # ---- Bundle: copy Lib/ and DLLs/ from system Python install (~30 MB) ----
    # Lib/  纯 Python 标准库；DLLs/  编译扩展模块（_socket/_ssl/zlib 等 .pyd
    #       及其依赖 DLL，如 libcrypto）——缺了它 urllib/HTTPS 在无 Python
    #       的机器上必挂。site-packages 必须排除：那是打包机的第三方包
    #       （动辄 GB 级），插件依赖应由插件目录自行 vendor。
    $pythonHome = Resolve-PythonHome
    if (-not $pythonHome) {
        Write-Warning "Python 3.12+ not found (registry/py launcher/PATH); skipping Python stdlib entirely"
    } else {
        Write-Host "        Python home: $pythonHome"

        # 解释器 DLL 进包（M12 稳定 ABI）：python3.dll（转发器）+ python3XX.dll
        #（实际运行时）。构建期 CMake 不再拷贝它们，打包时随标准库一并搬运
        foreach ($dll in (Get-ChildItem $pythonHome -Filter 'python3*.dll' -File)) {
            Copy-Item $dll.FullName $DistDir -Force
            Write-Host "        + $($dll.Name)"
        }

        # 刷新式拷贝：先删再拷，保证排除项（site-packages）对旧产物也生效
        foreach ($d in @("Lib", "DLLs")) {
            $stale = Join-Path $DistDir $d
            if (Test-Path $stale) {
                Remove-Item $stale -Recurse -Force
                Write-Host "        Removed existing $d/ for refresh"
            }
        }

        # Lib/：排除 site-packages 与开发/教学用模块
        $pythonLib = Join-Path $pythonHome "Lib"
        $dstLib = Join-Path $DistDir "Lib"
        if (-not (Test-Path $pythonLib)) {
            Write-Warning "Python Lib/ not found: $pythonLib"
        } else {
            Write-Host "        Source: $pythonLib"
            Write-Host "        Copying Lib/ (excluding site-packages/test/idlelib/ensurepip...)..."

            $robocopyArgs = @(
                $pythonLib,
                $dstLib,
                "/E",
                "/XD", "site-packages", "test", "idlelib", "turtledemo", "distutils", "ensurepip", "__pycache__",
                "/XF", "*.pyc",
                "/NJH", "/NJS",
                "/NDL"
            )

            $rc = (Start-Process robocopy -ArgumentList $robocopyArgs -Wait -PassThru -NoNewWindow).ExitCode
            if ($rc -ge 8) {
                Write-Warning "robocopy exit code $rc, Lib/ copy may be incomplete"
            } else {
                $libSize = (Get-ChildItem $dstLib -Recurse -File | Measure-Object -Property Length -Sum).Sum
                Write-Host ("        Lib/ done, size: {0:N1} MB" -f ($libSize / 1MB))
            }
        }

        # DLLs/：编译扩展模块与其依赖 DLL（.pyd 的依赖经 LOAD_WITH_ALTERED_SEARCH_PATH
        # 在同目录解析，整目录搬运即可）
        $pythonDlls = Join-Path $pythonHome "DLLs"
        $dstDlls = Join-Path $DistDir "DLLs"
        if (-not (Test-Path $pythonDlls)) {
            Write-Warning "Python DLLs/ not found: $pythonDlls (network plugins will fail on machines without Python)"
        } else {
            Write-Host "        Source: $pythonDlls"
            $robocopyArgs = @(
                $pythonDlls,
                $dstDlls,
                "/E",
                "/XD", "__pycache__",
                "/XF", "*.pyc",
                "/NJH", "/NJS",
                "/NDL"
            )
            $rc = (Start-Process robocopy -ArgumentList $robocopyArgs -Wait -PassThru -NoNewWindow).ExitCode
            if ($rc -ge 8) {
                Write-Warning "robocopy exit code $rc, DLLs/ copy may be incomplete"
            } else {
                $dllsSize = (Get-ChildItem $dstDlls -Recurse -File | Measure-Object -Property Length -Sum).Sum
                Write-Host ("        DLLs/ done, size: {0:N1} MB" -f ($dllsSize / 1MB))
            }
        }
    }
}

if ($PythonStrategy -eq "None") {
    Write-Host "        Python stdlib NOT bundled."
    Write-Host "        Python sub-plugins require user-installed Python 3.12+ (registry/py launcher/PATH)."
}

# ---- Step 4: Create portable marker ----
Write-Host "`n[4/6] Creating portable marker file..."
$portableFile = Join-Path $DistDir "portable"
"" | Out-File -FilePath $portableFile -Encoding ascii -NoNewline
Write-Host "        portable marker created"

# ---- Step 5: Prune unnecessary files ----
if (-not $NoPrune) {
    Write-Host "`n[5/6] Pruning unnecessary files..."

    $pruned = 0

    # Translation files (~7 MB)
    $translationsDir = Join-Path $DistDir "translations"
    if (Test-Path $translationsDir) {
        Remove-Item $translationsDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "        - translations/"
        $pruned++
    }

    # QML debugging tools
    $qmlToolingDir = Join-Path $DistDir "qmltooling"
    if (Test-Path $qmlToolingDir) {
        Remove-Item $qmlToolingDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "        - qmltooling/"
        $pruned++
    }

    # Unused QML Control styles (app only uses Basic)
    $unusedStyles = @(
        "Qt6QuickControls2Fusion.dll", "Qt6QuickControls2Fusion.qml",
        "Qt6QuickControls2Imagine.dll", "Qt6QuickControls2Imagine.qml",
        "Qt6QuickControls2Material.dll", "Qt6QuickControls2Material.qml",
        "Qt6QuickControls2Universal.dll", "Qt6QuickControls2Universal.qml",
        "Qt6QuickControls2FluentWinUI3StyleImpl.dll",
        "Qt6QuickControls2WindowsStyleImpl.dll"
    )
    foreach ($style in $unusedStyles) {
        $stylePath = Join-Path $DistDir $style
        if (Test-Path $stylePath) {
            Remove-Item $stylePath -Force -ErrorAction SilentlyContinue
            Write-Host "        - $style"
            $pruned++
        }
    }

    # Prune unused style subdirectories under qml/QtQuick/Controls/
    $qmlControlsDir = Join-Path $DistDir "qml\QtQuick\Controls"
    if (Test-Path $qmlControlsDir) {
        # impl 不是样式，是 Basic 自身依赖的共享实现模块（ScrollView/ToolTip 等
        # 都 import QtQuick.Controls.impl），删了 Basic 控件会集体加载失败
        $keepStyles = @("Basic", "impl")
        Get-ChildItem $qmlControlsDir -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -notin $keepStyles } |
            ForEach-Object {
                Remove-Item $_.FullName -Recurse -Force -ErrorAction SilentlyContinue
                Write-Host "        - qml/QtQuick/Controls/$($_.Name)/"
                $pruned++
            }
    }

    # PDB files (debug symbols)
    Get-ChildItem $DistDir -Filter "*.pdb" -Recurse -ErrorAction SilentlyContinue |
        ForEach-Object {
            Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
        }
    if ((Get-ChildItem $DistDir -Filter "*.pdb" -Recurse -ErrorAction SilentlyContinue).Count -eq 0) {
        Write-Host "        - *.pdb (debug symbols)"
        $pruned++
    }

    # Runtime log file
    $logFile = Join-Path $DistDir "xuanhub.log"
    if (Test-Path $logFile) {
        Remove-Item $logFile -Force -ErrorAction SilentlyContinue
        Write-Host "        - xuanhub.log"
        $pruned++
    }

    Write-Host "        Pruned $pruned item(s)"
} else {
    Write-Host "`n[5/6] Pruning: --no-prune, skipped"
}

# ---- Step 6: Create zip archive ----
Write-Host "`n[6/6] Packaging..."

$distSize = (Get-ChildItem $DistDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ("        Dist dir size: {0:N1} MB" -f ($distSize / 1MB))

if (-not $NoZip) {
    New-Item -ItemType Directory -Path $OutputDir -Force -ErrorAction SilentlyContinue | Out-Null

    $zipName = "XuanHub-v$Version-portable-x64.zip"
    $zipPath = Join-Path $OutputDir $zipName

    # Remove old zip if exists
    if (Test-Path $zipPath) {
        Remove-Item $zipPath -Force
    }

    Write-Host "        Creating $zipName ..."
    Compress-Archive -Path "$DistDir\*" -DestinationPath $zipPath -CompressionLevel Optimal -Force

    if (Test-Path $zipPath) {
        $zipSize = (Get-Item $zipPath).Length
        Write-Host ("        Done: {0} ({1:N1} MB)" -f $zipPath, ($zipSize / 1MB))
    } else {
        Write-Error "Packaging failed"
        exit 1
    }
} else {
    Write-Host "        --no-zip: skipping archive, portable dir ready at: $DistDir"
}

Write-Host "`n=============================================="
Write-Host "  Packaging complete!"
if (-not $NoZip) {
    Write-Host "  Output: $zipPath"
} else {
    Write-Host "  Portable dir: $DistDir"
}
Write-Host "==============================================`n"
