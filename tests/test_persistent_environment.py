"""Public environment plans in an isolated user home; Windows registry snapshot fidelity."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid

binary = str(Path(sys.argv[1]).resolve())
q = lambda value: json.dumps(str(value), ensure_ascii=False)
with tempfile.TemporaryDirectory(prefix='skrumi environment ') as temp:
    root = Path(temp)
    env = dict(os.environ, HOME=str(root), SHELL='/bin/sh', XDG_DATA_HOME='relative-invalid')
    source = root/'test.skm'
    def run(code, ok=True):
        source.write_text('using env;\n' + code, encoding='utf-8')
        p = subprocess.run([binary, str(source)], cwd=root, env=env, capture_output=True, text=True, encoding='utf-8', timeout=15)
        assert (p.returncode == 0) == ok, (p.stdout, p.stderr, code)
        return p
    if os.name == 'nt':
        variable = 'SKRUMI_TEST_'+uuid.uuid4().hex
        run(f'''array before = env_user_snapshot({q(variable)});
            if (before[0]) {{ fail("test key collision"); }}
            try {{
                env_user_compare_exchange({q(variable)}, before, [true, true, "Юникод %TEMP%"]);
                array after = env_user_snapshot({q(variable)});
                if (after[2] != "Юникод %TEMP%" || !after[1]) {{ fail("registry mismatch"); }}
                bool conflict = false;
                try {{ env_user_compare_exchange({q(variable)}, before, before); }} catch (string e) {{ conflict = true; }}
                if (!conflict) {{ fail("snapshot conflict ignored"); }}
            }} finally {{ env_user_compare_exchange({q(variable)}, env_user_snapshot({q(variable)}), before); }}''')
        if os.environ.get('CI') == 'true':
            target = root/'PATH Кириллица'
            target.mkdir()
            run(f'''array plan = user_path_plan({q(target)}, true);
                try {{
                    user_path_apply(plan, false);
                    array actual = env_user_snapshot("Path");
                    array expected = plan[3];
                    if (json_value(actual) != json_value(expected) || !str_starts_with(actual[2], {q(target)})) {{ fail("PATH write mismatch"); }}
                    array again = user_path_plan({q(target)}, true);
                    if (json_value(again[2]) != json_value(again[3])) {{ fail("duplicate PATH plan"); }}
                    user_path_apply(user_path_removal(again, {q(target)}), false);
                    if (json_value(env_user_snapshot("Path")) != json_value(actual)) {{ fail("pre-existing PATH entry removed"); }}
                }} finally {{ user_path_apply(plan, true); }}''')
    else:
        profile = root/'.profile'
        original = '# Профиль пользователя — original\nexport USER_SETTING="keep"\n'
        profile.write_text(original, encoding='utf-8'); profile.chmod(0o640)
        target = root/"путь ' literal $(touch INJECTED) [x]"
        target.mkdir()
        assert run('print(os_user_dir("data"));').stdout.strip() == str(root/'.local/share')
        code = f'array plan = user_path_plan({q(target)}, true);'
        run(code + 'fs_write_atomic("plan.json", json_value(plan)); user_path_apply(plan, false); user_path_apply(plan, false);')
        installed = profile.read_text()
        assert installed.startswith(original) and installed.count('# Skrumi PATH ') == 2
        assert profile.stat().st_mode & 0o777 == 0o640
        out = subprocess.check_output(['/bin/sh', '-c', '. "$1"; printf "%s" "$PATH"', 'sh', str(profile)], env=env, text=True)
        assert out.split(':').count(str(target)) == 1 and not (root/'INJECTED').exists()
        # Existing marker causes an exact no-op plan; no duplicate shell block.
        run(code + f'user_path_apply(plan, false); user_path_apply(user_path_removal(plan, {q(target)}), false);')
        assert profile.read_text() == installed
        run('array p = json_decode(read_file("plan.json")); user_path_apply(p, true); user_path_apply(p, true);')
        assert profile.read_text() == original
        # Refuse a stale rollback, then remove only our block preserving later edits.
        run('array p = json_decode(read_file("plan.json")); user_path_apply(p, false);')
        profile.write_text(profile.read_text()+'# later user edit\n')
        run('array p = json_decode(read_file("plan.json")); user_path_apply(p, true);', False)
        run(f'array p = json_decode(read_file("plan.json")); user_path_apply(user_path_removal(p, {q(target)}), false);')
        assert profile.read_text() == original+'# later user edit\n'
        unrelated = root/'user-file'
        unrelated.write_text('preserve', encoding='utf-8')
        run(f'user_path_apply(["file", {q(unrelated)}, [true, "preserve", "644"], [true, "destroy", "644"]], false);', False)
        assert unrelated.read_text() == 'preserve'
        # Fish has a native configuration fragment, not a POSIX profile block.
        env['SHELL'] = '/usr/bin/fish'
        env['XDG_CONFIG_HOME'] = str(root/'custom config')
        fragment = root/'custom config/fish/conf.d/skrumi-path.fish'
        fish_target = root/"fish '\\\\ path $(touch INJECTED) [x]"
        fish_target.mkdir()
        fish_code = f'array p = user_path_plan({q(fish_target)}, true);'
        run(fish_code + 'fs_write_atomic("fish-plan.json", json_value(p)); user_path_apply(p, false); user_path_apply(p, false);')
        installed_fish = fragment.read_text()
        assert installed_fish.count('# Skrumi PATH ') == 2
        assert profile.read_text() == original+'# later user edit\n'
        fish = shutil.which('fish')
        if fish:
            # Load the fragment twice to verify quoting, duplicate suppression and
            # no command substitution, using an actual Fish process.
            script = r'source "$argv[1]"; source "$argv[1]"; printf "%s\n" $PATH'
            result = subprocess.run([fish, '--no-config', '-c', script, str(fragment)],
                                    env=env, cwd=root, capture_output=True, text=True, timeout=15)
            assert result.returncode == 0, result.stderr
            assert result.stdout.splitlines().count(str(fish_target)) == 1, result.stdout
            startup = subprocess.run([fish, '-c', r'printf "%s\\n" $PATH'],
                                     env=env, cwd=root, capture_output=True, text=True, timeout=15)
            assert startup.returncode == 0 and startup.stdout.splitlines().count(str(fish_target)) == 1, startup
            assert not (root/'INJECTED').exists()
        else:
            print('Fish executable unavailable: native shell execution skipped')
        run('array p = json_decode(read_file("fish-plan.json")); user_path_apply(p, true);')
        assert not fragment.exists()
        fragment.write_text('# existing user config\n')
        run(fish_code + 'fs_write_atomic("fish-plan.json", json_value(p)); user_path_apply(p, false);')
        fragment.write_text(fragment.read_text()+'# later edit\n')
        run('array p = json_decode(read_file("fish-plan.json")); user_path_apply(p, true);', False)
        # Removal uses the installed backend even after changing login shell.
        env['SHELL'] = '/bin/sh'
        run(f'array p = json_decode(read_file("fish-plan.json")); user_path_apply(user_path_removal(p, {q(fish_target)}), false);')
        assert fragment.read_text() == '# existing user config\n# later edit\n'
        run('env_user_snapshot("Path");', False)
print('PERSISTENT_ENVIRONMENT_OK')
