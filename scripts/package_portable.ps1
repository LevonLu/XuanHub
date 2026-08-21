<#
.SYNOPSIS
    Package XuanHub as portable distribution.

.DESCRIPTION
    Copies build artifacts into a clean staging directory, collects dependencies
    (MSVC runtime DLLs, optional Python stdlib), rejects runtime/private files,
    creates the portable marker, prunes unnecessary files, and archives to zip.

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

.PARAMETER BundlePipPackages
    Comma-separated package name prefixes to inject from the packaging machine's
    site-packages into the bundle's Lib/site-packages (Bundle strategy only).
    Default empty. site-packages is excluded wholesale EXCEPT these explicitly
    named packages — for plugins that need third-party deps out of the box
    (e.g. heartrate 插件依赖 bleak + winrt 后端).
    Prefix matching copies the top-level entry (dir/file) and its *.dist-info,
    so a namespace package like winrt (winrt/ + winrt_*.dist-info) is covered
    by the single prefix "winrt".

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
    [ValidateSet("Bundle", "None")]
    [string]$PythonStrategy = "None",

    [Parameter(Mandatory = $false)]
    [string]$BundlePipPackages = "",

    [Parameter(Mandatory = $false)]
    [switch]$NoPrune
)

$ErrorActionPreference = "Stop"

function Test-StrictChildPath {
    param(
        [Parameter(Mandatory = $true)][string]$Parent,
        [Parameter(Mandatory = $true)][string]$Child
    )

    $parentFull = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $childFull = [IO.Path]::GetFullPath($Child).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    return $childFull.StartsWith($parentFull, [StringComparison]::OrdinalIgnoreCase) -and
           -not $childFull.Equals($parentFull, [StringComparison]::OrdinalIgnoreCase)
}

# 最终发布闸门：运行时文件一律拒绝，config.json 必须与仓库内置默认配置一致；
# 文本文件再检查高置信凭据形状和本机用户目录，避免“打包机状态”混入成品。
function Assert-PackageSafe {
    param(
        [Parameter(Mandatory = $true)][string]$PackageDir,
        [Parameter(Mandatory = $true)][string]$DefaultConfig,
        [Parameter(Mandatory = $true)][string]$LicenseFile
    )

    $forbiddenNames = @(
        "secrets.json", "XuanHub.lock", "xuanhub.log", ".env", ".npmrc", ".pypirc"
    )
    $forbiddenExtensions = @(
        ".pyc", ".pyo", ".pem", ".key", ".p12", ".pfx", ".keystore", ".jks",
        ".log", ".db", ".sqlite", ".sqlite3", ".dmp", ".dump", ".trace"
    )
    $violations = New-Object System.Collections.Generic.List[string]

    Get-ChildItem $PackageDir -Recurse -Directory -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -in @("__pycache__", ".git", ".hg", ".svn") } |
        ForEach-Object { $violations.Add("禁止目录: $($_.FullName)") }

    $packageFiles = @(Get-ChildItem $PackageDir -Recurse -File -Force)
    foreach ($file in $packageFiles) {
        if ($file.Name -in $forbiddenNames -or
            $file.Name -like ".env.*" -or
            $file.Extension.ToLowerInvariant() -in $forbiddenExtensions) {
            $violations.Add("禁止文件: $($file.FullName)")
        }
    }

    $packageConfig = Join-Path $PackageDir "config.json"
    if (-not (Test-Path $packageConfig -PathType Leaf)) {
        $violations.Add("缺少干净默认配置: $packageConfig")
    } elseif ((Get-FileHash $packageConfig -Algorithm SHA256).Hash -ne
              (Get-FileHash $DefaultConfig -Algorithm SHA256).Hash) {
        $violations.Add("config.json 不是仓库内置默认配置")
    }

    $packageLicense = Join-Path $PackageDir "LICENSE"
    if (-not (Test-Path $packageLicense -PathType Leaf)) {
        $violations.Add("缺少 BSD-3-Clause 许可证: $packageLicense")
    } elseif ((Get-FileHash $packageLicense -Algorithm SHA256).Hash -ne
              (Get-FileHash $LicenseFile -Algorithm SHA256).Hash) {
        $violations.Add("LICENSE 与仓库许可证不一致")
    }

    $textExtensions = @(
        ".json", ".qml", ".py", ".md", ".txt", ".ini", ".conf", ".yaml", ".yml",
        ".toml", ".ps1", ".cmake", ".xml", ".js", ".ts", ".css", ".html"
    )
    $secretPatterns = @(
        @{ Name = "私钥头"; Pattern = '-----BEGIN(?: [A-Z0-9]+)? PRIVATE KEY-----' },
        @{ Name = "AWS Access Key"; Pattern = '(?:AKIA|ASIA)[A-Z0-9]{16}' },
        @{ Name = "GitHub Token"; Pattern = '(?:gh[pousr]_[A-Za-z0-9]{36,255}|github_pat_[A-Za-z0-9_]{20,255})' },
        @{ Name = "OpenAI/Anthropic Key"; Pattern = '(?:sk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{20,}|sk-ant-[A-Za-z0-9_-]{20,})' },
        @{ Name = "Google/Slack Token"; Pattern = '(?:AIza[0-9A-Za-z_-]{35}|xox[baprs]-[0-9A-Za-z-]{10,})' },
        @{ Name = "JWT"; Pattern = 'eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}' },
        @{ Name = "带凭据 URL"; Pattern = 'https?://[^/\s:@]+:[^@\s/]+@' },
        @{ Name = "本机用户目录"; Pattern = '(?i)[A-Z]:[/\\]Users[/\\][^/\\\s]+' }
    )
    foreach ($file in $packageFiles) {
        if ($file.Length -gt 5MB -or $file.Extension.ToLowerInvariant() -notin $textExtensions) {
            continue
        }
        $content = [IO.File]::ReadAllText($file.FullName)
        foreach ($rule in $secretPatterns) {
            if ($content -match $rule.Pattern) {
                $violations.Add("$($rule.Name): $($file.FullName)")
            }
        }
    }

    if ($violations.Count -gt 0) {
        $detail = $violations | Sort-Object -Unique | ForEach-Object { "  - $_" }
        throw "发布安全检查失败：`n$($detail -join "`n")"
    }
    Write-Host "        Security gate: PASS"
}

# 压缩完成后检查 ZIP 自身，关闭“安全检查后、压缩过程中又生成运行时文件”的竞态。
function Assert-ZipManifestSafe {
    param(
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$DefaultConfig,
        [Parameter(Mandatory = $true)][string]$LicenseFile
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $forbiddenNames = @(
        "secrets.json", "XuanHub.lock", "xuanhub.log", ".env", ".npmrc", ".pypirc"
    )
    $forbiddenExtensions = @(
        ".pyc", ".pyo", ".pem", ".key", ".p12", ".pfx", ".keystore", ".jks",
        ".log", ".db", ".sqlite", ".sqlite3", ".dmp", ".dump", ".trace"
    )
    $violations = New-Object System.Collections.Generic.List[string]
    $configHashes = New-Object System.Collections.Generic.List[string]
    $licenseHashes = New-Object System.Collections.Generic.List[string]
    $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        foreach ($entry in $archive.Entries) {
            $parts = $entry.FullName -split '[/\\]'
            if ($parts -contains ".git" -or $parts -contains ".hg" -or
                $parts -contains ".svn" -or $parts -contains "__pycache__") {
                $violations.Add("禁止目录: $($entry.FullName)")
            }
            $extension = [IO.Path]::GetExtension($entry.Name).ToLowerInvariant()
            if ($entry.Name -in $forbiddenNames -or
                $entry.Name -like ".env.*" -or
                $extension -in $forbiddenExtensions) {
                $violations.Add("禁止文件: $($entry.FullName)")
            }
            if ($entry.FullName -eq "config.json") {
                $stream = $entry.Open()
                try {
                    $sha = [Security.Cryptography.SHA256]::Create()
                    try {
                        $configHashes.Add(([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', ''))
                    } finally {
                        $sha.Dispose()
                    }
                } finally {
                    $stream.Dispose()
                }
            }
            if ($entry.FullName -eq "LICENSE") {
                $stream = $entry.Open()
                try {
                    $sha = [Security.Cryptography.SHA256]::Create()
                    try {
                        $licenseHashes.Add(([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', ''))
                    } finally {
                        $sha.Dispose()
                    }
                } finally {
                    $stream.Dispose()
                }
            }
        }
    } finally {
        $archive.Dispose()
    }

    $defaultHash = (Get-FileHash $DefaultConfig -Algorithm SHA256).Hash
    if ($configHashes.Count -ne 1 -or $configHashes[0] -ne $defaultHash) {
        $violations.Add("ZIP 中缺少唯一且干净的默认 config.json")
    }
    $licenseHash = (Get-FileHash $LicenseFile -Algorithm SHA256).Hash
    if ($licenseHashes.Count -ne 1 -or $licenseHashes[0] -ne $licenseHash) {
        $violations.Add("ZIP 中缺少唯一且正确的 LICENSE")
    }
    if ($violations.Count -gt 0) {
        $detail = $violations | Sort-Object -Unique | ForEach-Object { "  - $_" }
        throw "ZIP 发布安全检查失败：`n$($detail -join "`n")"
    }
    Write-Host "        Archive gate: PASS"
}

# 定位本机 Python ≥3.12 安装目录（隔离 worker 运行时下限）：
# 注册表枚举全部 PythonCore 版本（取最高）→ py launcher → PATH；
# 排除 WindowsApps 商店占位 stub
function Resolve-PythonHome {
    $candidates = @()
    foreach ($root in @('HKCU:\Software\Python\PythonCore', 'HKLM:\Software\Python\PythonCore')) {
        Get-ChildItem $root -ErrorAction SilentlyContinue | ForEach-Object {
            $ver = $_.PSChildName   # 形如 "3.13"（32 位安装带 "-32" 后缀）
            if ($ver -match '^3\.(\d+)') {
                # 注意：不能用 $home（PowerShell 只读自动变量，赋值即抛异常）
                $pyHome = (Get-ItemProperty "$root\$ver\InstallPath" -ErrorAction SilentlyContinue).'(default)'
                if ($pyHome -and (Test-Path (Join-Path $pyHome 'python.exe'))) {
                    $candidates += [pscustomobject]@{ Minor = [int]$Matches[1]; Home = $pyHome.TrimEnd('\', '/') }
                }
            }
        }
    }
    $best = $candidates | Where-Object { $_.Minor -ge 12 } |
            Sort-Object Minor -Descending | Select-Object -First 1
    if ($best) { return $best.Home }
    $pyLauncher = @(Get-Command py -ErrorAction SilentlyContinue)[0]
    if ($pyLauncher) {
        # PowerShell 5.1 会把 py launcher 的 stderr 包装成 NativeCommandError；
        # 启动器存在但未注册解释器时必须吞掉错误，继续尝试 PATH。
        try {
            $exe = & $pyLauncher.Source -3 -c "import sys; print(sys.executable)" 2>$null
            $launcherCode = $LASTEXITCODE
            $minor = if ($launcherCode -eq 0) {
                & $pyLauncher.Source -3 -c "import sys; print(sys.version_info[1])" 2>$null
            } else { $null }
        } catch {
            $exe = $null
            $minor = $null
        }
        if ($exe -and $minor -and [int]$minor -ge 12 -and (Test-Path "$exe")) {
            return (Split-Path "$exe" -Parent)
        }
    }
    $cmd = @(Get-Command python -ErrorAction SilentlyContinue |
             Where-Object { $_.Source -notlike '*WindowsApps*' })[0]
    if ($cmd) {
        try {
            $exe = & $cmd.Source -c "import sys; print(sys.executable)" 2>$null
            $pythonCode = $LASTEXITCODE
            $minor = if ($pythonCode -eq 0) {
                & $cmd.Source -c "import sys; print(sys.version_info[1])" 2>$null
            } else { $null }
        } catch {
            $exe = $null
            $minor = $null
        }
        if ($exe -and $minor -and [int]$minor -ge 12 -and (Test-Path "$exe")) {
            return (Split-Path "$exe" -Parent)
        }
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

$SourceDistDir = Join-Path $ProjectRoot "build\default\dist\$Config"

# Read version from CMakeLists.txt
$CmakeFile = Join-Path $ProjectRoot "CMakeLists.txt"
$versionMatch = Select-String -Path $CmakeFile -Pattern 'VERSION\s+(\d+\.\d+\.\d+)'
if ($versionMatch) {
    $Version = $versionMatch.Matches[0].Groups[1].Value
} else {
    $Version = "0.1.0"
}

$packageName = "XuanHub-v$Version-portable-x64"
$DistDir = Join-Path $OutputDir $packageName
$zipPath = Join-Path $OutputDir "$packageName.zip"
$defaultConfig = Join-Path $ProjectRoot "app\resources\default_config.json"
$licenseFile = Join-Path $ProjectRoot "LICENSE"

Write-Host "=============================================="
Write-Host "  XuanHub Portable Packaging Tool  v$Version"
Write-Host "=============================================="
Write-Host ""
Write-Host "[info] Config:    $Config"
Write-Host "[info] Version:   $Version"
Write-Host "[info] Build dir: $SourceDistDir"
Write-Host "[info] Stage dir: $DistDir"

# ---- Step 1: Verify and stage build artifacts ----
Write-Host "`n[1/6] Creating clean staging directory..."
$sourceExe = Join-Path $SourceDistDir "XuanHub.exe"
if (-not (Test-Path $sourceExe)) {
    Write-Error "XuanHub.exe not found. Build first: cmake --build --preset default"
    exit 1
}
if (-not (Test-Path $defaultConfig -PathType Leaf)) {
    Write-Error "Default config not found: $defaultConfig"
    exit 1
}
if (-not (Test-Path $licenseFile -PathType Leaf)) {
    Write-Error "License not found: $licenseFile"
    exit 1
}
if (-not (Test-StrictChildPath -Parent $OutputDir -Child $DistDir)) {
    Write-Error "Unsafe staging path: $DistDir"
    exit 1
}
New-Item -ItemType Directory -Path $OutputDir -Force -ErrorAction SilentlyContinue | Out-Null
if (-not $NoZip -and (Test-Path $zipPath)) {
    # 先删除旧包；若本次任何安全检查失败，不留下可能被误上传的陈旧 ZIP。
    Remove-Item -LiteralPath $zipPath -Force
}
if (Test-Path $DistDir) {
    Remove-Item -LiteralPath $DistDir -Recurse -Force
}
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

# 从构建输出复制程序文件，但在进入 staging 时就排除所有运行时/私有状态；
# 后续仍有 Assert-PackageSafe 二次 fail-closed 检查，避免新增文件类型时静默泄露。
$stageCopyArgs = @(
    $SourceDistDir,
    $DistDir,
    "/E",
    "/COPY:DAT",
    "/DCOPY:DAT",
    "/R:1", "/W:1",
    "/XD", "__pycache__", ".git", ".hg", ".svn",
    "/XF", "config.json", "secrets.json", "XuanHub.lock", "xuanhub.log", "portable",
            "*.pyc", "*.pyo", "*.pdb", "*.log", "*.db", "*.sqlite", "*.sqlite3",
            "*.dmp", "*.dump", "*.trace", "*.pem", "*.key", "*.p12", "*.pfx",
    "/NJH", "/NJS", "/NDL", "/NFL"
)
& robocopy @stageCopyArgs | Out-Null
$stageCopyCode = $LASTEXITCODE
if ($stageCopyCode -ge 8) {
    Write-Error "robocopy exit code $stageCopyCode, staging copy failed"
    exit 1
}
Copy-Item -LiteralPath $defaultConfig -Destination (Join-Path $DistDir "config.json") -Force
Copy-Item -LiteralPath $licenseFile -Destination (Join-Path $DistDir "LICENSE") -Force

$exe = Join-Path $DistDir "XuanHub.exe"
if (-not (Test-Path $exe)) {
    Write-Error "XuanHub.exe missing from staging directory"
    exit 1
}
Write-Host "        XuanHub.exe + clean default config + LICENSE: OK"

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
    # 隔离 worker 所需解释器与 DLL 只在 Bundle 策略下随包分发，其余策略清掉旧产物
    $oldPythonExe = Join-Path $DistDir "python.exe"
    if (Test-Path $oldPythonExe) {
        Remove-Item $oldPythonExe -Force -ErrorAction SilentlyContinue
        Write-Host "        Removed stale python.exe"
    }
    Get-ChildItem $DistDir -Filter 'python3*.dll' -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
            Write-Host "        Removed stale $($_.Name)"
        }
}
# 清掉旧版本 Embedded 策略（已移除）可能留下的产物
foreach ($f in @("python313.zip", "python313._pth")) {
    $p = Join-Path $DistDir $f
    if (Test-Path $p) {
        Remove-Item $p -Force -ErrorAction SilentlyContinue
        Write-Host "        Removed stale $f"
    }
}
# 内嵌解释器架构已移除；手动对旧 dist 打包时也不能把后端 DLL 带进成品。
$oldBackend = Join-Path $DistDir "pybackend.dll"
if (Test-Path $oldBackend) {
    Remove-Item $oldBackend -Force -ErrorAction SilentlyContinue
    Write-Host "        Removed stale pybackend.dll"
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

        # 独立 worker 需要 python.exe；连同 python3.dll（转发器）+ python3XX.dll
        # 与标准库一起搬运，Bundle 包无需用户安装 Python。
        $pythonExe = Join-Path $pythonHome "python.exe"
        if (Test-Path $pythonExe) {
            Copy-Item $pythonExe (Join-Path $DistDir "python.exe") -Force
            Write-Host "        + python.exe"
        } else {
            Write-Warning "python.exe not found: $pythonExe (isolated Python plugins will be disabled)"
        }
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
                "/XD", "site-packages", "test", "idlelib", "turtledemo", "distutils", "ensurepip", "venv", "__pycache__",
                "/XF", "*.pyc",
                "/NJH", "/NJS",
                "/NDL", "/NFL", "/NP"
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
                "/NDL", "/NFL", "/NP"
            )
            $rc = (Start-Process robocopy -ArgumentList $robocopyArgs -Wait -PassThru -NoNewWindow).ExitCode
            if ($rc -ge 8) {
                Write-Warning "robocopy exit code $rc, DLLs/ copy may be incomplete"
            } else {
                $dllsSize = (Get-ChildItem $dstDlls -Recurse -File | Measure-Object -Property Length -Sum).Sum
                Write-Host ("        DLLs/ done, size: {0:N1} MB" -f ($dllsSize / 1MB))
            }
        }

        # BundlePipPackages：显式点名的第三方包注入包内 Lib/site-packages
        #（site-packages 整体排除的唯一例外，供依赖第三方库的插件开箱即用）。
        # 按前缀匹配顶层条目（目录/文件 + *.dist-info 一并拷贝）：
        # winrt 是共享命名空间包（winrt/ + winrt_*.dist-info），单个前缀 "winrt" 全覆盖。
        if ($BundlePipPackages) {
            $sp = Join-Path $pythonHome "Lib\site-packages"
            $dstSp = Join-Path $DistDir "Lib\site-packages"
            if (-not (Test-Path $sp)) {
                Write-Warning "site-packages not found: $sp (BundlePipPackages ignored)"
            } else {
                New-Item -ItemType Directory -Path $dstSp -Force -ErrorAction SilentlyContinue | Out-Null
                foreach ($pkg in ($BundlePipPackages -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
                    $matched = @(Get-ChildItem $sp -Filter "$pkg*" -ErrorAction SilentlyContinue)
                    if ($matched.Count -eq 0) {
                        Write-Warning "BundlePipPackages: '$pkg' not found in site-packages"
                        continue
                    }
                    foreach ($item in $matched) {
                        Copy-Item $item.FullName (Join-Path $dstSp $item.Name) -Recurse -Force
                    }
                    $size = ($matched | ForEach-Object {
                        if ($_.PSIsContainer) {
                            (Get-ChildItem $_.FullName -Recurse -File | Measure-Object -Property Length -Sum).Sum
                        } else { $_.Length }
                    } | Measure-Object -Sum).Sum
                    Write-Host ("        + site-packages/{0}* ({1} 项, {2:N1} MB)" -f $pkg, $matched.Count, ($size / 1MB))
                }
            }
        }
    }
}

# site-packages 由打包机复制时可能携带现成 pyc；它们会嵌入打包机的绝对路径。
# 发布包统一只保留源码/扩展模块，让目标机按需生成自己的缓存。
$bytecodeFiles = @(
    Get-ChildItem $DistDir -Recurse -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @(".pyc", ".pyo") }
)
foreach ($file in $bytecodeFiles) {
    Remove-Item -LiteralPath $file.FullName -Force
}
$cacheDirs = @(
    Get-ChildItem $DistDir -Recurse -Directory -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq "__pycache__" } |
        Sort-Object { $_.FullName.Length } -Descending
)
foreach ($dir in $cacheDirs) {
    Remove-Item -LiteralPath $dir.FullName -Recurse -Force
}
if ($bytecodeFiles.Count -gt 0 -or $cacheDirs.Count -gt 0) {
    Write-Host "        Removed $($bytecodeFiles.Count) bytecode file(s) and $($cacheDirs.Count) cache dir(s)"
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

Assert-PackageSafe -PackageDir $DistDir -DefaultConfig $defaultConfig -LicenseFile $licenseFile

$distSize = (Get-ChildItem $DistDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ("        Dist dir size: {0:N1} MB" -f ($distSize / 1MB))

if (-not $NoZip) {
    New-Item -ItemType Directory -Path $OutputDir -Force -ErrorAction SilentlyContinue | Out-Null

    $zipName = "$packageName.zip"

    Write-Host "        Creating $zipName ..."
    Compress-Archive -Path "$DistDir\*" -DestinationPath $zipPath -CompressionLevel Optimal -Force

    if (Test-Path $zipPath) {
        try {
            Assert-ZipManifestSafe -ZipPath $zipPath -DefaultConfig $defaultConfig -LicenseFile $licenseFile
        } catch {
            Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
            throw
        }
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
