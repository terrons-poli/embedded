"""Command-line contract of tools/mkimage.py from ASSIGNMENT.md section 6.3.

Run from the repository root:
    python3 -m unittest discover -s tools -v

Each test runs the documented `image` command in a subprocess and inspects the
resulting file, so the tests do not depend on the implementation's helpers.
"""

import os
import signal
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

MKIMAGE = Path(__file__).resolve().parent / "mkimage.py"


def run_mkimage(*args, preexec_fn=None):
    return subprocess.run(
        [sys.executable, str(MKIMAGE), *map(str, args)],
        capture_output=True,
        text=True,
        preexec_fn=preexec_fn,
    )


def limit_file_size():
    # Let writes fail with EFBIG after 100 bytes instead of killing the process.
    import resource

    resource.setrlimit(resource.RLIMIT_FSIZE, (100, 100))
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)


class ImageCommandTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)
        self.program = self.dir / "program.elf"
        self.program.write_bytes(b"p" * 4096)

    def test_accepts_name_starting_with_hyphen(self):
        output = self.dir / "fs.img"
        result = run_mkimage("image", output, f"-ok={self.program}")

        self.assertEqual(result.returncode, 0, result.stderr)
        image = output.read_bytes()
        self.assertEqual(struct.unpack_from("<I", image, 12)[0], 1)
        self.assertEqual(image[20:36], b"-ok".ljust(16, b"\0"))

    def test_requires_at_least_one_program(self):
        output = self.dir / "fs.img"
        result = run_mkimage("image", output)

        self.assertEqual(result.returncode, 2)
        self.assertIn("mkimage.py: error:", result.stderr)
        self.assertFalse(output.exists())

    @unittest.skipUnless(hasattr(signal, "SIGXFSZ"), "needs POSIX file-size limits")
    def test_failed_write_keeps_existing_output(self):
        output = self.dir / "fs.img"
        output.write_bytes(b"previous image")

        result = run_mkimage(
            "image", output, f"prog={self.program}", preexec_fn=limit_file_size
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("mkimage.py: error:", result.stderr)
        self.assertEqual(output.read_bytes(), b"previous image")
        self.assertEqual(sorted(os.listdir(self.dir)), ["fs.img", "program.elf"])

    @unittest.skipUnless(hasattr(signal, "SIGXFSZ"), "needs POSIX file-size limits")
    def test_failed_write_leaves_no_new_output(self):
        output = self.dir / "fs.img"

        result = run_mkimage(
            "image", output, f"prog={self.program}", preexec_fn=limit_file_size
        )

        self.assertEqual(result.returncode, 2)
        self.assertEqual(sorted(os.listdir(self.dir)), ["program.elf"])


if __name__ == "__main__":
    unittest.main()
