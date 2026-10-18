// Run, check and build Skrumi programs from VS Code: the ▶ button in the editor
// title, commands, and a "skrumi" task type for tasks.json.
const fs = require('fs');
const os = require('os');
const path = require('path');
const vscode = require('vscode');

const TASK_TYPE = 'skrumi';

function config() {
    return vscode.workspace.getConfiguration('skrumi');
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

function runtimeEnvironment(extra = {}) {
    const home = installationHome();
    return Object.assign({}, home ? { SKRUMI_HOME: home } : {}, extra);
}

function resolveExecutable() {
    const custom = config().get('run.executablePath');
    if (custom && custom !== 'skrumi') return custom;
    const name = process.platform === 'win32' ? 'skrumi.exe' : 'skrumi';
    const candidates = [];
    const installedHome = installationHome();
    if (installedHome) candidates.push(path.join(installedHome, name));
    for (const folder of vscode.workspace.workspaceFolders || []) {
        candidates.push(path.join(folder.uri.fsPath, 'build', name));
        candidates.push(path.join(folder.uri.fsPath, 'build', 'Release', name));
    }
    candidates.push(path.join(os.homedir(), '.local', 'bin', name));
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) return candidate;
    }
    return 'skrumi';
}

// The program to run: skrumi.run.entryFile when it is set (a project whose main file
// includes the others), otherwise the file open in the editor.
function entryFile() {
    const entry = config().get('run.entryFile');
    if (entry) {
        if (path.isAbsolute(entry)) return entry;
        const editor = vscode.window.activeTextEditor;
        const folder = (editor && vscode.workspace.getWorkspaceFolder(editor.document.uri)) ||
            (vscode.workspace.workspaceFolders || [])[0];
        if (folder) return path.join(folder.uri.fsPath, entry);
    }
    const editor = vscode.window.activeTextEditor;
    if (editor && editor.document.languageId === 'skrumi' && editor.document.uri.scheme === 'file') {
        return editor.document.uri.fsPath;
    }
    return null;
}

function commandLine(command, file, extraArgs) {
    if (command === 'check') return ['check', file];
    if (command === 'build') return ['build', file, '-o', path.basename(file, path.extname(file))];
    return [file, ...(extraArgs || config().get('run.arguments') || [])];
}

const labels = { run: 'Запуск', check: 'Проверка', build: 'Сборка' };
// Full paths in error messages let the problem matcher find files from any directory,
// so it needs no workspace folder.
const environment = runtimeEnvironment({ SKRUMI_ABSOLUTE_PATHS: '1' });

// A task for a file inside an open folder; null for a file opened on its own, which
// VS Code cannot run as a task.
function makeTask(command, file, extraArgs) {
    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file));
    if (!folder) return null;
    const definition = { type: TASK_TYPE, command, file };
    const execution = new vscode.ProcessExecution(resolveExecutable(), commandLine(command, file, extraArgs), {
        cwd: path.dirname(file),
        env: environment
    });
    const task = new vscode.Task(definition, folder,
        `${labels[command] || command}: ${path.basename(file)}`, 'Skrumi', execution,
        command === 'check' ? ['$skrumi'] : ['$skrumi-runtime']);
    task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, clear: true, focus: command === 'run' };
    if (command === 'build') task.group = vscode.TaskGroup.Build;
    return task;
}

function runInTerminal(command, file) {
    const terminal = vscode.window.createTerminal({
        name: `Skrumi: ${labels[command] || command} ${path.basename(file)}`,
        shellPath: resolveExecutable(),
        shellArgs: commandLine(command, file),
        cwd: path.dirname(file),
        env: environment
    });
    terminal.show();
}

async function start(command) {
    const file = entryFile();
    if (!file) {
        vscode.window.showWarningMessage('Откройте файл .skm или укажите главный файл командой «Skrumi: выбрать главный файл».');
        return;
    }
    // Every file of a project may be part of the program, so all of them are saved.
    await vscode.workspace.saveAll(false);
    const task = makeTask(command, file);
    if (task) await vscode.tasks.executeTask(task);
    else runInTerminal(command, file);
}

async function chooseEntry() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'skrumi') {
        vscode.window.showWarningMessage('Откройте главный файл программы .skm.');
        return;
    }
    const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
    const value = folder ? path.relative(folder.uri.fsPath, editor.document.uri.fsPath) : editor.document.uri.fsPath;
    await config().update('run.entryFile', value, folder ? vscode.ConfigurationTarget.WorkspaceFolder : vscode.ConfigurationTarget.Global);
    vscode.window.showInformationMessage(`Главный файл Skrumi: ${value}`);
}

function activate(context) {
    context.subscriptions.push(vscode.commands.registerCommand('skrumi.run', () => start('run')));
    context.subscriptions.push(vscode.commands.registerCommand('skrumi.check', () => start('check')));
    context.subscriptions.push(vscode.commands.registerCommand('skrumi.build', () => start('build')));
    context.subscriptions.push(vscode.commands.registerCommand('skrumi.chooseEntry', chooseEntry));
    context.subscriptions.push(vscode.tasks.registerTaskProvider(TASK_TYPE, {
        provideTasks() {
            const file = entryFile();
            return file ? ['run', 'check', 'build'].map(command => makeTask(command, file)).filter(Boolean) : [];
        },
        resolveTask(task) {
            const definition = task.definition;
            if (!definition.file) return undefined;
            let file = definition.file;
            const folder = (vscode.workspace.workspaceFolders || [])[0];
            if (!path.isAbsolute(file) && folder) file = path.join(folder.uri.fsPath, file);
            const resolved = makeTask(definition.command || 'run', file, definition.args);
            if (!resolved) return undefined;
            resolved.definition = definition;
            return resolved;
        }
    }));
}

module.exports = { activate, config, entryFile, resolveExecutable, installationHome, runtimeEnvironment };
