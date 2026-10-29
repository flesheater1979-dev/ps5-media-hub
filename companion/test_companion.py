import importlib.util
import pathlib
import tempfile
import unittest
import json
import hashlib

spec = importlib.util.spec_from_file_location("companion", pathlib.Path(__file__).with_name("companion.py"))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class CompanionTests(unittest.TestCase):
    def test_target_restrictions(self):
        self.assertEqual(c.validate_target("192.168.1.20", "2121"), ("192.168.1.20", 2121))
        for host in ("8.8.8.8", "127.0.0.1", "169.254.1.1", "console.local", "::1"):
            with self.assertRaises(ValueError):
                c.validate_target(host, 2121)
        with self.assertRaises(ValueError):
            c.validate_target("10.0.0.4", 65536)

    def test_hash_verification_rejects_changed_image(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / (c.TITLE_ID + ".ffpfsc")
            path.write_bytes(b"fixture")
            digest = hashlib.sha256(b"fixture").hexdigest()
            path.with_name("build-manifest.json").write_text(json.dumps({"artifacts": [{"name":path.name,"size":7,"sha256":digest}]}))
            self.assertEqual(c.load_build(path), (b"fixture", digest))
            path.write_bytes(b"changed")
            with self.assertRaises(ValueError):
                c.load_build(path)

    def test_redacts_headers_and_signed_urls(self):
        result = c.redact_log("Authorization: Bearer secret\napi_key=example&x=1\nhttps://host/video?t=private")
        for secret in ("secret", "example", "private"):
            self.assertNotIn(secret, result)


if __name__ == "__main__":
    unittest.main()
