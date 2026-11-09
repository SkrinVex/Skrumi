"""New syntax, error recovery and project operations through the actual CLI."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='skrumi-update-') as folder:
    root = Path(folder)

    def run(*args, expected=0):
        p = subprocess.run([binary, *args], cwd=root, capture_output=True, text=True, encoding='utf-8', timeout=30)
        out = p.stdout + p.stderr
        assert p.returncode == expected, (args, p.returncode, out)
        return out

    run('init', '.')
    originals = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
    run('init', '.', expected=1)
    assert all(p.read_bytes() == data for p, data in originals.items())
    assert 'Hello, Skrumi!' in run('run', '.')
    run('check', '--project')
    run('test')
    run('build', '.')
    run('build', '.', expected=1)
    run('build', '.', '--force')
    assert all(p.read_bytes() == data for p, data in originals.items())
    run('build', 'main.skm', '-o', 'main.skm', '--force', expected=1)
    assert (root / 'main.skm').read_bytes() == originals[root / 'main.skm']

    source = root / 'probe.skm'
    source.write_text('\ufeff/* with BOM */ assert_equal(0xAB & 0b1111, 11);', encoding='utf-8')
    run('probe.skm')
    run('check', 'probe.skm')
    # Compilation errors caught by Skrumi must not consume/mutate the function AST:
    # the same invalid assignment must fail cleanly on its second invocation too.
    source.write_text('array make() { return [1]; }\nvoid invalid() { make()[0]++; }\n'
                      'int caught = 0;\nfor (int i=0; i<2; i++) { try { invalid(); } catch (e) { caught++; } }\n'
                      'assert_equal(caught, 2);', encoding='utf-8')
    run('probe.skm')
    for text, hint in [
        ('int n = 0x;', 'hex'),
        ('int n = 0b102;', 'binary'),
        ('int n = 1__0;', 'separator'),
        ('float n = 1e+;', 'exponent'),
        ('/* unterminated', 'comment'),
        ('int n=0; ++n;', 'postfix'),
        ('int n = 1 ? 2 : 3;', 'bool'),
        ('int n = true ? 2 : "x";', 'incompatible'),
        ('float n = ~1.0;', 'requires int'),
        ('array make() { return [1]; } make()[0]++;', 'assignable'),
    ]:
        source.write_text(text, encoding='utf-8')
        output = run('check', 'probe.skm', expected=1)
        assert hint.lower() in output.lower(), (text, output)
    source.write_text('int main() {\nprint(1)\nreturn 0;\n}\nmain();', encoding='utf-8')
    output = run('check', 'probe.skm', expected=1)
    assert output.count('error:') == 1 and 'Undefined' not in output, output
    source.write_text('float area(float size) { return size; }', encoding='utf-8')
    assert 'SKR2001' in run('check', 'probe.skm')
    run('check', '--deny-warnings', 'probe.skm', expected=1)
    source.write_text('assert_close(0.1 + 0.2, 0.3, 1e-12);', encoding='utf-8')
    run('test', 'probe.skm')
    source.write_text('/* { \n } \n */\nif (true) {\nprint(1);\n}', encoding='utf-8')
    run('fmt', 'probe.skm')
    once = source.read_bytes()
    run('fmt', 'probe.skm')
    assert once == source.read_bytes()
    run('check', 'probe.skm')
print('GAME_UPDATE_CLI_OK')
