"""Product embedding needs only named files, sizes and hashes, not provenance."""

import json
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('embed_game_assets', root / 'tools/embed_game_assets.py')
    embedding = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(embedding)
    assert embedding.text_commands(['ABC', {'newline': True}, {'column': 5},
                                    {'wait': 2}, {'glyph': 1}, {'end': True}]) == bytes(
                                        [65, 66, 67, 13, 133, 0, 0, 1, 255])
    for invalid in [[{'column': 64}], [{'wait': -1}], [{'end': 1}],
                    [{'glyph': 256}], ['bad\ncontrol'], [{'unknown': True}]]:
        try:
            embedding.text_commands(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError(f'Invalid text command accepted: {invalid}')
    original = json.loads((root / 'assets/manifest.json').read_text())
    with tempfile.TemporaryDirectory(prefix='ghostbusters-embed-test-') as directory:
        workspace = Path(directory)
        assets = workspace / 'assets'
        shutil.copytree(root / 'assets', workspace / 'assets')
        command = [sys.executable, str(root / 'tools/embed_game_assets.py'),
                   '--assets', str(assets)]
        baseline = workspace / 'baseline.cpp'
        subprocess.run(command + ['--output', str(baseline)], check=True)
        manifest = {
            'format_version': original['format_version'],
            'regions': [{key: record[key] for key in ('name', 'file', 'size', 'sha256')}
                        for record in original['regions']],
            'font': original['font'],
        }
        (assets / 'manifest.json').write_text(json.dumps(manifest))
        output = workspace / 'game_assets.cpp'
        subprocess.run(command + ['--output', str(output)], check=True)
        native = output.read_bytes()
        assert native == baseline.read_bytes()
        assert b'legacy_address' not in native
        assert b'ReferenceRegion' not in native

        font_path = assets / manifest['font']['file']
        font = json.loads(font_path.read_text())
        font.pop('source', None)
        font_path.write_text(json.dumps(font))
        subprocess.run(command + ['--output', str(output)], check=True)
        assert output.read_bytes() == native
        unchanged_time = output.stat().st_mtime_ns
        subprocess.run(command + ['--output', str(output)], check=True)
        assert output.stat().st_mtime_ns == unchanged_time

        for field, value in [('glyph_width', 9), ('bit_order', 'lsb_left'),
                             ('glyphs', font['glyphs'][:-1]),
                             ('glyphs', [' ' * 16] + font['glyphs'][1:]),
                             ('glyphs', ['00' * 8] + font['glyphs'][1:])]:
            invalid = dict(font, **{field: value})
            font_path.write_text(json.dumps(invalid))
            failed = subprocess.run(command + ['--output', str(output)], capture_output=True)
            assert failed.returncode != 0, f'Invalid font accepted: {field}'
            assert output.read_bytes() == native

    print('Asset embedding uses only product manifest data')


if __name__ == '__main__':
    main()
