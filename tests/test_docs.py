"""The documentation describes the language as it is, not as it was.

1. Every ```cpp block of Skrumi code in README.md, DOCUMENTATION.md and docs/*.md
   passes `skrumi check` (blocks with #include are C++ embedding code and skipped).
2. DOCUMENTATION.md mentions every builtin function and every function a std module
   exports, and has a section for every std module.
"""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
root = Path(sys.argv[2]).resolve()

spec = importlib.util.spec_from_file_location("sync_editor_builtins", root / "packaging/sync_editor_builtins.py")
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

documents = [root / "README.md", root / "DOCUMENTATION.md", *sorted((root / "docs").glob("*.md"))]
failures = []
checked = 0
executed = 0
with tempfile.TemporaryDirectory(prefix="skrumi-docs-test-") as directory:
    env = dict(os.environ, SKRUMI_HOME=str(Path(directory) / "absent"))
    for document in documents:
        text = document.read_text(encoding="utf-8")
        for match in re.finditer(r"```cpp\n(.*?)```", text, re.S):
            code = match.group(1)
            if "#include" in code:
                continue
            line = text.count("\n", 0, match.start()) + 2
            source = Path(directory) / "block.skm"
            source.write_text(code, encoding="utf-8")
            result = subprocess.run([str(binary), "check", str(source)], cwd=directory, env=env,
                                    capture_output=True, encoding="utf-8", timeout=30)
            checked += 1
            if text[:match.start()].rstrip().endswith("<!-- skrumi-run -->") and result.returncode == 0:
                actual = subprocess.run([str(binary), str(source)], cwd=directory, env=env,
                                        capture_output=True, encoding="utf-8", timeout=30)
                executed += 1
                if actual.returncode != 0:
                    failures.append(f"{document.relative_to(root)}:{line} runtime contract failed\n{actual.stdout}{actual.stderr}")
            if result.returncode != 0 or "no problems found" not in result.stdout:
                failures.append(f"{document.relative_to(root)}:{line}\n{code}\n{result.stdout}{result.stderr}")

reference = (root / "DOCUMENTATION.md").read_text(encoding="utf-8")
names = set(catalog.builtin_names(root))
modules = catalog.standard_modules(root)
for module in modules:
    if f"using {module.stem};" not in reference:
        failures.append(f"DOCUMENTATION.md has no section for `using {module.stem};`")
    names.update(name for _, name, _ in catalog.module_exports(module))
missing = sorted(name for name in names if not re.search(r"\b" + re.escape(name) + r"\(", reference))
if missing:
    failures.append("DOCUMENTATION.md does not describe: " + ", ".join(missing))

if failures:
    print("\n\n".join(failures))
    sys.exit(1)
print(f"DOCS_OK: {checked} code blocks checked, {executed} contracts executed, {len(names)} functions documented")
