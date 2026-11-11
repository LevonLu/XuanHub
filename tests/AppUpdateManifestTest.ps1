# AppUpdateManifestTest.ps1 — 验证打包清单的实际内容与受保护文件排除
param([Parameter(Mandatory = $true)][string]$ManifestScript)
$ErrorActionPreference = 'Stop'
. $ManifestScript
$manifestTestRoot = Join-Path ([IO.Path]::GetTempPath()) ('XuanHubManifest-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $manifestTestRoot | Out-Null
try {
    foreach ($name in @('XuanHub.exe', 'XuanHubUpdater.exe', 'Qt6Core.dll', 'barplugin.dll', 'config.json', 'portable')) {
        [IO.File]::WriteAllText((Join-Path $manifestTestRoot $name), $name)
    }
    Write-AppUpdateManifest -Directory $manifestTestRoot -Version '0.4.5'
    $generated = Get-Content (Join-Path $manifestTestRoot 'update-manifest.json') -Raw | ConvertFrom-Json
    if ($generated.version -ne '0.4.5' -or $generated.schemaVersion -ne 1 -or $generated.files.Count -ne 4) {
        throw '生成的发行清单内容不符'
    }
    foreach ($entry in $generated.files) {
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try {
            $bytes = [IO.File]::ReadAllBytes((Join-Path $manifestTestRoot $entry.path))
            $actual = ([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
        } finally { $algorithm.Dispose() }
        if ($entry.sha256 -cne $actual -or $entry.size -ne (Get-Item -LiteralPath (Join-Path $manifestTestRoot $entry.path)).Length) {
            throw '发行清单大小或哈希不符'
        }
    }
    Write-Host 'PASS: 发行文件集合、版本、大小与 SHA-256'
} finally {
    $resolved = [IO.Path]::GetFullPath($manifestTestRoot)
    $temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($temporary, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike 'XuanHubManifest-*') { throw '拒绝越界清理' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
