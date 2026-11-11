import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("installer", Path(__file__).parents[1] / "install_personal_app.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root / "source"
        self.package.mkdir()
        self.card = self.root / "card"
        self.card.mkdir()
        self.manifest = {"id": "example", "name": "Example", "version": "1.0", "description": "Example app", "api_version": 1, "entry": "main.lua", "capabilities": []}
        self.write_manifest()
        (self.package / "main.lua").write_text("return {}")

    def write_manifest(self):
        (self.package / "manifest.json").write_text(json.dumps(self.manifest))

    def test_replacement_keeps_data(self):
        destination = installer.install(self.package, self.card)
        data = self.card / "appdata/example"
        data.mkdir(parents=True)
        (data / "settings").write_text("private")
        (self.package / "main.lua").write_text("return { update = function() end }")
        installer.install(self.package, self.card)
        self.assertEqual((destination / "main.lua").read_text(), (self.package / "main.lua").read_text())
        self.assertEqual((data / "settings").read_text(), "private")

    def test_unsafe_ids_and_links_rejected(self):
        for app_id in ("../escape", ".hidden", "with/slash", "a..b", "A", "x" * 32):
            self.manifest["id"] = app_id
            self.write_manifest()
            with self.assertRaises(ValueError):
                installer.install(self.package, self.card)
        self.manifest["id"] = "example"
        self.write_manifest()
        (self.package / "linked").symlink_to(self.card)
        with self.assertRaises(ValueError):
            installer.install(self.package, self.card)

    def test_api_requires_integer(self):
        for version in (True, 1.0, "1", 2):
            self.manifest["api_version"] = version
            self.write_manifest()
            with self.assertRaises(ValueError):
                installer.install(self.package, self.card)

    def test_bad_capabilities_and_root_link(self):
        for caps in (None, ["network"], ["wifi", "wifi"], [42]):
            self.manifest["capabilities"] = caps
            self.write_manifest()
            with self.assertRaises(ValueError):
                installer.install(self.package, self.card)
        self.manifest["capabilities"] = []
        self.write_manifest()
        link = self.root / "source-link"
        link.symlink_to(self.package)
        with self.assertRaises(ValueError):
            installer.install(link, self.card)

    def test_interrupted_copy_preserves_previous(self):
        destination = installer.install(self.package, self.card)
        with patch.object(installer.shutil, "copytree", side_effect=OSError("removed card")):
            with self.assertRaises(OSError):
                installer.install(self.package, self.card)
        self.assertEqual((destination / "main.lua").read_text(), "return {}")

    def test_interrupted_rename_restores_previous(self):
        destination = installer.install(self.package, self.card)
        destination.rename(self.card / "apps/.example.previous")
        installer.install(self.package, self.card)
        self.assertTrue(destination.is_dir())
        self.assertFalse((self.card / "apps/.example.previous").exists())


if __name__ == "__main__":
    unittest.main()
