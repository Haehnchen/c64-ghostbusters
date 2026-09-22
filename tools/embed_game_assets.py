#!/usr/bin/env python3
"""Compile checked-in named asset regions into C++; never reads a reference image."""
import argparse
import hashlib
import json
from pathlib import Path


def text_commands(commands):
    # Text is ASCII glyph input, not screen codes. Controls retain a slot in the
    # timed stream so pauses, cursor moves and key tones keep their ordering.
    result = bytearray()
    for command in commands:
        if isinstance(command, str):
            data = command.encode('ascii')
            if any(byte < 32 or byte > 126 for byte in data):
                raise ValueError('Use explicit text controls outside printable ASCII')
            result.extend(data)
            continue
        if not isinstance(command, dict) or len(command) != 1:
            raise ValueError('Invalid text command')
        kind, value = next(iter(command.items()))
        if kind in ('end', 'newline') and value is True:
            result.append(255 if kind == 'end' else 13)
        elif type(value) is int and kind == 'wait' and 1 <= value <= 255:
            result.extend(bytes(value))
        elif type(value) is int and kind == 'column' and 0 <= value < 64:
            result.append(128 + value)
        elif type(value) is int and kind == 'glyph' and 0 <= value < 256:
            result.append(value)
        else:
            raise ValueError('Invalid text command value')
    return bytes(result)


def checked(path, record):
    # Manifest sizes and hashes cover decoded bytes, not the JSON source text.
    if path.suffix == '.json':
        document = json.loads(path.read_text())
        if document.get('format_version') != 1:
            raise ValueError('Unsupported resource format')
        resource = document['assets'][record['name']]
        if resource['encoding'] == 'hex':
            data = bytes.fromhex(resource['data'])
        elif resource['encoding'] == 'bytes':
            data = bytes(resource['data'])
        elif resource['encoding'] == 'ascii':
            data = resource['data'].encode('ascii')
        elif resource['encoding'] == 'text_commands':
            data = text_commands(resource['data'])
        else:
            raise ValueError('Unsupported resource encoding')
    else:
        data = path.read_bytes()
    if len(data) != record['size'] or hashlib.sha256(data).hexdigest() != record['sha256']:
        raise ValueError(f'Asset size/hash mismatch: {path}')
    return data


def read_font(path):
    # Glyphs are eight top-to-bottom bitmap rows, high bit at the left edge.
    # Source metadata records provenance only and never selects build inputs.
    font = json.loads(path.read_text())
    expected = {'format_version': 1, 'glyph_width': 8, 'glyph_height': 8,
                'glyph_count': 512, 'bit_order': 'msb_left',
                'encoding': 'hex_rows_top_to_bottom'}
    if any(font.get(key) != value for key, value in expected.items()):
        raise ValueError('Unsupported game font format')
    glyphs = font.get('glyphs', [])
    if len(glyphs) != 512 or any(not isinstance(glyph, str) or len(glyph) != 16
                                or any(c not in '0123456789abcdefABCDEF' for c in glyph)
                                for glyph in glyphs):
        raise ValueError('Invalid game font glyphs')
    return bytes.fromhex(''.join(glyphs))


def checked_font(path, record):
    data = read_font(path)
    if len(data) != record['size'] or hashlib.sha256(data).hexdigest() != record['sha256']:
        raise ValueError(f'Font size/hash mismatch: {path}')
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((args.assets / 'manifest.json').read_text())
    if manifest['format_version'] != 1:
        raise ValueError('Unsupported asset manifest version')
    lines = ['// Generated from versioned game assets. Do not edit.',
             '#include "assets/embedded.hpp"', 'namespace ghostbusters::assets {', 'namespace {']
    entries = []
    names = set()
    for i, record in enumerate(manifest['regions']):
        if not record['name'] or record['name'] in names:
            raise ValueError('Asset names must be nonempty and unique')
        names.add(record['name'])
        data = checked(args.assets / record['file'], record)
        if not data:
            raise ValueError('Empty asset region')
        symbol = f'region_{i}'
        lines.append(f'constexpr std::uint8_t {symbol}[] = {{')
        lines.extend(','.join(str(v) for v in data[n:n+32]) + ',' for n in range(0, len(data), 32))
        lines.append('};')
        entries.append(f'{{{json.dumps(record["name"])}, {symbol}}},')
    record = manifest['font']
    font = checked_font(args.assets / record['file'], record)
    lines.append('constexpr std::uint8_t font[] = {')
    lines.extend(','.join(str(v) for v in font[n:n+32]) + ',' for n in range(0, len(font), 32))
    lines.extend(['};', 'const EmbeddedRegion regions[] = {', *entries, '};', '}',
                  'std::span<const EmbeddedRegion> embedded_regions() { return regions; }',
                  'std::span<const std::uint8_t> embedded_font() { return font; }', '}'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    content = '\n'.join(lines) + '\n'
    if not args.output.exists() or args.output.read_text() != content:
        args.output.write_text(content)


if __name__ == '__main__':
    main()
