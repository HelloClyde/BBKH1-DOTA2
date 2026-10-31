"""Convert the installed map overview using its original world-coordinate bounds."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

from PIL import Image
from source2_vpk import VpkArchive

ROOT = Path(__file__).resolve().parents[1]
GAME = Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')


def main():
    archive = VpkArchive(GAME / 'pak01_dir.vpk')
    config_entry = 'resource/overviews/dota.txt'
    material_entry = 'materials/overviews/dota.vmat_c'
    config = archive.read(config_entry).decode('utf8')
    folder = ROOT / 'private/source2/exports/overview'
    folder.mkdir(parents=True, exist_ok=True)
    material = folder / 'dota.vmat'
    subprocess.run([str(ROOT.parent / '.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe'),
                    '-i', str(archive.path), '-f', material_entry, '-d', '-o', str(material)], check=True)
    compiled = re.search(r'"g_tColor"\s+"([^"]+)"', material.read_text())
    if not compiled:
        raise ValueError('Original overview material has no colour texture')
    texture_entry = compiled[1] + '_c'
    original = folder / 'dota.png'
    image = Image.open(original).convert('RGB')
    def value(name):
        return float(re.search(r'\b' + name + r'\s+(-?[0-9.]+)', config)[1])
    # Source's overview scale uses a 1024-pixel reference; the exported mip may
    # have a different resolution. Arena bounds are Source X/Y = [-8192,8192].
    world_span = value('scale') * 1024
    left = (-8192 - value('pos_x')) / world_span * image.width
    top = (value('pos_y') - 8192) / world_span * image.height
    right = (8192 - value('pos_x')) / world_span * image.width
    bottom = (value('pos_y') + 8192) / world_span * image.height
    adapted = image.resize((72, 72), Image.Resampling.LANCZOS, box=(left, top, right, bottom))
    adapted.save(folder / 'h1-overview.png')
    pixels = [((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) for r, g, b in adapted.getdata()]
    output = ROOT / 'private/generated/dota_overview.h'
    output.write_text('/* Original Steam overview; Source X right, Source Y up. */\n'
                      'static const uint16_t dota_overview[72*72]={\n' +
                      ''.join(','.join(str(p) for p in pixels[i:i+24]) + ',\n'
                              for i in range(0, len(pixels), 24)) + '};\n', encoding='utf8')
    receipt = {'config': archive.identity(config_entry), 'material': archive.identity(material_entry),
               'texture': archive.identity(texture_entry), 'world_bounds': [-8192, -8192, 8192, 8192],
               'exported_size': image.size, 'crop_pixels': [left, top, right, bottom],
               'output_size': [72, 72], 'output_sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
               'mapping': 'game x=2048+SourceX/4; game y=2048-SourceY/4'}
    (ROOT / 'private/source2/reports/map-overview.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'output': str(output), 'sha256': receipt['output_sha256']}))


if __name__ == '__main__':
    main()
