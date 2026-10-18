"""Exercise editor changes in real standalone install/recovery transactions."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
binary = Path(sys.argv[1]).resolve()
version = (repo/'VERSION').read_text().strip()
spec = importlib.util.spec_from_file_location('builder', repo/'packaging/build_gui_installer.py')
builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
q = lambda v: json.dumps(str(v), ensure_ascii=False)
with tempfile.TemporaryDirectory(prefix='skrumi editors ') as temporary:
    base = Path(temporary)
    user = base/'user'; user.mkdir()
    target = base/'custom runtime'
    resources = base/'resources'; payload = resources/'payload'; payload.mkdir(parents=True)
    suffix = '.exe' if os.name == 'nt' else ''
    runtime = payload/('skrumi'+suffix); runtime.write_bytes(binary.read_bytes())
    builder.prepare_editors(resources, version)
    records = [[p.relative_to(payload).as_posix(), hashlib.sha256(p.read_bytes()).hexdigest(), '755' if p == runtime else '644'] for p in payload.rglob('*') if p.is_file()]
    (resources/'manifest.json').write_text(json.dumps([version, 'windows' if os.name == 'nt' else 'linux', 'x86_64', records]))
    source = base/'driver.skm'
    # Override user directories in the script to test native Windows APIs without
    # touching the CI account's editor settings. Production always uses Known Folders.
    source.write_text(f'''include({q(repo/'installer/transaction.skm')});
string os_user_dir(string kind) {{ return {q(user)}; }}
array args = os_args();
try {{
    if (args[0] == "recover") {{ tx_recover(args[1]); }}
    else {{
        if (args[0] != "none" && args[0] != "uninstall") {{ editor_selected = ["vscode", "kate"]; }}
        if (args[0] == "no-lsp") {{ tx_include_lsp = false; }}
        tx_begin(args[1], args[0] == "uninstall", false);
        if (args[0] == "partial") {{
            while (tx_phase != "verifying") {{ tx_step(); }}
            editor_apply([editor_changes[0]], false);
            tx_release(); exit(0);
        }}
        while (tx_phase != "integrated") {{ tx_step(); wait(1); }}
        if (args[0] == "crash") {{ tx_release(); exit(0); }}
        if (args[0] == "cancel") {{ tx_cancel(); }}
        else {{ while (!tx_step()) {{ wait(1); }} }}
    }}
}} catch (string error) {{
    print(error); try {{ tx_cancel(); }} catch (string e) {{ print(e); }} tx_release(); exit(1);
}}
''', encoding='utf-8')
    driver = base/('setup'+suffix)
    def run(*args, ok=True):
        p = subprocess.run([str(x) for x in args], capture_output=True, text=True, encoding='utf-8', timeout=60)
        assert (p.returncode == 0) == ok, (args, p.stdout, p.stderr)
        return p
    run(binary, 'build', source, '--resources', resources, '-o', driver)
    lsp = user/'kate/lspclient/settings.json'; lsp.parent.mkdir(parents=True)
    original = '{"servers":{"python":{"command":["pylsp"]}},"custom":123}\n'
    lsp.write_text(original)
    extension = user/f'.vscode/extensions/skrinvex.skrumi-language-{version}'
    syntax = user/'org.kde.syntax-highlighting/syntax/skrumi.xml'
    def installed():
        settings = json.loads(lsp.read_text())
        assert settings['servers']['python']['command'] == ['pylsp']
        assert settings['servers']['skrumi']['command'] == [str(target/('skrumi-lsp'+suffix))]
        assert json.loads((extension/'installation.json').read_text()) == [str(target)]
        assert (extension/'client/extension.js').is_file() and syntax.is_file()
    def restored():
        assert lsp.read_text() == original
        assert not syntax.exists() and not (extension/'package.json').exists()
    run(driver, 'none', target); restored()
    assert not (target/'editors/zed/extension.toml').exists()
    run(driver, 'cancel', target); restored()
    run(driver, 'partial', target)
    run(driver, 'recover', target); restored()
    run(driver, 'no-lsp', target)
    assert 'skrumi' not in json.loads(lsp.read_text())['servers']
    run(driver, 'none', target); restored()
    run(driver, 'crash', target); installed()
    run(driver, 'recover', target); restored()
    run(driver, 'install', target); installed()
    run(driver, 'install', target); installed()  # idempotent update
    settings = json.loads(lsp.read_text()); settings['other'] = 'keep my edit'
    lsp.write_text(json.dumps(settings))
    run(driver, 'install', target); installed()
    assert json.loads(lsp.read_text())['other'] == 'keep my edit'
    run(driver, 'none', target)  # deselect integrations during update
    settings = json.loads(lsp.read_text())
    assert 'skrumi' not in settings['servers'] and settings['other'] == 'keep my edit'
    assert not syntax.exists()
    run(driver, 'install', target); installed()
    syntax.write_text('user-modified syntax')
    run(driver, 'install', target, ok=False)
    assert syntax.read_text() == 'user-modified syntax'
    run(driver, 'uninstall', target)
    assert syntax.read_text() == 'user-modified syntax'
    assert 'skrumi' not in json.loads(lsp.read_text())['servers']
    # A foreign pre-existing Skrumi configuration is never silently overwritten.
    syntax.unlink()
    settings = json.loads(lsp.read_text()); settings['servers']['skrumi'] = {'command':['my-lsp']}
    lsp.write_text(json.dumps(settings)); before = lsp.read_bytes()
    run(driver, 'install', target, ok=False)
    assert lsp.read_bytes() == before and not (extension/'package.json').exists()
    # Detection uses executable candidates rather than stale settings directories.
    detect = base/'detect.skm'; fake = base/'bin'; fake.mkdir()
    candidate = fake/('code.cmd' if os.name == 'nt' else 'code')
    candidate.write_text('unused'); candidate.chmod(0o755)
    detect.write_text(f'''include({q(repo/'installer/transaction.skm')});
if (!env_set("PATH", {q(fake)})) {{ fail("env_set"); }}
if (editor_find("code") != {q(candidate)}) {{ fail("PATH detection"); }}
print("DETECTION_OK");
''')
    run(binary, detect)
print('INSTALLER_EDITORS_OK')
