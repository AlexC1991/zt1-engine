"""Local pack format and conflict tests; no repository calls."""
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

import themepack


class ThemePackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for folder, resource in [('Plants', 'scenery/foliage/fern.ai'), ('Rocks', 'scenery/other/rock.ai')]:
            p = self.root / folder
            p.mkdir()
            (p / 'mod.json').write_text(json.dumps({'name': folder, 'id': folder.lower(), 'version': '1.2', 'replaces': [resource]}))
            with zipfile.ZipFile(p / 'content.ztd', 'w') as z:
                z.writestr(resource, folder)
                z.writestr('shared.cfg', 'identical')
            (p / '.art').mkdir()
            (p / '.art' / 'private.json').write_text('{}')
            (p / 'picture.png').write_bytes(b'not used')
            (p / 'repository-preview.png').write_bytes(folder.encode())
            meta = json.loads((p / 'mod.json').read_text())
            meta['picture'] = 'picture.png'
            (p / 'mod.json').write_text(json.dumps(meta))

    def tearDown(self):
        self.temp.cleanup()

    def make(self, **changes):
        return themepack.make({'modsFolder': str(self.root), 'folders': ['Plants', 'Rocks'], 'name': 'Habitat Pack', **changes})

    def test_merge_dedup_and_source_preservation(self):
        before = (self.root / 'Plants' / 'content.ztd').read_bytes()
        r = self.make()
        self.assertTrue(r['ok'])
        out = self.root / r['folder']
        self.assertEqual(sorted(p.name for p in out.iterdir()), ['content.ztd', 'mod.json'])
        with zipfile.ZipFile(out / 'content.ztd') as z:
            self.assertEqual(len(z.namelist()), 3)
        meta = json.loads((out / 'mod.json').read_text())
        self.assertEqual(len(meta['themePack']['components']), 2)
        self.assertEqual(len(meta['replaces']), 2)
        self.assertEqual(before, (self.root / 'Plants' / 'content.ztd').read_bytes())

    def test_conflict_is_reported_without_partial_pack(self):
        with zipfile.ZipFile(self.root / 'Rocks' / 'content.ztd', 'a') as z:
            z.writestr('SHARED.CFG', 'different')
        with self.assertRaisesRegex(ValueError, 'Conflict at shared.cfg: Plants and Rocks'):
            self.make()
        self.assertFalse((self.root / 'Habitat Pack').exists())

    def test_single_or_duplicate_mod_is_rejected(self):
        for folders in [['Plants'], ['Plants', 'Plants']]:
            with self.assertRaisesRegex(ValueError, 'at least two'):
                self.make(folders=folders)

    def test_source_folder_traversal_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'mods folder'):
            self.make(folders=['Plants', '../outside'])

    def test_zipped_path_traversal_is_rejected(self):
        with zipfile.ZipFile(self.root / 'Rocks' / 'content.ztd', 'a') as z:
            z.writestr('../escape.ai', 'bad')
        with self.assertRaisesRegex(ValueError, 'outside its mod'):
            self.make()

    def test_existing_output_is_preserved(self):
        self.make()
        before = (self.root / 'Habitat Pack' / 'mod.json').read_bytes()
        with self.assertRaisesRegex(ValueError, 'already has that folder name'):
            self.make()
        self.assertEqual(before, (self.root / 'Habitat Pack' / 'mod.json').read_bytes())

    def test_settings_require_migration(self):
        p = self.root / 'Plants' / 'mod.json'
        meta = json.loads(p.read_text())
        meta['settings'] = [{'name': 'brightness'}]
        p.write_text(json.dumps(meta))
        with self.assertRaisesRegex(ValueError, 'settings tied to its own mod identity'):
            self.make()


if __name__ == '__main__':
    unittest.main()
