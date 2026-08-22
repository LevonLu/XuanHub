# `.xuanplugin` 插件包格式（Schema 1）

`.xuanplugin` 是 XuanHub 的离线插件分发格式。容器是单磁盘、非 ZIP64 的 ZIP，但安装方不能把它当普通压缩包直接展开到插件目录；必须先通过宿主统一校验器，并且只展开到临时目录。

## 目录结构

```text
plugin.json             包描述，必需
manifest.sha256         文件哈希清单，必需
signature.json          发布者签名，可选
LICENSE                 可选
README.md               可选
payload/                插件实际运行目录，必需
  metadata.json         native 插件 Manifest
  plugin.json           Python 插件 Manifest
  ...                   DLL、Python、QML、i18n、assets、离线 wheels 等
```

`payload/` 保留现有插件运行目录的原始结构。因此 Python 插件解包后仍是 `plugin.json + main.py + QML`，原生插件仍是 `metadata.json + DLL + QML`，安装器不重写插件内容。

## `plugin.json`

包描述使用 UTF-8 JSON：

```json
{
  "schemaVersion": 1,
  "id": "com.example.demo",
  "version": "1.2.0",
  "runtime": "native",
  "manifest": "payload/metadata.json",
  "publisher": "Example Publisher"
}
```

- `schemaVersion` 当前只能是整数 `1`。
- `id` 和 `version` 必须与 payload Manifest 完全一致；版本使用 SemVer。
- `runtime` 只能是 `native` 或 `python`。
- `manifest` 不允许自定义：native 固定为 `payload/metadata.json`，Python 固定为 `payload/plugin.json`。
- `publisher` 可选。它是待签名的显示信息；没有签名时不代表可信身份。
- 签名包还必须在 `plugin.json` 声明签名算法和公钥标识：

  ```json
  {
    "signature": {
      "algorithm": "ecdsa-p256-sha256",
      "keyId": "sha256:<64 个小写十六进制字符>"
    }
  }
  ```

  声明本身受 `manifest.sha256` 覆盖。`plugin.json` 有声明但缺少 `signature.json`，或只有
  `signature.json` 而没有声明，均视为损坏包并拒绝，不能静默降级为未签名包。

native payload 继续使用 `app/src/PluginManifest.cpp` 的 V1/V2 规则，包括宿主版本、Qt、编译器、CRT、入口文件与路径边界。Python payload 使用现有 pybridge 身份、版本、入口和权限规则，并复用同一宿主版本范围检查。

## `manifest.sha256`

清单必须为无 BOM 的 UTF-8、LF 换行，并以 LF 结尾。每行格式为：

```text
<64 个小写十六进制字符><两个 ASCII 空格><包内路径>
```

路径按 Unicode 码点升序排列且不得重复。清单覆盖除 `manifest.sha256` 和可选 `signature.json` 外的每个普通文件，包括根 `plugin.json`、LICENSE、README 和 payload 全部文件。包内有未列文件、清单有多余文件或任一 SHA-256 不一致时，整包拒绝。

`signature.json` 不进入哈希清单以避免签名自引用。它使用以下 Schema 1：

```json
{
  "schemaVersion": 1,
  "algorithm": "ecdsa-p256-sha256",
  "keyId": "sha256:<64 个小写十六进制字符>",
  "publicKey": "<Base64 编码的 65 字节 SEC1 未压缩 P-256 公钥>",
  "signed": "manifest.sha256",
  "signature": "<Base64 编码的 64 字节 r || s>"
}
```

签名输入是 `manifest.sha256` 的原始字节，先计算 SHA-256，再用 ECDSA P-256 验证。
`keyId` 是 65 字节公钥（`04 || X || Y`）的 SHA-256，格式固定为 `sha256:<hex>`。
Base64 必须是带标准 padding 的规范编码。算法、keyId、目标文件或签名字节任一不匹配，
整包必须拒绝；不得按未签名包继续安装。

签名证明包内容由对应私钥持有者签发，但内嵌公钥本身不等于受信任身份。首版设置页同时
展示发布者文本、keyId 和包来源，由用户决定是否信任；后续更新源必须额外固定预期 keyId，
防止索引或包被替换后使用另一把密钥重新签名。完全未声明签名且没有 `signature.json` 的本地
包仍可安装，但界面必须明确显示“未签名”警告。

## 安全边界

Schema 1 的强制限制：

- 扩展名必须是 `.xuanplugin`，包最大 256 MiB。
- 最多 2048 个 ZIP 条目；单文件展开后最大 64 MiB；总展开大小最大 512 MiB。
- 仅允许 Stored 或 Deflate；单文件展开/压缩比最大 200。
- 禁止多磁盘、ZIP64、加密条目、符号链接、绝对路径、反斜杠、`..`、ADS 冒号、控制字符、非规范路径及 Windows 设备名。
- 路径按 Windows 不区分大小写的语义去重；本地文件头必须与中央目录一致。
- 顶层只允许三个控制文件、LICENSE、README 和 `payload/`。
- 哈希、包描述、运行时 Manifest、兼容性与权限预览全部通过后，才允许写入自动清理的临时目录。

当前读取实现隔离在 `app/src/PluginPackageValidator.cpp`，使用项目锁定的 Qt 6.9.3 `QZipReader` 私有接口；中央目录和安全策略由 XuanHub 自己校验，不调用 `extractAll()`。

## 参考打包器

```powershell
python scripts/package_plugin.py plugins_py/hello `
  --runtime python `
  --publisher XuanHub `
  --output dist/py_hello-0.1.0.xuanplugin
```

原生插件把源目录换成构建后的单插件运行目录，并传 `--runtime native`。加 `--json` 可得到机器可读结果。

参考打包器使用固定 ZIP 时间、固定权限、排序路径和 Stored 存储，输入相同则产物字节完全相同。它会 fail closed：链接或 junction、VCS 元数据、Python 缓存、日志/锁、常见凭据文件、私钥和高置信访问令牌都会阻止打包，而不是被静默忽略。
