# test_i18n_catalogs.py — 内置语言包结构、隔离和中英完整性回归
from __future__ import annotations

import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class I18nCatalogTests(unittest.TestCase):
    def _load_pair(self, directory: Path, plugin_id: str | None = None) -> None:
        catalogs = {}
        for locale in ("zh-CN", "en-US"):
            path = directory / "i18n" / f"{locale}.json"
            self.assertTrue(path.is_file(), f"missing catalog: {path}")
            obj = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(obj.get("locale"), locale, str(path))
            contexts = obj.get("contexts")
            self.assertIsInstance(contexts, dict, str(path))
            self.assertTrue(contexts, str(path))
            for context, messages in contexts.items():
                expected = f"plugin.{plugin_id}." if plugin_id else "xuanhub."
                self.assertTrue(context.startswith(expected), f"foreign context {context} in {path}")
                self.assertIsInstance(messages, dict, f"{context} in {path}")
                for source, translated in messages.items():
                    self.assertIsInstance(source, str)
                    self.assertIsInstance(translated, str)
                    self.assertTrue(translated, f"empty translation {context}/{source} in {path}")
            catalogs[locale] = contexts

        self.assertEqual(set(catalogs["zh-CN"]), set(catalogs["en-US"]), str(directory))
        for context in catalogs["zh-CN"]:
            self.assertEqual(set(catalogs["zh-CN"][context]),
                             set(catalogs["en-US"][context]), context)

    def test_core_catalogs_are_complete(self) -> None:
        self._load_pair(ROOT / "app")

    def test_all_built_in_plugin_catalogs_are_complete(self) -> None:
        plugin_dirs: list[tuple[Path, str]] = []
        for root_name, metadata_name in (("plugins", "metadata.json"),
                                         ("plugins_py", "plugin.json")):
            for metadata in (ROOT / root_name).glob(f"*/{metadata_name}"):
                obj = json.loads(metadata.read_text(encoding="utf-8"))
                plugin_id = obj.get("id")
                if plugin_id and plugin_id != "pybridge":
                    plugin_dirs.append((metadata.parent, plugin_id))
        # pybridge 是不可见 host 插件，但其原生通知同样必须有语言包。
        pybridge_meta = json.loads((ROOT / "plugins/pybridge/metadata.json").read_text(encoding="utf-8"))
        plugin_dirs.append((ROOT / "plugins/pybridge", pybridge_meta["id"]))

        self.assertTrue(plugin_dirs)
        for directory, plugin_id in plugin_dirs:
            with self.subTest(plugin=plugin_id):
                self._load_pair(directory, plugin_id)

    def test_all_translated_source_strings_exist_in_english(self) -> None:
        contexts: dict[str, dict[str, str]] = {}
        for catalog in [ROOT / "app/i18n/en-US.json", *ROOT.glob("plugins/*/i18n/en-US.json"),
                        *ROOT.glob("plugins_py/*/i18n/en-US.json")]:
            contexts.update(json.loads(catalog.read_text(encoding="utf-8"))["contexts"])

        direct = re.compile(r'(?:qsTranslate|QCoreApplication::translate)\s*\(\s*'
                            r'"([^"]+)"\s*,\s*"((?:\\.|[^"])*)"', re.S)
        helper_context = re.compile(r'qsTranslate\s*\(\s*"([^"]+)"\s*,\s*source\s*\)')
        helper_source = re.compile(r'(?:root\.)?trText\s*\(\s*"((?:\\.|[^"])*)"')
        for suffix in ("*.qml", "*.cpp"):
            for source_file in [*ROOT.glob(f"app/**/*{suffix[1:]}"),
                                *ROOT.glob(f"plugins/**/*{suffix[1:]}"),
                                *ROOT.glob(f"plugins_py/**/*{suffix[1:]}")]:
                text = source_file.read_text(encoding="utf-8-sig")
                expected = [(match.group(1), match.group(2).replace(r'\"', '"'))
                            for match in direct.finditer(text)]
                helper = helper_context.search(text)
                if helper:
                    expected.extend((helper.group(1), match.group(1))
                                    for match in helper_source.finditer(text))
                for context, source in expected:
                    with self.subTest(file=str(source_file.relative_to(ROOT)), context=context,
                                      source=source):
                        self.assertIn(context, contexts)
                        self.assertIn(source, contexts[context])

        for metadata_name in ("metadata.json", "plugin.json"):
            for metadata in [*ROOT.glob(f"plugins/*/{metadata_name}"),
                             *ROOT.glob(f"plugins_py/*/{metadata_name}")]:
                obj = json.loads(metadata.read_text(encoding="utf-8"))
                plugin_id = obj.get("id")
                if not plugin_id:
                    continue
                metadata_context = contexts.get(f"plugin.{plugin_id}.metadata", {})
                if obj.get("name"):
                    self.assertIn(obj["name"], metadata_context, str(metadata))
                settings_context = contexts.get(f"plugin.{plugin_id}.settings", {})
                for group in obj.get("settings", []):
                    for source in [group.get("group"), *(
                        field.get(key)
                        for field in group.get("fields", [])
                        for key in ("label", "description", "placeholder")
                    )]:
                        if source:
                            self.assertIn(source, settings_context, f"{metadata}: {source}")
                    for field in group.get("fields", []):
                        for option in field.get("options", []):
                            if isinstance(option, dict) and option.get("label"):
                                self.assertIn(option["label"], settings_context,
                                              f"{metadata}: {option['label']}")


if __name__ == "__main__":
    unittest.main()
