#!/usr/bin/env python3
"""Shared facts in the site and documentation: version, builtins, modules, tests.

    python3 packaging/site_facts.py --build build          # check them (a ctest runs this)
    python3 packaging/site_facts.py --build build --write  # bring them up to date

Every build runs the second line (the site_facts target), so shared facts follow the code
without anyone editing it; the test catches a page committed without a build.

The version comes from VERSION, builtins from the catalog in src/, modules from std/
and the tests from `ctest -N` in the build directory. The test count is the one of a
Linux build: Windows leaves out the tests that need a Linux desktop.
"""
import argparse
import importlib.util
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def facts(build, ctest):
    spec = importlib.util.spec_from_file_location("sync_editor_builtins", ROOT / "packaging" / "sync_editor_builtins.py")
    catalog = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(catalog)
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    found = {
        "version": version,
        "release": version,
        "builtins": len(catalog.builtin_names(ROOT)),
        "modules": len(catalog.standard_modules(ROOT)),
        "tests": None,
    }
    if build and sys.platform.startswith("linux"):
        listing = subprocess.run([ctest, "--test-dir", str(build), "-N"], capture_output=True, text=True, check=True).stdout
        total = re.search(r"Total Tests:\s*(\d+)", listing)
        if total:
            found["tests"] = int(total.group(1))
    return found


# What each fact looks like on the page; group 2 is the value.
PATTERNS = {
    "version": [r'(<p class="eyebrow">Skrumi )([\d.]+)( ·)', r'(id="frame-input" type="text" value="Skrumi )([\d.]+)(")'],
    "builtins": [r'(<div><b>)(\d+)(</b><span>встроенных функций)'],
    "modules": [r'(<div><b>)(\d+)(</b><span>модулей библиотеки)', r'(<strong>Стандартная библиотека</strong> — )(\d+)( модулей)'],
    "release": [r'(<footer>Skrumi )([\d.]+)( ·)'],
    "tests": [r'(<div><b>)(\d+)(</b><span>автотестов)', r'(├── tests/\s+<i>)(\d+)( автотестов)',
              r'(<strong>Тесты\.</strong> )(\d+)( проверок)'],
}


DOCUMENT_PATTERNS = {
    "site/builder/index.html": {"version": [
        r'(<span data-runtime-version>)([\d.]+)(</span>)',
    ]},
    "resources/version.svg": {"version": [
        r'(aria-label="version: )([\d.]+)(")',
        r'(<title>version: )([\d.]+)(</title>)',
        r'(<text x="86" y="14">)([\d.]+)(</text>)',
    ]},
    "README.md": {"modules": [r'(Стандартная библиотека из )(\d+)( модулей)']},
    "tests/README.md": {"modules": [r'(всех функций )(\d+)( модулей)',
                                      r'(signatureHelp всех )(\d+)(\nstd-модулей)']},
}


def synchronize(found, write=False):
    stale = []
    for relative, patterns in {"site/index.html": PATTERNS, **DOCUMENT_PATTERNS}.items():
        path = ROOT / relative
        page = path.read_text(encoding="utf-8")
        original = page
        for name, expressions in patterns.items():
            value = found[name]
            if value is None:
                continue
            for pattern in expressions:
                matches = list(re.finditer(pattern, page))
                if not matches:
                    raise ValueError(f"{relative}: missing {name} marker: {pattern}")
                for match in reversed(matches):
                    if match.group(2) != str(value):
                        stale.append(f"{relative}: {name}: says {match.group(2)}, expected {value}")
                        page = page[:match.start(2)] + str(value) + page[match.end(2):]
        if write and page != original:
            path.write_text(page, encoding="utf-8")
    return stale


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", help="build directory, for the number of tests")
    parser.add_argument("--write", action="store_true", help="update site and documentation instead of checking them")
    parser.add_argument("--ctest", default="ctest", help="the ctest to count the tests with")
    args = parser.parse_args()
    try:
        stale = synchronize(facts(args.build, args.ctest), write=args.write)
    except ValueError as error:
        sys.exit(str(error))
    if stale and not args.write:
        sys.exit("Documentation facts are out of date:\n  " + "\n  ".join(stale) +
                 "\nrun: python3 packaging/site_facts.py --build <build dir> --write")
    print("\n".join(stale) if stale else "SITE_FACTS_OK")


if __name__ == "__main__":
    main()
