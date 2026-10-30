# SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

import native_chunks


class NativeChunkPackagingTest(unittest.TestCase):
    def test_removes_stale_chunks_when_source_is_small_or_missing(self):
        for source_exists in (True, False):
            with self.subTest(source_exists=source_exists):
                with tempfile.TemporaryDirectory() as directory:
                    library = Path(directory) / "libcudf.so"
                    library.write_bytes(b"a" * 2048)
                    self.assertTrue(native_chunks.produce(library, 0, 1024, 9, 2))
                    chunks = Path(str(library) + ".chunks")
                    manifest = Path(str(library) + ".chunks.properties")
                    self.assertTrue(chunks.exists())
                    self.assertTrue(manifest.exists())

                    if source_exists:
                        library.write_bytes(b"b" * 1024)
                    self.assertFalse(native_chunks.produce(library, 2048, 1024, 9, 2))
                    self.assertEqual(source_exists, library.exists())
                    self.assertFalse(chunks.exists())
                    self.assertFalse(manifest.exists())

    def test_replaces_stale_chunks_after_source_recopy(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libcudf.so"
            library.write_bytes(b"a" * 2048)
            self.assertTrue(native_chunks.produce(library, 0, 1024, 9, 2))
            old_chunk = Path(str(library) + ".chunks/00000").read_bytes()

            library.write_bytes(b"b" * 2048)
            self.assertTrue(native_chunks.produce(library, 0, 1024, 9, 2))
            new_chunk = Path(str(library) + ".chunks/00000").read_bytes()
            self.assertNotEqual(old_chunk, new_chunk)
            self.assertFalse(library.exists())

    def test_round_trip_and_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            library = root / "amd64/Linux/libcudf.so"
            library.parent.mkdir(parents=True)
            contents = b"0123456789abcdef" * 200000
            library.write_bytes(contents)
            self.assertTrue(
                native_chunks.produce(library, 0, 1024 * 1024, 9, 2)
            )
            self.assertFalse(library.exists())
            archive = root / "test.jar"
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as jar:
                for file in library.parent.rglob("*"):
                    if file.is_file():
                        jar.write(file, file.relative_to(root))
            self.assertEqual(4, native_chunks.store(root, archive))
            self.assertEqual(4, native_chunks.verify(archive))

            first = library.parent / "libcudf.so.chunks/00000"
            first.write_bytes(b"broken")
            native_chunks.store(root, archive)
            with self.assertRaises(subprocess.CalledProcessError):
                native_chunks.verify(archive)


if __name__ == "__main__":
    unittest.main()
