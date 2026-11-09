"""References, renaming and formatting through the real skrumi-lsp executable.

A program of two files: main.skm includes util.skm. Only main.skm is open, so the
server reads util.skm from disk; then util.skm is opened and must still be found.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())

UTIL = '''int add(int left, int right) {
    return left + right;
}
'''
MAIN = '''include("util.skm");

int total = add(1, 2);
int twice(int value) {
    return add(value, value);
}
print(total, twice(total));
'''


class Server:
    def __init__(self):
        self.process = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.next_id = 1

    def send(self, message):
        body = json.dumps(message).encode('utf-8')
        self.process.stdin.write(b'Content-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body)
        self.process.stdin.flush()

    def read(self):
        length = 0
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise AssertionError('server closed the connection')
            line = line.strip()
            if not line:
                break
            if line.lower().startswith(b'content-length:'):
                length = int(line.split(b':')[1])
        return json.loads(self.process.stdout.read(length))

    def request(self, method, params):
        ident = self.next_id
        self.next_id += 1
        self.send({'jsonrpc': '2.0', 'id': ident, 'method': method, 'params': params})
        while True:
            message = self.read()
            if message.get('id') == ident:
                return message

    def notify(self, method, params):
        self.send({'jsonrpc': '2.0', 'method': method, 'params': params})


def uri(path):
    return path.resolve().as_uri()


def at(text, needle, occurrence=0):
    index = -1
    for _ in range(occurrence + 1):
        index = text.index(needle, index + 1)
    line = text.count('\n', 0, index)
    return {'line': line, 'character': index - (text.rfind('\n', 0, index) + 1)}


def spots(locations):
    return sorted((Path(l['uri']).name if 'uri' in l else '', l['range']['start']['line'], l['range']['start']['character'])
                  for l in locations)


with tempfile.TemporaryDirectory(prefix='skrumi-lsp-') as directory:
    root = Path(directory)
    (root / 'util.skm').write_text(UTIL, encoding='utf-8')
    (root / 'main.skm').write_text(MAIN, encoding='utf-8')
    main_uri = uri(root / 'main.skm')
    server = Server()
    init = server.request('initialize', {'rootUri': uri(root), 'capabilities': {}})
    caps = init['result']['capabilities']
    assert caps['referencesProvider'] and caps['renameProvider']['prepareProvider'] and caps['documentFormattingProvider'], caps
    server.notify('initialized', {})
    server.notify('textDocument/didOpen', {'textDocument': {'uri': main_uri, 'languageId': 'skrumi', 'version': 1, 'text': MAIN}})

    # add() is declared in util.skm, which is not open.
    refs = server.request('textDocument/references', {'textDocument': {'uri': main_uri}, 'position': at(MAIN, 'add'),
                                                       'context': {'includeDeclaration': True}})['result']
    assert spots(refs) == [('main.skm', 2, 12), ('main.skm', 4, 11), ('util.skm', 0, 4)], spots(refs)
    no_decl = server.request('textDocument/references', {'textDocument': {'uri': main_uri}, 'position': at(MAIN, 'add'),
                                                          'context': {'includeDeclaration': False}})['result']
    assert ('util.skm', 0, 4) not in spots(no_decl), spots(no_decl)

    prepared = server.request('textDocument/prepareRename', {'textDocument': {'uri': main_uri}, 'position': at(MAIN, 'add', 1)})
    assert prepared['result']['placeholder'] == 'add', prepared
    edit = server.request('textDocument/rename', {'textDocument': {'uri': main_uri}, 'position': at(MAIN, 'add'), 'newName': 'sum'})
    changes = edit['result']['changes']
    assert sorted(Path(u).name for u in changes) == ['main.skm', 'util.skm'], changes
    assert all(e['newText'] == 'sum' for edits in changes.values() for e in edits)
    assert sum(len(edits) for edits in changes.values()) == 3, changes

    # A parameter renames with its declaration, and only inside its function.
    edit = server.request('textDocument/rename', {'textDocument': {'uri': main_uri}, 'position': at(MAIN, 'value', 1), 'newName': 'n'})
    edits = edit['result']['changes'][main_uri]
    assert sorted((e['range']['start']['line'], e['range']['start']['character']) for e in edits) == [(3, 14), (4, 15), (4, 22)], edits

    # Builtins, keywords and bad names are refused.
    refused = server.request('textDocument/prepareRename', {'textDocument': {'uri': main_uri}, 'position': at(MAIN, 'print')})
    assert 'error' in refused, refused
    bad = server.request('textDocument/rename', {'textDocument': {'uri': main_uri}, 'position': at(MAIN, 'total'), 'newName': 'while'})
    assert 'error' in bad, bad

    # The declaring file open in the editor: its buffer is used, not the disk.
    edited_util = UTIL.replace('int add', '\n\nint add')
    util_uri = uri(root / 'util.skm')
    server.notify('textDocument/didOpen', {'textDocument': {'uri': util_uri, 'languageId': 'skrumi', 'version': 1, 'text': edited_util}})
    refs = server.request('textDocument/references', {'textDocument': {'uri': main_uri}, 'position': at(MAIN, 'add'),
                                                       'context': {'includeDeclaration': True}})['result']
    assert ('util.skm', 2, 4) in spots(refs), spots(refs)

    messy = 'int  x=1;\n\n\n\nif (x > 0) {\nprint(x);\n}'
    doc_uri = uri(root / 'messy.skm')
    server.notify('textDocument/didOpen', {'textDocument': {'uri': doc_uri, 'languageId': 'skrumi', 'version': 1, 'text': messy}})
    formatted = server.request('textDocument/formatting', {'textDocument': {'uri': doc_uri}, 'options': {'tabSize': 4, 'insertSpaces': True}})
    assert formatted['result'][0]['newText'] == 'int  x=1;\n\nif (x > 0) {\n    print(x);\n}\n', formatted

    # After `name.` the completion lists the fields and methods of name's struct, even
    # while the line is still unfinished.
    shapes = 'struct Point {\n    int x;\n    int y;\n    float length() { return 1.0; }\n}\nPoint p = Point(1, 2);\np.'
    shapes_uri = uri(root / 'shapes.skm')
    server.notify('textDocument/didOpen', {'textDocument': {'uri': shapes_uri, 'languageId': 'skrumi', 'version': 1, 'text': shapes}})
    listed = server.request('textDocument/completion', {'textDocument': {'uri': shapes_uri},
                                                        'position': {'line': 6, 'character': 2}})['result']
    assert sorted(item['label'] for item in listed) == ['length', 'x', 'y'], listed

    # After a module alias, the module's functions and variables.
    aliased = 'using math as m;\nfloat r = m.'
    aliased_uri = uri(root / 'aliased.skm')
    server.notify('textDocument/didOpen', {'textDocument': {'uri': aliased_uri, 'languageId': 'skrumi', 'version': 1, 'text': aliased}})
    listed = server.request('textDocument/completion', {'textDocument': {'uri': aliased_uri},
                                                        'position': {'line': 1, 'character': 12}})['result']
    labels = [item['label'] for item in listed]
    assert 'hypot' in labels and 'PI' in labels and 'print' not in labels, labels

    # After an enum's name, its values.
    colors = 'enum Color { Red, Green }\nColor c = Color.'
    colors_uri = uri(root / 'colors.skm')
    server.notify('textDocument/didOpen', {'textDocument': {'uri': colors_uri, 'languageId': 'skrumi', 'version': 1, 'text': colors}})
    listed = server.request('textDocument/completion', {'textDocument': {'uri': colors_uri},
                                                        'position': {'line': 1, 'character': 16}})['result']
    assert [item['label'] for item in listed] == ['Red', 'Green'], listed

    shadow = 'float measure(float size) { return size; }\n'
    shadow_uri = uri(root / 'shadow.skm')
    server.notify('textDocument/didOpen', {'textDocument': {'uri': shadow_uri, 'languageId': 'skrumi', 'version': 1, 'text': shadow}})
    pos = at(shadow, 'size')
    actions = server.request('textDocument/codeAction', {'textDocument': {'uri': shadow_uri},
        'range': {'start': pos, 'end': pos}, 'context': {'diagnostics': []}})['result']
    assert len(actions) == 1, actions
    assert actions[0]['diagnostics'][0]['code'] == 'SKR2001', actions
    edits = actions[0]['edit']['changes'][shadow_uri]
    assert len(edits) == 2 and all(e['newText'] == 'size_local' for e in edits), edits

    server.request('shutdown', None)
    server.notify('exit', None)
    server.process.wait(timeout=10)

print('LSP_EDITS_OK')
