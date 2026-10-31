from string import ascii_letters, digits
import argparse
from pathlib import Path

IMG1_HEADER_SIZE = 20
IMG1_DIRECTORY_SIZE = 24
VERSION = 1

PROGRAM_VOCABULARY = ascii_letters + digits + "_-"


def _encode_int(n: int) -> bytes:
    return n.to_bytes(4, "little")


def _check_program_name(name: str):
    if name == "" or len(name) > 15:
        raise Exception(f"program name must have between 1 and 15 characters: '{name}'")
    for c in name:
        if c not in PROGRAM_VOCABULARY:
            raise Exception(
                f"program name must only have characters in '{PROGRAM_VOCABULARY}': '{name}'"
            )


def _check_program_size(name: str, program: bytes):
    if (
        len(program) < 40
        or len(program) > 2**21 - IMG1_HEADER_SIZE - IMG1_DIRECTORY_SIZE
    ):
        raise Exception(
            f"program size must be between 40 B and 2 MiB: '{name}' is {len(program)} bytes long"
        )


def validate_inputs(programs_tuples: list[tuple[str, bytes]]):
    if len(programs_tuples) == 0 or len(programs_tuples) > 32:
        raise Exception(
            f"packer accepts between 1 and 32 programs: {len(programs_tuples)}"
        )

    names = {}

    for name, program in programs_tuples:
        _check_program_name(name)
        _check_program_size(name, program)

        if name in names:
            raise Exception(f"cannot accept duplicate program names: '{name}'")
        names[name] = 1


def pack_into_img(programs_tuples: list[tuple[str, bytes]]) -> bytes:
    """Pack files in input order, rejecting inputs that violate IMG1 rules.

    File contents are opaque here; ELF validation belongs to the caller.
    Invalid IMG1 metadata or sizes raise ValueError.
    """
    if not 1 <= len(programs_tuples) <= 32:
        raise ValueError("IMG1 requires between 1 and 32 programs")

    names = set()
    image_size = IMG1_HEADER_SIZE + len(programs_tuples) * IMG1_DIRECTORY_SIZE
    for name, content in programs_tuples:
        if not 1 <= len(name) <= 15 or any(
            not (char.isascii() and (char.isalnum() or char in "_-")) for char in name
        ):
            raise ValueError(f"invalid IMG1 name: {name!r}")
        if name in names:
            raise ValueError(f"duplicate IMG1 name: {name!r}")
        names.add(name)
        if len(content) < 40:
            raise ValueError(f"program {name!r} must contain at least 40 bytes")
        image_size += (-image_size) % 4 + len(content)
        if image_size > 0x00200000:
            raise ValueError("IMG1 image exceeds 2 MiB")

    # Header
    img = bytearray()

    img += b"IMG1"
    img += _encode_int(VERSION)
    img += bytes(4)  # insert the IMG1 size later
    img += _encode_int(len(programs_tuples))
    img += _encode_int(IMG1_HEADER_SIZE)

    # Directories and content
    directories = bytearray()
    contents = bytearray()
    base_offset = IMG1_HEADER_SIZE + len(programs_tuples) * IMG1_DIRECTORY_SIZE
    for i, (name, content) in enumerate(programs_tuples):
        directory = bytearray()

        # directory[0:16] = name suffixed with 0s
        directory += name.encode("ascii").ljust(16, b"\0")

        # directory[16:20] = offset of the program from the start of the image
        #
        # The padding for the `i`th program is inserted together with the `i+1`th
        # program.
        current_offset = base_offset + len(contents)
        padding_size = (-current_offset) % 4
        contents += bytes(padding_size)
        offset = current_offset + padding_size
        directory += _encode_int(offset)

        # directory[20:24] = program length (without padding)
        directory += _encode_int(len(content))

        directories += directory
        contents += content

    img += directories
    img += contents

    # Now that we know the length, add it
    img[8:12] = _encode_int(len(img))

    return bytes(img)


def check_img_size(img: bytes):
    if len(img) > 2**21:
        raise Exception(
            f"IMG1 image must not exceed 2 MiB, actual size = {len(img)} bytes"
        )


def parse_program(spec: str):
    if "=" not in spec:
        raise ValueError(f"expected NAME=FILE, got {spec!r}")
    name, filename = spec.split("=", 1)
    if not name:
        raise ValueError(f"expected NAME=FILE, got '={filename}'")
    if not filename:
        raise ValueError(f"expected NAME=FILE, got '{name}='")
    return name, Path(filename).read_bytes()


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="mkimage.py")
    commands = parser.add_subparsers(dest="command", required=True)

    image_parser = commands.add_parser(
        "image", help="pack programs into an IMG1 (provide them as NAME=ELF)"
    )
    image_parser.add_argument("output", type=Path)
    image_parser.add_argument("program", nargs="+")

    return parser


def parse_args(argv: list[str] | None = None) -> tuple[Path, list[tuple[str, bytes]]]:
    """Parse the `image` command and return the output path and program tuples.

    Bad arguments, unreadable files and invalid IMG1 inputs exit through
    argparse with `mkimage.py: error: <reason>` and status 2.
    """
    parser = _build_parser()
    args = parser.parse_args(argv)

    try:
        programs = [parse_program(item) for item in args.program]
        validate_inputs(programs)
    except Exception as e:
        parser.error(str(e))

    return args.output, programs


def main(argv: list[str] | None = None):
    output, programs = parse_args(argv)

    try:
        img = pack_into_img(programs)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(img)
    except (ValueError, OSError) as e:
        _build_parser().error(str(e))

    print(f"IMG1: {output} ({len(img)} bytes, {len(programs)} programs)")


if __name__ == "__main__":
    main()
