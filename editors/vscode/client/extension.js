const cp = require('child_process');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

let serverProcess = null;
let diagnosticCollection = null;
let outputChannel = null;
let messageBuffer = Buffer.alloc(0);
let requestId = 1;
const pendingRequests = new Map();

function log(msg) {
    if (outputChannel) {
        outputChannel.appendLine(`[Skrumi LSP] ${msg}`);
    }
}

function installationHome() {
    if (process.env.SKRUMI_HOME) return process.env.SKRUMI_HOME;
    try {
        const [installed] = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'installation.json'), 'utf8'));
        if (typeof installed === 'string' && path.isAbsolute(installed)) return installed;
    } catch (_) { /* Marketplace/manual installs have no installer path hint. */ }

    if (process.platform === 'win32' && process.env.LOCALAPPDATA) {
        return path.join(process.env.LOCALAPPDATA, 'Skrumi');
    }
    return '';
}

function resolveServerPath() {
    const config = vscode.workspace.getConfiguration('skrumi');
    let customPath = config.get('lsp.serverPath');
    if (customPath && customPath !== 'skrumi-lsp') {
        return customPath;
    }

    // Check workspace local build first
    if (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length > 0) {
        for (const folder of vscode.workspace.workspaceFolders) {
            const localBin = path.join(folder.uri.fsPath, 'build', 'skrumi-lsp');
            if (fs.existsSync(localBin)) {
                return localBin;
            }
            const localBinWin = path.join(folder.uri.fsPath, 'build', 'skrumi-lsp.exe');
            if (fs.existsSync(localBinWin)) {
                return localBinWin;
            }
        }
    }

    // Check common installation paths
    const home = process.env.HOME || process.env.USERPROFILE || '';
    const installedHome = installationHome();
    const candidatePaths = [
        ...(installedHome ? [path.join(installedHome, process.platform === 'win32' ? 'skrumi-lsp.exe' : 'skrumi-lsp')] : []),
        path.join(home, '.local', 'bin', 'skrumi-lsp'),
        path.join(home, '.local', 'share', 'skrumi', 'skrumi-lsp'),
        '/usr/local/bin/skrumi-lsp',
        '/usr/bin/skrumi-lsp'
    ];

    for (const p of candidatePaths) {
        if (p && fs.existsSync(p)) {
            return p;
        }
    }

    return 'skrumi-lsp';
}

function sendMessage(msg) {
    if (!serverProcess || !serverProcess.stdin || serverProcess.stdin.destroyed) {
        return;
    }
    const json = JSON.stringify(msg);
    const body = Buffer.from(json, 'utf8');
    const header = Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, 'ascii');
    serverProcess.stdin.write(header);
    serverProcess.stdin.write(body);
}

function sendRequest(method, params) {
    return new Promise((resolve, reject) => {
        if (!serverProcess) {
            return reject(new Error('Server not running'));
        }
        const id = requestId++;
        pendingRequests.set(id, { resolve, reject });
        sendMessage({ jsonrpc: '2.0', id, method, params });
    });
}

function handleMessage(msg) {
    if (!msg || typeof msg !== 'object') return;

    // Handle responses to requests
    if (msg.id !== undefined && pendingRequests.has(msg.id)) {
        const { resolve, reject } = pendingRequests.get(msg.id);
        pendingRequests.delete(msg.id);
        if (msg.error) {
            reject(new Error(msg.error.message || 'LSP Request Error'));
        } else {
            resolve(msg.result);
        }
        return;
    }

    // Handle notifications from server
    if (msg.method === 'textDocument/publishDiagnostics') {
        const params = msg.params;
        if (!params || !params.uri) return;
        const uri = vscode.Uri.parse(params.uri);
        const diagnostics = (params.diagnostics || []).map(d => {
            const startLine = Math.max(0, (d.range && d.range.start ? d.range.start.line : 0));
            const startChar = Math.max(0, (d.range && d.range.start ? d.range.start.character : 0));
            const endLine = Math.max(0, (d.range && d.range.end ? d.range.end.line : startLine));
            const endChar = Math.max(0, (d.range && d.range.end ? d.range.end.character : startChar + 1));

            const range = new vscode.Range(startLine, startChar, endLine, endChar);
            let severity = vscode.DiagnosticSeverity.Error;
            if (d.severity === 2) severity = vscode.DiagnosticSeverity.Warning;
            else if (d.severity === 3) severity = vscode.DiagnosticSeverity.Information;
            else if (d.severity === 4) severity = vscode.DiagnosticSeverity.Hint;

            const diag = new vscode.Diagnostic(range, d.message, severity);
            diag.source = 'Skrumi';
            return diag;
        });

        diagnosticCollection.set(uri, diagnostics);
    }
}

function startServer(context) {
    const serverPath = resolveServerPath();
    log(`Starting Skrumi LSP server: ${serverPath}`);

    try {
        const installedHome = installationHome();
        const env = Object.assign({}, process.env);
        if (installedHome) env.SKRUMI_HOME = installedHome;
        serverProcess = cp.spawn(serverPath, [], {
            stdio: ['pipe', 'pipe', 'pipe'],
            env
        });
    } catch (err) {
        log(`Failed to spawn server process: ${err.message}`);
        vscode.window.showErrorMessage(`Не удалось запустить skrumi-lsp: ${err.message}`);
        return;
    }

    serverProcess.on('error', err => {
        log(`Server error: ${err.message}`);
        vscode.window.showErrorMessage(`Ошибка skrumi-lsp: ${err.message}. Проверьте путь в настройках.`);
    });

    serverProcess.on('exit', (code, signal) => {
        log(`Server exited with code: ${code}, signal: ${signal}`);
        serverProcess = null;
    });

    serverProcess.stderr.on('data', chunk => {
        const text = chunk.toString('utf8');
        log(text.trim());
    });

    messageBuffer = Buffer.alloc(0);
    serverProcess.stdout.on('data', chunk => {
        messageBuffer = Buffer.concat([messageBuffer, chunk]);
        while (true) {
            const headerEnd = messageBuffer.indexOf('\r\n\r\n');
            if (headerEnd === -1) break;

            const header = messageBuffer.slice(0, headerEnd).toString('ascii');
            const match = header.match(/Content-Length:\s*(\d+)/i);
            if (!match) {
                // Invalid header, discard
                messageBuffer = messageBuffer.slice(headerEnd + 4);
                continue;
            }

            const contentLength = parseInt(match[1], 10);
            const totalLength = headerEnd + 4 + contentLength;
            if (messageBuffer.length < totalLength) break;

            const bodyBytes = messageBuffer.slice(headerEnd + 4, totalLength);
            messageBuffer = messageBuffer.slice(totalLength);

            try {
                const parsed = JSON.parse(bodyBytes.toString('utf8'));
                handleMessage(parsed);
            } catch (err) {
                log(`JSON parse error: ${err.message}`);
            }
        }
    });

    // Initialize handshake
    const rootUri = (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length > 0)
        ? vscode.workspace.workspaceFolders[0].uri.toString()
        : null;

    sendRequest('initialize', {
        processId: process.pid,
        rootUri: rootUri,
        capabilities: {
            textDocument: {
                synchronization: { dynamicRegistration: false },
                completion: { dynamicRegistration: false },
                signatureHelp: { dynamicRegistration: false },
                hover: { dynamicRegistration: false },
                definition: { dynamicRegistration: false },
                documentSymbol: { dynamicRegistration: false }
            }
        }
    }).then(res => {
        log('Server initialized successfully');
        sendMessage({ jsonrpc: '2.0', method: 'initialized', params: {} });

        // Synchronize currently opened Skrumi documents
        vscode.workspace.textDocuments.forEach(doc => {
            if (doc.languageId === 'skrumi') {
                syncDocumentOpen(doc);
            }
        });
    }).catch(err => {
        log(`Initialize request failed: ${err.message}`);
    });
}

function syncDocumentOpen(document) {
    if (document.languageId !== 'skrumi') return;
    sendMessage({
        jsonrpc: '2.0',
        method: 'textDocument/didOpen',
        params: {
            textDocument: {
                uri: document.uri.toString(),
                languageId: 'skrumi',
                version: document.version,
                text: document.getText()
            }
        }
    });
}

function syncDocumentChange(event) {
    if (event.document.languageId !== 'skrumi') return;
    sendMessage({
        jsonrpc: '2.0',
        method: 'textDocument/didChange',
        params: {
            textDocument: {
                uri: event.document.uri.toString(),
                version: event.document.version
            },
            contentChanges: [
                { text: event.document.getText() }
            ]
        }
    });
}

function syncDocumentClose(document) {
    if (document.languageId !== 'skrumi') return;
    diagnosticCollection.delete(document.uri);
    sendMessage({
        jsonrpc: '2.0',
        method: 'textDocument/didClose',
        params: {
            textDocument: {
                uri: document.uri.toString()
            }
        }
    });
}

function activate(context) {
    outputChannel = vscode.window.createOutputChannel('Skrumi Language Server');
    diagnosticCollection = vscode.languages.createDiagnosticCollection('skrumi');
    context.subscriptions.push(diagnosticCollection);
    context.subscriptions.push(outputChannel);

    startServer(context);
    require('./run').activate(context);
    require('./debug').activate(context);

    // Document synchronization events
    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(doc => syncDocumentOpen(doc)));
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(e => syncDocumentChange(e)));
    context.subscriptions.push(vscode.workspace.onDidCloseTextDocument(doc => syncDocumentClose(doc)));
    // A saved file changes what the other files of its project see.
    context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(doc => {
        if (doc.languageId !== 'skrumi') return;
        sendMessage({ jsonrpc: '2.0', method: 'textDocument/didSave', params: { textDocument: { uri: doc.uri.toString() } } });
    }));

    // Hover Provider
    context.subscriptions.push(vscode.languages.registerHoverProvider('skrumi', {
        async provideHover(document, position) {
            try {
                const res = await sendRequest('textDocument/hover', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character }
                });
                if (!res || !res.contents) return null;
                const text = typeof res.contents === 'string' ? res.contents : (res.contents.value || '');
                if (!text) return null;
                return new vscode.Hover(new vscode.MarkdownString(text));
            } catch {
                return null;
            }
        }
    }));

    // Completion Provider
    context.subscriptions.push(vscode.languages.registerCompletionItemProvider('skrumi', {
        async provideCompletionItems(document, position) {
            try {
                const res = await sendRequest('textDocument/completion', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character }
                });
                if (!res) return null;
                const items = Array.isArray(res) ? res : (res.items || []);
                return items.map(item => {
                    const ci = new vscode.CompletionItem(item.label);
                    ci.detail = item.detail;
                    if (item.documentation) {
                        const doc = typeof item.documentation === 'string' ? item.documentation : item.documentation.value;
                        ci.documentation = new vscode.MarkdownString(doc);
                    }
                    if (item.kind) {
                        ci.kind = item.kind - 1;
                    }
                    return ci;
                });
            } catch {
                return null;
            }
        }
    }, '.', ' '));

    // Signature Help Provider (Fires on '(' and ',')
    context.subscriptions.push(vscode.languages.registerSignatureHelpProvider('skrumi', {
        async provideSignatureHelp(document, position) {
            try {
                const res = await sendRequest('textDocument/signatureHelp', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character }
                });
                if (!res || !res.signatures || res.signatures.length === 0) return null;
                const sh = new vscode.SignatureHelp();
                sh.activeSignature = res.activeSignature || 0;
                sh.activeParameter = res.activeParameter || 0;
                sh.signatures = res.signatures.map(s => {
                    const sig = new vscode.SignatureInformation(s.label);
                    if (s.documentation) {
                        const doc = typeof s.documentation === 'string' ? s.documentation : (s.documentation.value || '');
                        sig.documentation = new vscode.MarkdownString(doc);
                    }
                    if (Array.isArray(s.parameters)) {
                        sig.parameters = s.parameters.map(p => {
                            const pi = new vscode.ParameterInformation(p.label);
                            if (p.documentation) {
                                const pdoc = typeof p.documentation === 'string' ? p.documentation : (p.documentation.value || '');
                                pi.documentation = new vscode.MarkdownString(pdoc);
                            }
                            return pi;
                        });
                    }
                    return sig;
                });
                return sh;
            } catch {
                return null;
            }
        }
    }, '(', ','));

    // Definition Provider (F12 / Ctrl+Click)
    context.subscriptions.push(vscode.languages.registerDefinitionProvider('skrumi', {
        async provideDefinition(document, position) {
            try {
                const res = await sendRequest('textDocument/definition', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character }
                });
                if (!res) return null;
                const locations = Array.isArray(res) ? res : [res];
                return locations.map(loc => {
                    const targetUri = vscode.Uri.parse(loc.uri);
                    const range = new vscode.Range(
                        loc.range.start.line,
                        loc.range.start.character,
                        loc.range.end.line,
                        loc.range.end.character
                    );
                    return new vscode.Location(targetUri, range);
                });
            } catch {
                return null;
            }
        }
    }));

    // Document Symbols Provider (Ctrl+Shift+O)
    context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider('skrumi', {
        async provideDocumentSymbols(document) {
            try {
                const res = await sendRequest('textDocument/documentSymbol', {
                    textDocument: { uri: document.uri.toString() }
                });
                if (!res || !Array.isArray(res)) return null;

                function convertSymbol(s) {
                    const range = new vscode.Range(
                        s.range.start.line,
                        s.range.start.character,
                        s.range.end.line,
                        s.range.end.character
                    );
                    const selRange = s.selectionRange ? new vscode.Range(
                        s.selectionRange.start.line,
                        s.selectionRange.start.character,
                        s.selectionRange.end.line,
                        s.selectionRange.end.character
                    ) : range;

                    const kind = s.kind ? (s.kind - 1) : vscode.SymbolKind.Variable;
                    const docSymbol = new vscode.DocumentSymbol(s.name, s.detail || '', kind, range, selRange);
                    if (s.children && Array.isArray(s.children)) {
                        docSymbol.children = s.children.map(convertSymbol);
                    }
                    return docSymbol;
                }

                return res.map(convertSymbol);
            } catch {
                return null;
            }
        }
    }));

    const toRange = r => new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character);

    // Find All References (Shift+F12): every file of the program.
    context.subscriptions.push(vscode.languages.registerReferenceProvider('skrumi', {
        async provideReferences(document, position, referenceContext) {
            try {
                const res = await sendRequest('textDocument/references', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character },
                    context: { includeDeclaration: referenceContext.includeDeclaration }
                });
                if (!Array.isArray(res)) return null;
                return res.map(loc => new vscode.Location(vscode.Uri.parse(loc.uri), toRange(loc.range)));
            } catch {
                return null;
            }
        }
    }));

    // Rename Symbol (F2): the declaration and its uses in every file of the program.
    context.subscriptions.push(vscode.languages.registerRenameProvider('skrumi', {
        async prepareRename(document, position) {
            const res = await sendRequest('textDocument/prepareRename', {
                textDocument: { uri: document.uri.toString() },
                position: { line: position.line, character: position.character }
            });
            return { range: toRange(res.range), placeholder: res.placeholder };
        },
        async provideRenameEdits(document, position, newName) {
            const res = await sendRequest('textDocument/rename', {
                textDocument: { uri: document.uri.toString() },
                position: { line: position.line, character: position.character },
                newName
            });
            const edit = new vscode.WorkspaceEdit();
            for (const [uri, edits] of Object.entries((res && res.changes) || {})) {
                for (const change of edits) edit.replace(vscode.Uri.parse(uri), toRange(change.range), change.newText);
            }
            return edit;
        }
    }));

    // Format Document (Shift+Alt+F): the same layout as skrumi fmt.
    context.subscriptions.push(vscode.languages.registerDocumentFormattingEditProvider('skrumi', {
        async provideDocumentFormattingEdits(document, options) {
            try {
                const res = await sendRequest('textDocument/formatting', {
                    textDocument: { uri: document.uri.toString() },
                    options: { tabSize: options.tabSize, insertSpaces: options.insertSpaces }
                });
                if (!Array.isArray(res)) return [];
                return res.map(e => vscode.TextEdit.replace(toRange(e.range), e.newText));
            } catch {
                return [];
            }
        }
    }));

    // Command to restart LSP
    context.subscriptions.push(vscode.commands.registerCommand('skrumi.restartServer', () => {
        if (serverProcess) {
            try { serverProcess.kill(); } catch {}
            serverProcess = null;
        }
        startServer(context);
        vscode.window.showInformationMessage('Skrumi Language Server перезапущен.');
    }));
}

function deactivate() {
    if (serverProcess) {
        try {
            sendMessage({ jsonrpc: '2.0', method: 'shutdown', id: requestId++ });
            setTimeout(() => {
                if (serverProcess) {
                    try { serverProcess.kill(); } catch {}
                }
            }, 500);
        } catch {}
    }
    if (diagnosticCollection) {
        diagnosticCollection.clear();
    }
}

module.exports = {
    activate,
    deactivate
};
