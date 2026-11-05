#!/usr/bin/env python3
#
# PROJECT:     MinedowsOS Bad Apple!! player
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Converts a Bad Apple!! video into the badapple.bin frame stream
# COPYRIGHT:   Copyright 2026 Kocurowy96
#

import argparse
import fractions
import struct
import subprocess
import sys
import zlib

MAGIC = b'BAPL'
VERSION = 1


def probe_rate(video):
    rate = subprocess.run(
        ['ffprobe', '-v', 'error', '-select_streams', 'v:0',
         '-show_entries', 'stream=avg_frame_rate', '-of', 'default=nw=1:nk=1', video],
        check=True, capture_output=True, text=True).stdout.strip()
    return fractions.Fraction(rate)


def read_frames(video, width, height, threshold):
    process = subprocess.Popen(
        ['ffmpeg', '-v', 'error', '-i', video,
         '-vf', f'scale={width}:{height}:flags=area,format=gray',
         '-f', 'rawvideo', '-'],
        stdout=subprocess.PIPE)
    size = width * height
    lut = bytes(1 if value >= threshold else 0 for value in range(256))
    while True:
        frame = process.stdout.read(size)
        if len(frame) < size:
            break
        yield frame.translate(lut)
    if process.wait() != 0:
        sys.exit('ffmpeg failed')


def put_varint(value, out):
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)


def encode_runs(pixels):
    out = bytearray()
    current = 0
    run = 0
    for pixel in pixels:
        if pixel == current:
            run += 1
        else:
            put_varint(run, out)
            current = pixel
            run = 1
    put_varint(run, out)
    return out


def main():
    parser = argparse.ArgumentParser(description='Convert a Bad Apple!! video into badapple.bin')
    parser.add_argument('video')
    parser.add_argument('output')
    parser.add_argument('--width', type=int, default=96)
    parser.add_argument('--height', type=int, default=72)
    parser.add_argument('--threshold', type=int, default=128)
    args = parser.parse_args()

    rate = probe_rate(args.video)
    if rate.numerator > 0xFFFF or rate.denominator > 0xFFFF:
        rate = rate.limit_denominator(1001)

    stream = bytearray()
    previous = bytes(args.width * args.height)
    count = 0
    for frame in read_frames(args.video, args.width, args.height, args.threshold):
        absolute = encode_runs(frame)
        delta = encode_runs(bytes(a ^ b for a, b in zip(frame, previous)))
        if len(delta) < len(absolute):
            stream.append(1)
            stream += delta
        else:
            stream.append(0)
            stream += absolute
        previous = frame
        count += 1

    header = struct.pack('<4sHHHHHHII', MAGIC, VERSION, args.width, args.height,
                         rate.numerator, rate.denominator, 0, count, len(stream))
    with open(args.output, 'wb') as output:
        output.write(header)
        output.write(zlib.compress(bytes(stream), 9))

    print(f'{count} frames, {args.width}x{args.height} at {rate} fps, '
          f'{len(stream)} bytes of runs')


if __name__ == '__main__':
    main()
