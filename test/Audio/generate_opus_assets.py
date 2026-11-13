#!/usr/bin/env python3
"""Regenerate the Ogg Opus test assets used by the SFML audio test suite.

The generated .opus files are committed to the repository next to this
script. To reproduce them, install opus-tools (opusenc) and run:

    python3 test/Audio/generate_opus_assets.py test/Audio

The exact bytes of the encoded files may depend on the opusenc version,
but the properties verified by the tests (channel count, sample rate,
sample count, duration, seek behavior) do not.

Assets generated:
  opus_mono_48k.opus            mono,   48 kHz source, 1 s sine
  opus_stereo_48k.opus          stereo, 48 kHz source, 1 s sine pair
  opus_mono_44k.opus            mono, 44.1 kHz source, 1 s sine (resampled to 48 kHz by the encoder)
  opus_mono_16k.opus            mono,   16 kHz source, 1 s sine (resampled to 48 kHz by the encoder)
  opus_long_mono.opus           mono,   48 kHz source, 30 s sine (many pages, used for seek tests)
  opus_truncated.opus           stereo file cut in the middle of an audio page
  opus_head_only.opus           only the OpusHead page, no OpusTags (must fail to open)
  opus_bad_tags.opus            corrupted OpusTags magic, valid page CRCs (must fail to open)
  opus_bad_head_magic.opus      corrupted OpusHead magic (must be rejected by the format check)
  opus_bad_head_channels.opus   unsupported channel count 9 (must fail to open)
  opus_bad_head_family.opus     reserved channel mapping family 2 (must fail to open)
  opus_missing_mapping.opus     mapping family 1 without mapping table (must fail to open)
"""

import math
import struct
import subprocess
import sys
import wave
from pathlib import Path

# Ogg CRC-32 (polynomial 0x04C11DB7, no reflection)
CRC_TABLE = []
for i in range(256):
    value = i << 24
    for _ in range(8):
        value = ((value << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if value & 0x80000000 else (value << 1) & 0xFFFFFFFF
    CRC_TABLE.append(value)


def ogg_crc(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]
    return crc


def page_bounds(data: bytes, offset: int) -> tuple[int, int]:
    """Return (offset, end_offset) of the Ogg page at the given offset."""
    assert data[offset:offset + 4] == b"OggS", f"no capture pattern at offset {offset}"
    segment_count = data[offset + 26]
    body_size = sum(data[offset + 27:offset + 27 + segment_count])
    return offset, offset + 27 + segment_count + body_size


def find_page_containing(data: bytes, position: int) -> int:
    offset = 0
    while offset < len(data):
        start, end = page_bounds(data, offset)
        if start <= position < end:
            return start
        offset = end
    raise ValueError(f"position {position} is not inside any Ogg page")


def fix_page_crc(data: bytearray, page_offset: int) -> None:
    """Recompute the CRC of the page at the given offset after modifying its contents."""
    start, end = page_bounds(data, page_offset)
    data[page_offset + 22:page_offset + 26] = b"\0\0\0\0"
    data[page_offset + 22:page_offset + 26] = ogg_crc(bytes(data[start:end])).to_bytes(4, "little")


def write_wav(path: Path, sample_rate: int, channels: list[list[int]]) -> None:
    """Write a 16-bit PCM WAV file from per-channel sample lists."""
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(len(channels))
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        frames = b"".join(
            struct.pack("<" + "h" * len(channels), *(channel[i] for channel in channels))
            for i in range(len(channels[0]))
        )
        wav.writeframes(frames)


def sine(sample_rate: int, frequency: float, amplitude: float, count: int, phase: float = 0.0) -> list[int]:
    return [int(amplitude * math.sin(phase + 2.0 * math.pi * frequency * i / sample_rate)) for i in range(count)]


def encode(work_dir: Path, output_dir: Path, name: str, sample_rate: int,
           channels: list[list[int]], bitrate: int) -> bytes:
    """Encode the given channels to an Ogg Opus file and return its bytes."""
    wav_path = work_dir / f"{name}.wav"
    opus_path = output_dir / f"{name}.opus"
    write_wav(wav_path, sample_rate, channels)
    subprocess.run(["opusenc", "--quiet", "--bitrate", str(bitrate), str(wav_path), str(opus_path)], check=True)
    return opus_path.read_bytes()


def main() -> None:
    output_dir = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    work_dir = output_dir / ".opus_asset_tmp"
    work_dir.mkdir(exist_ok=True)

    # Mono and stereo files encoded from different source sample rates.
    # Opus always decodes at 48 kHz, so all of them must report 48000 Hz.
    encode(work_dir, output_dir, "opus_mono_48k", 48000, [sine(48000, 440.0, 16000, 48000)], 64)
    stereo = encode(work_dir, output_dir, "opus_stereo_48k", 48000,
                    [sine(48000, 440.0, 12000, 48000), sine(48000, 660.0, 12000, 48000, math.pi / 4.0)], 96)
    encode(work_dir, output_dir, "opus_mono_44k", 44100, [sine(44100, 330.0, 16000, 44100)], 64)
    encode(work_dir, output_dir, "opus_mono_16k", 16000, [sine(16000, 220.0, 16000, 16000)], 48)

    # Long file spanning many Ogg pages to exercise seeking (page bisection)
    encode(work_dir, output_dir, "opus_long_mono", 48000, [sine(48000, 440.0, 16000, 48000 * 30)], 32)

    # Truncated in the middle of the last audio page: must still open, with a
    # shorter duration derived from the last complete page
    page_offsets = []
    offset = 0
    while offset < len(stereo):
        start, end = page_bounds(stereo, offset)
        page_offsets.append((start, end))
        offset = end
    last_start, last_end = page_offsets[-1]
    (output_dir / "opus_truncated.opus").write_bytes(stereo[:(last_start + last_end) // 2])

    # Only the OpusHead page: missing OpusTags header, must fail to open
    _, first_page_end = page_bounds(stereo, 0)
    (output_dir / "opus_head_only.opus").write_bytes(stereo[:first_page_end])

    # Corrupted OpusTags magic (page CRCs fixed so the tags validation itself is exercised)
    data = bytearray(stereo)
    tags_pos = data.find(b"OpusTags")
    assert tags_pos > 0
    data[tags_pos:tags_pos + 8] = b"OpusTogs"
    fix_page_crc(data, find_page_containing(data, tags_pos))
    (output_dir / "opus_bad_tags.opus").write_bytes(data)

    # Corrupted OpusHead magic: must be rejected by the format check already
    data = bytearray(stereo)
    head_pos = data.find(b"OpusHead")
    assert head_pos > 0
    data[head_pos + 7:head_pos + 8] = b"X"
    fix_page_crc(data, find_page_containing(data, head_pos))
    (output_dir / "opus_bad_head_magic.opus").write_bytes(data)

    # Unsupported channel count 9
    data = bytearray(stereo)
    head_pos = data.find(b"OpusHead")
    data[head_pos + 9] = 9
    fix_page_crc(data, find_page_containing(data, head_pos))
    (output_dir / "opus_bad_head_channels.opus").write_bytes(data)

    # Reserved channel mapping family 2
    data = bytearray(stereo)
    head_pos = data.find(b"OpusHead")
    data[head_pos + 18] = 2
    fix_page_crc(data, find_page_containing(data, head_pos))
    (output_dir / "opus_bad_head_family.opus").write_bytes(data)

    # Mapping family 1 requires a mapping table that the (unmodified, 19-byte) header does not contain
    data = bytearray(stereo)
    head_pos = data.find(b"OpusHead")
    data[head_pos + 18] = 1
    fix_page_crc(data, find_page_containing(data, head_pos))
    (output_dir / "opus_missing_mapping.opus").write_bytes(data)

    # Clean up the intermediate WAV files
    for wav_path in work_dir.glob("*.wav"):
        wav_path.unlink()
    work_dir.rmdir()

    print(f"Opus test assets written to {output_dir}")


if __name__ == "__main__":
    main()
