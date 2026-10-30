# PortablePackageSafetyTest.ps1 — 验证公开格式常量不会放宽实际便携包凭据检查
param([Parameter(Mandatory = $true)][string]$PackageScript)
$ErrorActionPreference = 'Stop'

# 只加载被测函数，不执行构建目录复制、清理或压缩等打包主流程。
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    $PackageScript, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw '打包脚本解析失败' }
foreach ($name in @('Get-FileSha256', 'Get-PackageCredentialScanText', 'Assert-PackageSafe')) {
    $function = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq $name
    }, $true)
    if (-not $function) { throw "缺少被测函数: $name" }
    . ([scriptblock]::Create($function.Extent.Text))
}

$knownPath = 'Lib/site-packages/cryptography/hazmat/primitives/serialization/ssh.py'
$header = '-----BEGIN OPENSSH PRIVATE KEY-----'
$constant = '_SK_START = b"' + $header + '"'
$privateBlock = $header + "`n" + ('A' * 64) + "`n-----END OPENSSH PRIVATE KEY-----"
$cases = @(
    @{ Name = '公开常量'; Path = $knownPath; Content = $constant; Reject = $false },
    @{ Name = 'CRLF 与其他代码'; Path = $knownPath; Content = $constant + "`r`nvalue = 1`r`n"; Reject = $false },
    @{ Name = '其他路径'; Path = 'other.py'; Content = $constant; Reject = $true },
    @{ Name = '真实私钥'; Path = $knownPath; Content = $privateBlock; Reject = $true },
    @{ Name = '同文件追加私钥'; Path = $knownPath; Content = $constant + "`n" + $privateBlock; Reject = $true },
    @{ Name = '常量行额外内容'; Path = $knownPath; Content = $constant + ' + extra'; Reject = $true },
    @{ Name = '同文件其他令牌'; Path = $knownPath; Content = $constant + "`n" + 'ghp_' + ('a' * 36); Reject = $true }
)
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') +
            [IO.Path]::DirectorySeparatorChar
$testRoot = Join-Path $tempBase ('XuanHubPortableGate-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $defaultConfig = Join-Path $testRoot 'default.json'
    $licenseFile = Join-Path $testRoot 'license-source.txt'
    [IO.File]::WriteAllText($defaultConfig, '{}')
    [IO.File]::WriteAllText($licenseFile, 'test-license')
    $index = 0
    foreach ($case in $cases) {
        $package = Join-Path $testRoot ('package-' + $index++)
        New-Item -ItemType Directory -Path $package | Out-Null
        Copy-Item -LiteralPath $defaultConfig -Destination (Join-Path $package 'config.json')
        Copy-Item -LiteralPath $licenseFile -Destination (Join-Path $package 'LICENSE')
        $source = Join-Path $package $case.Path
        New-Item -ItemType Directory -Path (Split-Path $source -Parent) -Force | Out-Null
        [IO.File]::WriteAllText($source, $case.Content)
        $rejected = $false
        try {
            Assert-PackageSafe -PackageDir $package -DefaultConfig $defaultConfig -LicenseFile $licenseFile
        } catch {
            if ($_.Exception.Message -notlike '*发布安全检查失败*') { throw }
            $rejected = $true
        }
        if ($rejected -ne $case.Reject) { throw "安全边界不符: $($case.Name)" }
        Write-Host "PASS: $($case.Name)"
    }
} finally {
    # 只删除本测试刚创建、位于临时目录内且前缀固定的唯一目录。
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolved.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike 'XuanHubPortableGate-*') {
        throw '拒绝清理越界测试目录'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
