import struct
import zlib

def create_chunk(chunk_type: bytes, data: bytes) -> bytes:
    """Create a PNG chunk with proper length and CRC."""
    length = struct.pack(">I", len(data))
    crc = struct.pack(">I", zlib.crc32(chunk_type + data) & 0xffffffff)
    return length + chunk_type + data + crc

with open("malformed_iccp.png", "wb") as f:
    # PNG signature
    f.write(b"\x89PNG\r\n\x1a\n")

    # IHDR chunk: width=1, height=1, bit depth=8, color type=2 (RGB)
    ihdr_data = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    f.write(create_chunk(b'IHDR', ihdr_data))

    # Malformed iCCP chunk:
    # Keyword: "bad" + null byte
    # Compression method: 0
    # Compressed profile: only 3 bytes, not enough for a valid profile (libpng expects at least 4)
    iccp_keyword = b"bad\x00"
    iccp_compression = b"\x00"
    malformed_profile = b"\xDE\xAD\xBE"  # < 4 bytes
    iccp_data = iccp_keyword + iccp_compression + malformed_profile
    f.write(create_chunk(b'iCCP', iccp_data))

    # IDAT chunk: minimal 1x1 red pixel (no filter, RGB)
    raw_pixel = b"\x00\xff\x00\x00"  # No filter + RGB = red pixel
    compressed_pixel = zlib.compress(raw_pixel)
    f.write(create_chunk(b'IDAT', compressed_pixel))

    # IEND chunk
    f.write(create_chunk(b'IEND', b''))