# Copyright (c) 2026, NVIDIA CORPORATION.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0

import tempfile
from pathlib import Path
import subprocess
import unittest
import zipfile

import native_chunks


class NativeChunkPackagingTest(unittest.TestCase):
    def test_round_trip_and_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            library = root / "amd64/Linux/libcudf.so"
            library.parent.mkdir(parents=True)
            contents = (b"0123456789abcdef" * 200000)
            library.write_bytes(contents)
            self.assertTrue(native_chunks.produce(library, 0, 1024 * 1024, 9, 2))
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
