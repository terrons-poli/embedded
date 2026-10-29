IMG1_HEADER_SIZE = 20
IMG1_DIRECTORY_SIZE = 24
VERSION = 1


def _encode_int(n: int) -> bytes:
    return n.to_bytes(4, "little")


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
