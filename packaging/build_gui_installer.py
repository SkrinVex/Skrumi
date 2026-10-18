"""Build an offline standalone Skrumi application. No installation policy lives here."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib

ROOT = Path(__file__).resolve().parents[1]

def brand_png(svg):
    # Rasterize the repository's polygon mark verbatim. Fail if its geometry changes
    # to unsupported SVG commands; never silently substitute a different logo.
    node = ET.fromstring(svg).find('{http://www.w3.org/2000/svg}path')
    if node is None or node.get('transform') != 'translate(0 -5)':
        raise ValueError('Unsupported brand SVG transform')
    data = node.get('d')
    if not re.fullmatch(r'M[\d\s]+(?:Z)', data):
        raise ValueError('Unsupported brand SVG geometry')
    values = list(map(int, re.findall(r'\d+', data)))
    points = [(values[i], values[i+1]-5) for i in range(0, len(values), 2)]
    color = bytes.fromhex(node.get('fill').removeprefix('#')) + b'\xff'
    raw = bytearray()
    for y in range(128):
        raw.append(0)
        for x in range(128):
            inside = False
            for i, (ax, ay) in enumerate(points):
                bx, by = points[(i+1) % len(points)]
                if (ay > y+.5) != (by > y+.5) and x+.5 < (bx-ax)*(y+.5-ay)/(by-ay)+ax:
                    inside = not inside
            raw.extend(color if inside else b'\0\0\0\0')
    def chunk(kind, value):
        return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind+value))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 128,128,8,6,0,0,0)) +
            chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

def prepare_editors(resources, version):
    # Editor resources are installed only when selected; no network is needed.
    editor_resources = resources/'editors'; editor_resources.mkdir()
    editor_records = []
    vscode = editor_resources/'vscode'; vscode.mkdir()
    # Explicit runtime files: never embed local build caches, tests or credentials.
    vscode_files = ['package.json', 'language-configuration.json', 'README.md',
                    'syntaxes/skrumi.tmLanguage.json', 'client/extension.js',
                    'client/run.js', 'client/debug.js', 'LICENSE']
    for name in vscode_files:
        source = ROOT/'LICENSE' if name == 'LICENSE' else ROOT/'editors/vscode'/name
        destination = vscode/name; destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        editor_records.append([f'vscode/skrinvex.skrumi-language-{version}/{name}', f'vscode/{name}'])
    (vscode/'installation.json').write_text('[]', encoding='utf-8')
    editor_records.append([f'vscode/skrinvex.skrumi-language-{version}/installation.json', 'vscode/installation.json'])
    kate = editor_resources/'kate'; kate.mkdir()
    for name, source in [('syntax', 'skrumi.xml'), ('lsp.json', 'settings.json'), ('dap.json', 'dap.json')]:
        shutil.copyfile(ROOT/'editors/kate'/source, kate/name)
        editor_records.append(['kate/'+name.removesuffix('.json'), 'kate/'+name])
    for action in ['run', 'check', 'build']:
        shutil.copyfile(ROOT/f'editors/kate/externaltools/skrumi-{action}.ini', kate/action)
        editor_records.append(['kate/'+action, 'kate/'+action])
    (editor_resources/'manifest.json').write_text(json.dumps(editor_records), encoding='utf-8')

def build(binary, lsp, output):
    platform = 'windows' if sys.platform == 'win32' else 'linux'
    suffix = '.exe' if platform == 'windows' else ''
    version = (ROOT/'VERSION').read_text().strip()
    result = subprocess.run([str(binary), '--version'], capture_output=True, text=True, check=True)
    if result.stdout.strip() != 'Skrumi ' + version:
        raise ValueError('Build runtime VERSION mismatch')
    with tempfile.TemporaryDirectory(prefix='skrumi-setup-build-') as temporary:
        resources = Path(temporary)/'full'
        resources.mkdir()
        probe = resources/'probe.skm'
        probe.write_text('print(os_arch());', encoding='utf-8')
        arch = subprocess.check_output([str(binary), str(probe)], text=True).strip()
        probe.unlink()
        if arch != 'x86_64':
            raise ValueError('GUI installer packaging currently supports verified x86_64 builds only')
        lsp_version = subprocess.check_output([str(lsp), '--version'], text=True).strip()
        if lsp_version != 'skrumi-lsp ' + version:
            raise ValueError('Language server VERSION mismatch')
        payload = resources/'payload'; payload.mkdir()
        for name, source in [('skrumi',binary), ('skrumi-lsp',lsp)]:
            shutil.copyfile(source, payload/(name+suffix))
        (payload/'std').mkdir()
        for module in sorted((ROOT/'std').glob('*.skm')):
            shutil.copyfile(module, payload/'std'/module.name)
        prepare_editors(resources, version)
        def describe(folder, records, mode):
            (folder/'manifest.json').write_text(json.dumps([version,platform,arch,records]), encoding='utf-8')
            (folder/'installer-mode').write_text(mode, encoding='utf-8')
            (folder/'branding').mkdir()
            shutil.copyfile(ROOT/'installer/assets/DejaVuSans.ttf', folder/'branding/font.ttf')
            shutil.copyfile(ROOT/'installer/assets/FONT-LICENSE.txt', folder/'branding/FONT-LICENSE.txt')
            (folder/'branding/logo.png').write_bytes(brand_png((ROOT/'resources/branding/skrumi-mark.svg').read_text()))
        if platform == 'windows':
            maintenance = Path(temporary)/'maintenance'
            maintenance.mkdir()
            describe(maintenance, [], 'maintenance')
            subprocess.run([str(binary), 'build', str(ROOT/'installer/main.skm'), '--resources', str(maintenance),
                            '--no-console', '-o', str(payload/'@maintenance.exe')], check=True)
            digest = hashlib.sha256((payload/'@maintenance.exe').read_bytes()).hexdigest()
            (payload/'@maintenance.json').write_text(json.dumps(['Skrumi maintenance v1', version, digest]), encoding='utf-8')
        records = [[file.relative_to(payload).as_posix(), hashlib.sha256(file.read_bytes()).hexdigest(),
                    '600' if file.name == '@maintenance.json' else '644' if file.parent.name == 'std' or 'editors' in file.relative_to(payload).parts else '755']
                   for file in sorted(payload.rglob('*')) if file.is_file()]
        describe(resources, records, 'full')
        subprocess.run([str(binary), 'build', str(ROOT/'installer/main.skm'), '--resources', str(resources),
                        '--no-console', '-o', str(output)], check=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--lsp', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.runtime.resolve(), args.lsp.resolve(), args.output.resolve())
