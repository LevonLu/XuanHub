# compression.py — 有限长度的 Zstandard 解压与透明 JSONL 读取
from __future__ import annotations

import io
import json
from pathlib import Path

MAX_OUTPUT = 256 * 1024 * 1024


def _zstd_reader(stream):
    try:
        from compression import zstd
        return zstd.ZstdFile(stream, "rb")
    except ImportError:
        try:
            import zstandard
        except ImportError:
            raise RuntimeError("compression_unavailable") from None
        return zstandard.ZstdDecompressor().stream_reader(stream)


def decompress_zstd(data, max_output=MAX_OUTPUT):
    # 流式限长同时覆盖未在帧头声明内容长度的压缩数据。
    with _zstd_reader(io.BytesIO(data)) as stream:
        result = stream.read(max_output + 1)
        if len(result) > max_output:
            raise RuntimeError("decompressed_too_large")
        return result


def read_bytes(path, max_output=MAX_OUTPUT):
    path = Path(path)
    if path.suffix.lower() not in {".zst", ".zstd"}:
        with path.open("rb") as stream:
            result = stream.read(max_output + 1)
    else:
        with path.open("rb") as source, _zstd_reader(source) as stream:
            result = stream.read(max_output + 1)
    if len(result) > max_output:
        raise RuntimeError("decompressed_too_large")
    return result


def json_lines(path):
    if Path(path).suffix.lower() in {".zst", ".zstd"}:
        stream = io.BytesIO(read_bytes(path))
    else:
        stream = Path(path).open("rb")
    with stream:
        for index, line in enumerate(stream):
            try:
                value = json.loads(line)
            except (ValueError, UnicodeError):
                continue
            if isinstance(value, dict):
                yield index, value
