"""Public packer contract from ASSIGNMENT.md sections 6.1 and 6.3.

Run from the repository root:
    python3 -m unittest discover -s tools -v

Expected fields use the published format, without importing implementation
constants or private helpers. Payloads are opaque bytes because this API packs
files; ELF parsing and command-line handling are separate responsibilities.
"""

import struct
import unittest

from tools.mkimage import check_img_size, pack_into_img, validate_inputs


class ValidateInputsTests(unittest.TestCase):
    def test_accepts_program_count_boundaries(self):
        for count in (1, 32):
            with self.subTest(count=count):
                validate_inputs([(f"p{i}", b"x" * 40) for i in range(count)])

    def test_rejects_too_many_programs(self):
        with self.assertRaisesRegex(ValueError, "between 1 and 32 programs"):
            validate_inputs([(f"p{i}", b"x" * 40) for i in range(33)])

    def test_rejects_zero_programs(self):
        with self.assertRaises(ValueError):
            validate_inputs([])

    def test_rejects_duplicate_names_even_when_separated(self):
        for names in (("same", "same"), ("same", "other", "same")):
            with self.subTest(names=names):
                with self.assertRaises(ValueError):
                    validate_inputs([(name, b"x" * 40) for name in names])

    def test_accepts_case_distinct_names(self):
        validate_inputs([("hello", b"a" * 40), ("Hello", b"b" * 40)])

    def test_accepts_allowed_name_characters_and_length_boundaries(self):
        names = [
            "x",
            "ABCDEFGHIJKLMNO",
            "PQRSTUVWXYZ",
            "abcdefghijklmno",
            "pqrstuvwxyz",
            "0123456789_-",
        ]
        for name in names:
            with self.subTest(name=name):
                validate_inputs([(name, b"x" * 40)])

    def test_rejects_names_outside_length_limits(self):
        for name in ("", "a" * 16):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "invalid IMG1 name"):
                    validate_inputs([(name, b"x" * 40)])

    def test_rejects_disallowed_name_characters(self):
        for name in (
            "has space",
            "has.dot",
            "path/name",
            "back\\slash",
            "has=equals",
            "has\ttab",
            "has\nnewline",
            "nul\0byte",
            "\x7f",
            "café",
            "１",
            chr(0x1F600),
        ):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "invalid IMG1 name"):
                    validate_inputs([(name, b"x" * 40)])

    def test_accepts_program_size_boundaries(self):
        # A single program leaves 20 header bytes and 24 directory bytes.
        for size in (40, 0x00200000 - 20 - 24):
            with self.subTest(size=size):
                validate_inputs([("program", b"x" * size)])

    def test_rejects_program_sizes_outside_limits_in_any_position(self):
        for size in (0, 1, 39, 0x00200000 - 20 - 24 + 1):
            for position in (0, 1):
                programs = [("valid", b"v" * 40)]
                programs.insert(position, ("invalid", b"x" * size))
                with self.subTest(size=size, position=position):
                    with self.assertRaisesRegex(
                        ValueError, "at least 40 bytes|image exceeds 2 MiB"
                    ):
                        validate_inputs(programs)

    def test_rejects_invalid_name_after_valid_program(self):
        with self.assertRaisesRegex(ValueError, "invalid IMG1 name"):
            validate_inputs([("valid", b"v" * 40), ("bad.name", b"x" * 40)])

    def test_accepts_total_image_size_at_limit(self):
        # The second program starts at 112 after three padding bytes.
        validate_inputs([("first", b"a" * 41), ("last", b"b" * (0x00200000 - 112))])

    def test_total_size_limit_includes_directories_and_alignment(self):
        for last_size in (0x00200000 - 112 + 1, 0x00200000 - 68 - 41):
            with self.subTest(last_size=last_size):
                with self.assertRaisesRegex(ValueError, "image exceeds 2 MiB"):
                    validate_inputs([("first", b"a" * 41), ("last", b"b" * last_size)])


class CheckImgSizeTests(unittest.TestCase):
    # This function checks byte length only. IMG1 structure and padding
    # generation are covered separately by PackIntoImgTests.
    def test_accepts_images_at_or_below_size_limit(self):
        for size in (20, 0x00200000 - 1, 0x00200000):
            with self.subTest(size=size):
                check_img_size(bytes(size))

    def test_rejects_images_over_size_limit(self):
        for size in (0x00200000 + 1, 0x00200000 + 3):
            with self.subTest(size=size):
                with self.assertRaisesRegex(
                    Exception,
                    f"^IMG1 image must not exceed 2 MiB, actual size = {size} bytes$",
                ):
                    check_img_size(bytes(size))


class PackIntoImgTests(unittest.TestCase):
    def assert_image(self, programs):
        """Read the result as a consumer, checking the IMG1 layout rules."""
        image = pack_into_img(programs)
        self.assertIsInstance(image, bytes)
        self.assertGreaterEqual(len(image), 20)
        self.assertLessEqual(len(image), 0x00200000)
        magic, version, image_size, count, directory_offset = struct.unpack_from(
            "<4s4I", image
        )
        self.assertEqual(magic, b"IMG1")
        self.assertEqual(version, 1)
        self.assertEqual(image_size, len(image))
        self.assertEqual(count, len(programs))
        self.assertEqual(directory_offset, 20)

        previous_end = directory_offset + count * 24
        for index, (name, content) in enumerate(programs):
            name_field, offset, size = struct.unpack_from(
                "<16sII", image, directory_offset + index * 24
            )
            self.assertEqual(name_field, name.encode("ascii").ljust(16, b"\0"))
            self.assertEqual(offset % 4, 0)
            self.assertGreaterEqual(offset, previous_end)
            self.assertLess(offset - previous_end, 4)
            self.assertEqual(image[previous_end:offset], bytes(offset - previous_end))
            self.assertEqual(size, len(content))
            self.assertGreaterEqual(size, 40)
            self.assertLessEqual(offset + size, image_size)
            self.assertEqual(image[offset : offset + size], content)
            previous_end = offset + size

        # image_size ends at the last file, with no trailing padding.
        self.assertEqual(previous_end, image_size)
        return image

    def test_single_file_matches_literal_wire_format(self):
        payload = bytes(range(40))
        # Header: version 1, size 84, count 1, directory at 20.
        # Entry: name "hello", content at 44, size 40.
        expected = (
            b"IMG1"
            b"\x01\x00\x00\x00"
            b"\x54\x00\x00\x00"
            b"\x01\x00\x00\x00"
            b"\x14\x00\x00\x00"
            b"hello" + b"\0" * 11 + b"\x2c\x00\x00\x00"
            b"\x28\x00\x00\x00" + payload
        )
        self.assertEqual(pack_into_img([("hello", payload)]), expected)

    def test_preserves_order_and_all_payload_bytes(self):
        self.assert_image(
            [
                ("z-last", bytes(range(256))),
                ("a-first", b"\xff\x00IMG1\x00" * 7),
                ("middle", b"\x80" * 43),
            ]
        )

    def test_each_alignment_remainder_and_no_trailing_padding(self):
        for remainder in range(4):
            with self.subTest(remainder=remainder):
                self.assert_image(
                    [("first", b"a" * (40 + remainder)), ("last", b"b" * 41)]
                )

    def test_accepts_all_allowed_name_characters(self):
        # These names cover A-Z, a-z, 0-9, underscore and hyphen.
        names = ["ABCDEFGHIJKLMNO", "PQRSTUVWXYZ", "abcdefghijklmno", "pqrstuvwxyz"]
        names += ["0123456789_-", "x"]
        self.assert_image([(name, b"x" * 40) for name in names])

    def test_names_are_case_sensitive(self):
        self.assert_image([("hello", b"a" * 40), ("Hello", b"b" * 40)])

    def test_accepts_maximum_program_count(self):
        self.assert_image([(f"p{i}", bytes([i]) * 40) for i in range(32)])

    def test_rejects_program_counts_outside_limits(self):
        for count in (0, 33):
            with self.subTest(count=count), self.assertRaises(ValueError):
                pack_into_img([(f"p{i}", b"x" * 40) for i in range(count)])

    def test_rejects_invalid_names(self):
        names = [
            "",
            "a" * 16,
            "has space",
            "has.dot",
            "path/name",
            "back\\slash",
            "has=equals",
            "has\ttab",
            "has\nnewline",
            "nul\0byte",
            "\x7f",
            "café",
            "１",
            chr(0x1F600),
        ]
        for name in names:
            with self.subTest(name=name), self.assertRaises(ValueError):
                pack_into_img([(name, b"x" * 40)])

    def test_rejects_duplicate_names_even_when_separated(self):
        for names in (("same", "same"), ("same", "other", "same")):
            with self.subTest(names=names), self.assertRaises(ValueError):
                pack_into_img([(name, b"x" * 40) for name in names])

    def test_rejects_files_smaller_than_directory_minimum(self):
        for size in (0, 1, 39):
            for position in (0, 1):
                programs = [("valid", b"v" * 40)]
                programs.insert(position, ("short", b"x" * size))
                with (
                    self.subTest(size=size, position=position),
                    self.assertRaises(ValueError),
                ):
                    pack_into_img(programs)

    def test_accepts_image_exactly_at_size_limit(self):
        # Two entries end at 68. The 41-byte first file ends at 109;
        # the second starts at 112 after three zero padding bytes.
        self.assert_image([("first", b"a" * 41), ("last", b"b" * (0x00200000 - 112))])

    def test_rejects_image_one_byte_over_size_limit(self):
        with self.assertRaises(ValueError):
            pack_into_img([("large", b"x" * (0x00200000 - 44 + 1))])

    def test_size_limit_includes_alignment_padding(self):
        # Header, entries and payloads alone total 2 MiB, but the first
        # payload requires three more bytes of padding before the second.
        with self.assertRaises(ValueError):
            pack_into_img(
                [("first", b"a" * 41), ("last", b"b" * (0x00200000 - 68 - 41))]
            )


if __name__ == "__main__":
    unittest.main()
