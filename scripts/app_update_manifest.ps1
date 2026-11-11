# app_update_manifest.ps1 — 为干净发行目录生成可复核的官方文件清单
function Write-AppUpdateManifest {
    param(
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)][string]$Version
    )
    $updateRoot = [IO.Path]::GetFullPath($Directory).TrimEnd('\', '/')
    $updateFiles = @()
    foreach ($entry in (Get-ChildItem -LiteralPath $updateRoot -Recurse -File | Sort-Object FullName)) {
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "发行目录包含链接：$($entry.Name)"
        }
        $relative = $entry.FullName.Substring($updateRoot.Length + 1).Replace('\', '/')
        if ($relative -in @('config.json', 'portable', 'update-manifest.json')) { continue }
        $stream = [IO.File]::OpenRead($entry.FullName)
        try {
            $algorithm = [Security.Cryptography.SHA256]::Create()
            try {
                $digest = ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
            } finally { $algorithm.Dispose() }
        } finally { $stream.Dispose() }
        $updateFiles += [ordered]@{ path = $relative; size = $entry.Length; sha256 = $digest }
    }
    foreach ($required in @('XuanHub.exe', 'XuanHubUpdater.exe', 'Qt6Core.dll', 'barplugin.dll')) {
        if (-not ($updateFiles | Where-Object { $_.path -ceq $required })) {
            throw "发行目录缺少更新必需文件：$required"
        }
    }
    $manifest = [ordered]@{ schemaVersion = 1; version = $Version; files = @($updateFiles) }
    [IO.File]::WriteAllText((Join-Path $updateRoot 'update-manifest.json'),
        ($manifest | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
}
