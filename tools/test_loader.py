"""Exercise the kernel's real IMG1 parser on the host, without ARM or QEMU."""

import ctypes
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAX_SIZE = 0x200000


def make_image(entries=None, directory=20, image_size=None):
    if entries is None:
        entries = [(b"hello", 44, 40)]
    if image_size is None:
        image_size = max(directory + len(entries) * 24, *(o + s for _, o, s in entries))
    image = bytearray(image_size)
    struct.pack_into("<4s4I", image, 0, b"IMG1", 1, image_size, len(entries), directory)
    for i, (name, offset, size) in enumerate(entries):
        struct.pack_into("<16sII", image, directory + i * 24, name, offset, size)
    return image


class LoaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        # CC may be the ARM compiler in a build environment. HOST_CC explicitly
        # selects a native compiler, because ctypes loads the result on the host.
        library = Path(cls.temp.name) / "loader.so"
        subprocess.run(
            [
                *shlex.split(os.environ.get("HOST_CC", "cc")),
                "-std=c17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-shared",
                "-fPIC",
                "-I",
                str(ROOT),
                str(ROOT / "kernel/loader.c"),
                "-o",
                str(library),
            ],
            check=True,
        )
        cls.library = ctypes.CDLL(str(library))
        cls.validate = cls.library.img1_validate
        cls.validate.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.validate.restype = ctypes.c_uint

    def verdict(self, image, expected, *, available=None):
        # Offset one byte to exercise unaligned input too. Keep the buffer alive
        # across the C call and check that validation never changes its bytes.
        buffer = ctypes.create_string_buffer(b"!" + bytes(image))
        before = buffer.raw
        self.assertEqual(
            self.validate(
                ctypes.addressof(buffer) + 1,
                len(image) if available is None else available,
            ),
            expected,
        )
        self.assertEqual(buffer.raw, before)

    def test_valid_baseline(self):
        self.verdict(make_image(), 1)

    def test_header_fields(self):
        for field, values in (
            (0, [0, 0x31474D00]),
            (4, [0, 2, 0xFFFFFFFF]),
            (8, [0, 19, MAX_SIZE + 1, 0xFFFFFFFF]),
            (12, [0, 33, 0xFFFFFFFF]),
            (16, [0, 16, 21, 84, 88, 0xFFFFFFFC]),
        ):
            for value in values:
                with self.subTest(field=field, value=value):
                    image = make_image()
                    struct.pack_into("<I", image, field, value)
                    self.verdict(image, 0)

    def test_null_and_truncated_header(self):
        self.assertEqual(self.validate(None, MAX_SIZE), 0)
        for length in range(20):
            with self.subTest(length=length):
                self.verdict(make_image()[:length], 0)

    def test_declared_size_exceeds_available(self):
        self.verdict(make_image(), 0, available=83)

    def test_directory_must_fit_declared_image(self):
        for size in (20, 43):
            image = make_image()
            struct.pack_into("<I", image, 8, size)
            self.verdict(image, 0)

    def test_directory_at_nondefault_offset_with_nonzero_gap(self):
        image = make_image([(b"hello", 48, 40)], directory=24)
        image[20:24] = b"\xff" * 4
        self.verdict(image, 1)
        image[24] = ord("!")
        self.verdict(image, 0)

    def test_names(self):
        for name, valid in (
            (b"", 0),
            (b"a", 1),
            (b"ABCDEFGHIJKLMNO", 1),
            (b"ABCDEFGHIJKLMNOP", 0),
            (b"Az09_-", 1),
            (b"has space", 0),
            (b"dot.name", 0),
            (b"slash/name", 0),
            (b"\xff", 0),
            (b"ok\0\xffinvalid", 1),
        ):
            with self.subTest(name=name):
                self.verdict(make_image([(name, 44, 40)]), valid)

    def test_file_ranges(self):
        for offset, size in (
            (0, 0),
            (40, 40),
            (45, 40),
            (44, 39),
            (44, 0),
            (84, 40),
            (88, 40),
            (44, 41),
            (0xFFFFFFFC, 40),
            (44, 0xFFFFFFFF),
        ):
            with self.subTest(offset=offset, size=size):
                image = make_image()
                struct.pack_into("<II", image, 36, offset, size)
                self.verdict(image, 0)

    def test_duplicate_names_ignore_bytes_after_nul(self):
        for name in (b"hello", b"hello\0garbage"):
            self.verdict(make_image([(b"hello", 68, 40), (name, 108, 40)]), 0)
        self.verdict(make_image([(b"hello", 68, 40), (b"Hello", 108, 40)]), 1)

    def test_overlaps_in_both_orders(self):
        for first, second in ((68, 68), (68, 72), (72, 68)):
            self.verdict(make_image([(b"one", first, 40), (b"two", second, 40)]), 0)
        # Containment, rather than merely partial overlap.
        self.verdict(make_image([(b"one", 68, 100), (b"two", 72, 40)]), 0)

    def test_adjacent_unsorted_files(self):
        self.verdict(make_image([(b"one", 108, 40), (b"two", 68, 40)]), 1)

    def test_gaps_and_odd_file_sizes(self):
        self.verdict(make_image([(b"one", 72, 41), (b"two", 120, 43)]), 1)

    def test_maximum_count_and_image_size(self):
        start = 20 + 32 * 24
        entries = [(f"p{i}".encode(), start + i * 40, 40) for i in range(32)]
        self.verdict(make_image(entries, image_size=MAX_SIZE), 1)
        self.verdict(make_image([(b"last", MAX_SIZE - 40, 40)]), 1)

    def test_no_program_content_validation(self):
        # make_image fills payload with zeros, which is not a valid ELF or SFT1.
        self.verdict(make_image(), 1)


if __name__ == "__main__":
    unittest.main()
