# generate_plugin_signing_key.py — 创建 XuanHub 插件发布者的本地 P-256 私钥文件
from __future__ import annotations

import argparse
import json
from pathlib import Path

from plugin_signing import SigningError, write_key_file


def main() -> int:
    parser = argparse.ArgumentParser(description="创建 XuanHub 插件发布者签名密钥")
    parser.add_argument("--output", type=Path, required=True,
                        help="私钥 JSON 输出路径；已存在时拒绝覆盖")
    parser.add_argument("--publisher", default="", help="可选发布者显示名称")
    parser.add_argument("--json", action="store_true", help="输出机器可读结果")
    arguments = parser.parse_args()
    try:
        document = write_key_file(arguments.output, arguments.publisher)
        result = {
            "ok": True,
            "path": str(arguments.output.expanduser().resolve(strict=False)),
            "algorithm": document["algorithm"],
            "keyId": document["keyId"],
            "publisher": document.get("publisher", ""),
        }
    except (OSError, SigningError) as error:
        result = {"ok": False, "error": str(error)}
        if arguments.json:
            print(json.dumps(result, ensure_ascii=False, sort_keys=True))
        else:
            print(f"创建密钥失败：{error}")
        return 1
    if arguments.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        print(f"已创建私钥：{result['path']}")
        print(f"公钥标识：{result['keyId']}")
        print("请离线备份并保护该文件；不要把它放进插件目录或提交到版本库。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
