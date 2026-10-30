# Agent Note: 便携包凭据扫描区分公开格式常量

Status: implemented

## Problem

用量插件需要随便携包分发 `cryptography`。其 `serialization/ssh.py` 包含公开的
`_SK_START` SSH 私钥格式标记常量，现有私钥头扫描将该常量误判为凭据，阻止生成发布包。

## Decision

仅对 `Lib/site-packages/cryptography/hazmat/primitives/serialization/ssh.py`
中完全匹配 `_SK_START` 字节串赋值的独立行做扫描文本归一化。
文件其余内容继续经过所有既有凭据规则；其他路径、行尾额外内容与真实私钥头仍被拒绝。
上游改变常量写法时继续默认拒绝，需重新核对原因。

活跃笔记中没有便携包凭据扫描的既有决定；菜单定位与笔记安装记录与此问题无关。

## Alternatives considered

- **不随包提供 cryptography**：无需修改扫描器，但用量插件缺少声明的运行依赖，无法直接启用。
- **忽略整个第三方目录或文件**：维护简单，但会漏检同文件中混入的真实凭据，因此只处理精确的公开常量行。
- **仅识别完整 PEM 块**：能自然排除解析器常量，但会放过截断的密钥材料，因此保留原私钥头规则。

## Verification

- `tests/PortablePackageSafetyTest.ps1` 从脚本 AST 加载实际 `Assert-PackageSafe` 及其依赖函数，不执行复制、删除或压缩主流程。
- 使用 PowerShell 5.1 执行回归通过：指定路径的公开常量和 CRLF 文件通过；相同常量放在其他路径、真实私钥、同文件追加私钥或令牌、常量行额外内容均被拒绝。
- 回归通过 `xuanhub_portable_package_safety_tests` 接入 CTest。实际便携打包继续执行 staging 与最终 ZIP 门禁。

## Consequences

- **收益**：允许分发所需的 cryptography 源码，同时保持原来的私钥头、令牌、本机路径等规则；没有跳过任何完整文件或目录。
- **代价**：该例外依赖第三方源码的具体公开常量写法；依赖升级后可能再次触发安全检查，需要人工核对。
- **边界**：仅处理已核对的公开标记，不证明第三方依赖无其他安全问题；所有其他发布安全检查保持原有要求。
