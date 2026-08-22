# `.xuanplugin` 静态更新索引（Schema 1）

更新索引是用户显式添加的只读 JSON URL。玄枢只支持绝对 `https://` URL和本机
`file:///` URL，不接受 HTTP、URL 凭据或 fragment。检查更新只下载并解析索引，不会下载
插件包；用户确认安装某一项后才下载包，并重新经过完整 `.xuanplugin` 离线校验器。

## 格式

```json
{
  "schemaVersion": 1,
  "id": "example-releases",
  "packages": [
    {
      "id": "com.example.demo",
      "version": "1.2.0",
      "runtime": "native",
      "url": "packages/com.example.demo-1.2.0.xuanplugin",
      "sha256": "sha256:<64 个小写十六进制字符>",
      "size": 123456,
      "signerKeyId": "sha256:<64 个小写十六进制字符>"
    }
  ]
}
```

- `schemaVersion` 当前只能为整数 `1`；`id` 使用插件 ID 相同的安全字符集。
- `packages` 最多 4096 项；同一 `id + version` 不得重复。
- `version` 必须是 SemVer；`runtime` 只能是 `native` 或 `python`。
- `url` 可相对索引 URL，也可为绝对 HTTPS/本机 file URL；解析后仍须满足安全 URL 规则。
- `sha256`、`size` 必填。下载必须同时匹配字节数和 SHA-256，且不得超过插件包 256 MiB
  上限，然后才允许交给离线包校验器。
- `signerKeyId` 对签名包强烈建议必填。已安装版本有签名时，更新必须继续使用相同 keyId；
  不匹配视为发布者身份变化，普通更新流程必须拒绝。更换发布密钥需要用户重新走本地安装并
  明确确认，不能通过索引静默完成。

索引不是信任根：即使通过 HTTPS 获取，也不能替代插件包哈希、Manifest、权限、兼容性和
发布者签名校验。索引只用于发现候选版本；安装事务、失败回滚和版本保留继续由
`PluginPackageStore` 负责。
