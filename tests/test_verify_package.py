"""Reject incomplete or ambiguous package receipts before extracting binaries."""
import hashlib
import json
from pathlib import Path
import stat
import tempfile
import unittest
import zipfile

from tools import verify_package


class VerifyPackageTests(unittest.TestCase):
    def setUp(self):
        base=Path(__file__).resolve().parents[1]/'.local/tests'
        base.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.archive=self.root/'candidate.zip'
        self.folder=self.root/'extract'

    def candidate(self, names=('tool',), extra=None, kind=stat.S_IFREG, corrupt=False):
        data=b'original test fixture'
        manifest={'schema':'gtracker.developer-package','version':1,'profile':'desktop',
                  'files':[{'path':name,'size_bytes':len(data),
                            'sha256':hashlib.sha256(data).hexdigest()} for name in names]}
        with zipfile.ZipFile(self.archive,'w') as archive:
            archive.writestr('gtracker/package.json',json.dumps(manifest))
            for name in names:
                entry=zipfile.ZipInfo('gtracker/'+name)
                entry.external_attr=(kind|0o755)<<16
                archive.writestr(entry,b'changed' if corrupt else data)
            if extra: archive.writestr(extra,data)

    def test_complete_manifest_extracts(self):
        self.candidate()
        manifest=verify_package.extract_verified(self.archive,self.folder)
        self.assertEqual(manifest['version'],1)
        self.assertEqual((self.folder/'gtracker/tool').read_bytes(),b'original test fixture')

    def test_rejects_unlisted_file_before_extraction(self):
        self.candidate(extra='gtracker/unlisted')
        with self.assertRaisesRegex(ValueError,'payloads differ'):
            verify_package.extract_verified(self.archive,self.folder)
        self.assertFalse(self.folder.exists())

    def test_rejects_case_collisions(self):
        self.candidate(names=('tool','Tool'))
        with self.assertRaisesRegex(ValueError,'Duplicate'):
            verify_package.extract_verified(self.archive,self.folder)
        self.assertFalse(self.folder.exists())

    def test_rejects_links_and_corruption_before_extraction(self):
        for options in ({'kind':stat.S_IFLNK},{'corrupt':True}):
            with self.subTest(options=options):
                self.candidate(**options)
                with self.assertRaises(ValueError):
                    verify_package.extract_verified(self.archive,self.folder)
                self.assertFalse(self.folder.exists())

    def test_rejects_noncanonical_or_escaping_paths(self):
        for name in ('../escape','/absolute','a/../escape','a//b','a/./b',
                     'C:/escape','a\\b','a:stream','a.','a '):
            with self.subTest(name=name),self.assertRaises(ValueError):
                verify_package.package_path(name)


if __name__=='__main__':
    unittest.main()
