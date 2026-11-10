"""Keep retired brand identifiers out of active code, paths and release inputs."""
from pathlib import Path
import re
import subprocess
import os
import shutil
import sys

root = Path(__file__).resolve().parents[1]
retired = re.compile(r'fox[ _-]?lang|\.fox', re.I)
allowed = {
    'tests/test_brand_audit.py': 'The audit pattern and its documented exceptions.',
    'tests/test_rebranding.py': 'Negative CLI/configuration compatibility regression cases.',
    'tests/test_module_resolution.py': 'Negative fallback and collision regression fixtures.',
    'tests/test_android_packaging.py': 'Negative source staging regression fixtures.',
}
if (root / '.git').exists() and shutil.which('git') and '--source-tree' not in sys.argv:
    paths = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard'], cwd=root, text=True).splitlines()
else:
    # Docker/source archives intentionally omit Git metadata and the Git executable.
    # Exclude only generated/dependency trees; keep all product sources and .github.
    paths = []
    for directory, folders, files in os.walk(root):
        folders[:] = [name for name in folders
                      if name not in {'.git', 'node_modules', '__pycache__', '.gradle', '.cxx', 'target', 'build', 'dist'}
                      and not name.startswith(('build-', 'dist-'))
                      and not (Path(directory) == root and name == 'web')]
        paths.extend((Path(directory) / name).relative_to(root).as_posix() for name in files)
for name in sorted(set(paths)):
    path = root / name
    if not path.is_file(): continue
    assert not retired.search(name), f'Retired path: {name}'
    try: text = path.read_text(encoding='utf-8')
    except UnicodeError: continue
    matches = list(retired.finditer(text))
    if matches:
        assert name in allowed, f'Unexplained retired identifier: {name}'
        print(f'{name}: {len(matches)} matches — {allowed[name]}')
print('BRAND_AUDIT_OK')

# Association metadata is executable configuration, not just display branding.
import json
import tomllib
import xml.etree.ElementTree as ET
vscode = json.loads((root / 'editors/vscode/package.json').read_text(encoding='utf-8'))
assert vscode['name'] == 'skrumi-language'
assert [(x['id'], x['extensions']) for x in vscode['contributes']['languages']] == [('skrumi', ['.skm'])]
zed = tomllib.loads((root / 'editors/zed/languages/skrumi/config.toml').read_text(encoding='utf-8'))
assert zed['name'] == 'Skrumi' and zed['path_suffixes'] == ['skm']
kate = ET.fromstring((root / 'editors/kate/skrumi.xml').read_text(encoding='utf-8'))
assert kate.attrib['extensions'] == '*.skm'
