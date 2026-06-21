'use strict';

const path = require('path');
const vscode = require('vscode');
const {
  LanguageClient,
  TransportKind,
} = require('vscode-languageclient/node');

let client;
let outputChannel;
let statusBar;
let taskProvider;
let restartPromise = Promise.resolve();

function pieConfiguration(uri) {
  return vscode.workspace.getConfiguration('pie', uri);
}

function compilerPath(uri) {
  return pieConfiguration(uri).get('compiler.path', 'pie');
}

function workspaceFolderFor(uri) {
  if (uri) {
    const folder = vscode.workspace.getWorkspaceFolder(uri);
    if (folder) return folder;
  }
  return vscode.workspace.workspaceFolders?.[0];
}

function activePieUri(argument) {
  if (argument instanceof vscode.Uri) return argument;
  if (typeof argument === 'string') return vscode.Uri.parse(argument);
  if (argument && typeof argument === 'object' && typeof argument.fsPath === 'string') {
    return vscode.Uri.file(argument.fsPath);
  }
  const editor = vscode.window.activeTextEditor;
  return editor?.document.languageId === 'pie' ? editor.document.uri : undefined;
}

async function savePieDocument(uri) {
  const document = vscode.workspace.textDocuments.find((item) => item.uri.toString() === uri.toString());
  if (document?.isDirty) await document.save();
}

function getTerminal() {
  const existing = vscode.window.terminals.find((terminal) => terminal.name === 'Pie');
  return existing || vscode.window.createTerminal({ name: 'Pie' });
}

function shellQuote(value) {
  const stringValue = String(value);
  if (process.platform === 'win32') return `"${stringValue.replace(/"/g, '""')}"`;
  return `'${stringValue.replace(/'/g, `'\\''`)}'`;
}

async function runInTerminal(command, args, uri) {
  const terminal = getTerminal();
  const folder = workspaceFolderFor(uri);
  if (folder) terminal.sendText(`cd ${shellQuote(folder.uri.fsPath)}`);
  terminal.show(true);
  terminal.sendText([command, ...args].map(shellQuote).join(' '));
}

async function checkFile(argument) {
  const uri = activePieUri(argument);
  if (!uri) {
    await vscode.window.showInformationMessage('Open a Pie file to check it.');
    return;
  }
  await savePieDocument(uri);
  if (client) {
    statusBar.text = '$(sync~spin) Pie: checking';
    try {
      const result = await client.sendRequest('pie/checkDocument', { uri: uri.toString() });
      statusBar.text = result.ok ? '$(check) Pie' : '$(error) Pie';
      statusBar.tooltip = result.message;
      vscode.window.setStatusBarMessage(result.message, 3500);
      return;
    } catch (error) {
      outputChannel.appendLine(`Language-server check failed: ${error.stack || error.message}`);
    }
  }
  await runInTerminal(compilerPath(uri), ['check', uri.fsPath], uri);
}

async function runFile(argument) {
  const uri = activePieUri(argument);
  if (!uri) {
    await vscode.window.showInformationMessage('Open a Pie file to run it.');
    return;
  }
  await savePieDocument(uri);
  if (client) {
    statusBar.text = '$(sync~spin) Pie: running';
    try {
      const result = await client.sendRequest('pie/runDocument', { uri: uri.toString() });
      statusBar.text = result.ok ? '$(check) Pie' : '$(error) Pie';
      statusBar.tooltip = result.message;
      if (result.output) {
        outputChannel.clear();
        outputChannel.append(result.output.endsWith('\n') ? result.output : `${result.output}\n`);
        outputChannel.show(true);
      }
      if (result.ok) vscode.window.setStatusBarMessage(result.message, 3500);
      else await vscode.window.showErrorMessage(result.message);
      return;
    } catch (error) {
      outputChannel.appendLine(`Language-server run failed: ${error.stack || error.message}`);
    }
  }
  await runInTerminal(compilerPath(uri), ['run', uri.fsPath], uri);
}

async function runPackageCommand(command) {
  const uri = activePieUri();
  const folder = workspaceFolderFor(uri);
  if (!folder) {
    await vscode.window.showInformationMessage(`Open a Pie workspace to ${command} it.`);
    return;
  }
  await vscode.workspace.saveAll(false);
  await runInTerminal(compilerPath(folder.uri), [command], folder.uri);
}

async function showReferences(uriValue, positionValue) {
  const uri = activePieUri(uriValue);
  if (!uri || !positionValue) return;
  const position = new vscode.Position(positionValue.line, positionValue.character);
  const locations = await vscode.commands.executeCommand('vscode.executeReferenceProvider', uri, position) || [];
  await vscode.commands.executeCommand('editor.action.showReferences', uri, position, locations);
}

async function showCompilerVersion() {
  if (!client) {
    await vscode.window.showInformationMessage('Pie language server is not running.');
    return;
  }
  const uri = activePieUri() || workspaceFolderFor()?.uri;
  const result = await client.sendRequest('pie/compilerVersion', { uri: uri?.toString() });
  outputChannel.appendLine(result.output || result.message);
  await vscode.window.showInformationMessage(result.message);
}

async function showResolvedCompilerPath() {
  if (!client) {
    await vscode.window.showInformationMessage(`Configured Pie compiler: ${compilerPath(activePieUri())}`);
    return;
  }
  const uri = activePieUri() || workspaceFolderFor()?.uri;
  const result = await client.sendRequest('pie/resolveCompiler', { uri: uri?.toString() });
  await vscode.window.showInformationMessage(result.message);
}

async function showPackageInfo() {
  if (!client) {
    await vscode.window.showInformationMessage('Pie language server is not running.');
    return;
  }
  const uri = activePieUri() || workspaceFolderFor()?.uri;
  const result = await client.sendRequest('pie/packageInfo', { uri: uri?.toString() });
  outputChannel.appendLine(result.output || result.message);
  await vscode.window.showInformationMessage(result.message);
}

class PieTaskProvider {
  provideTasks() {
    const folder = vscode.workspace.workspaceFolders?.[0];
    if (!folder) return [];
    return ['check', 'build', 'run', 'test'].map((command) => this.makeTask({ type: 'pie', command }, folder));
  }

  resolveTask(task) {
    const folder = task.scope instanceof vscode.WorkspaceFolder
      ? task.scope
      : vscode.workspace.workspaceFolders?.[0];
    return folder ? this.makeTask(task.definition, folder, task.name) : undefined;
  }

  makeTask(definition, folder, name) {
    const args = [definition.command];
    if (definition.file) args.push(definition.file);
    const execution = new vscode.ShellExecution(compilerPath(folder.uri), args, { cwd: folder.uri.fsPath });
    const task = new vscode.Task(definition, folder, name || `Pie: ${definition.command}`, 'pie', execution, []);
    task.group = definition.command === 'build'
      ? vscode.TaskGroup.Build
      : definition.command === 'test' ? vscode.TaskGroup.Test : undefined;
    task.presentationOptions = {
      reveal: vscode.TaskRevealKind.Always,
      panel: vscode.TaskPanelKind.Dedicated,
      clear: false,
    };
    return task;
  }
}

async function startClient(context) {
  if (!pieConfiguration().get('server.enabled', true)) {
    statusBar.text = '$(circle-slash) Pie LSP';
    statusBar.tooltip = 'Pie language server is disabled';
    return;
  }

  const serverModule = context.asAbsolutePath(path.join('dist', 'server.js'));
  const serverOptions = {
    run: { module: serverModule, transport: TransportKind.ipc },
    debug: { module: serverModule, transport: TransportKind.ipc, options: { execArgv: ['--nolazy', '--inspect=6010'] } },
  };
  const watcher = vscode.workspace.createFileSystemWatcher('**/*.{pie,toml}');
  context.subscriptions.push(watcher);
  const clientOptions = {
    documentSelector: [{ scheme: 'file', language: 'pie' }, { scheme: 'untitled', language: 'pie' }],
    synchronize: { configurationSection: 'pie', fileEvents: watcher },
    outputChannel,
    traceOutputChannel: outputChannel,
    markdown: { isTrusted: true, supportHtml: false },
    initializationOptions: { extensionVersion: context.extension.packageJSON.version },
  };

  client = new LanguageClient('pieLanguageServer', 'Pie Language Server', serverOptions, clientOptions);
  client.onDidChangeState((event) => {
    if (event.newState === 2) {
      statusBar.text = '$(check) Pie';
      statusBar.tooltip = 'Pie language server is ready';
    } else if (event.newState === 1) {
      statusBar.text = '$(sync~spin) Pie';
      statusBar.tooltip = 'Pie language server is starting';
    } else {
      statusBar.text = '$(warning) Pie';
      statusBar.tooltip = 'Pie language server stopped';
    }
  });
  statusBar.text = '$(sync~spin) Pie';
  await client.start();
}

function restartClient(context) {
  restartPromise = restartPromise.then(async () => {
    statusBar.text = '$(sync~spin) Pie: restarting';
    if (client) {
      const oldClient = client;
      client = undefined;
      await oldClient.stop();
    }
    await startClient(context);
  }).catch((error) => {
    outputChannel.appendLine(`Could not restart Pie language server: ${error.stack || error.message}`);
    statusBar.text = '$(error) Pie';
  });
  return restartPromise;
}

function updateStatusVisibility() {
  const editor = vscode.window.activeTextEditor;
  const keepVisible = pieConfiguration(editor?.document.uri).get('statusBar.showOutsidePie', false);
  if (editor?.document.languageId === 'pie' || keepVisible) {
    statusBar.show();
    statusBar.backgroundColor = editor?.document.languageId === 'pie'
      ? undefined
      : new vscode.ThemeColor('statusBarItem.prominentBackground');
  } else {
    statusBar.hide();
  }
}

async function activate(context) {
  outputChannel = vscode.window.createOutputChannel('Pie Language Server');
  statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
  statusBar.name = 'Pie Language Server';
  statusBar.command = 'pie.showLanguageServerOutput';
  statusBar.text = '$(sync~spin) Pie';
  statusBar.tooltip = 'Starting Pie language server';
  updateStatusVisibility();

  taskProvider = new PieTaskProvider();
  context.subscriptions.push(
    outputChannel,
    statusBar,
    vscode.commands.registerCommand('pie.checkFile', checkFile),
    vscode.commands.registerCommand('pie.runFile', runFile),
    vscode.commands.registerCommand('pie.buildWorkspace', () => runPackageCommand('build')),
    vscode.commands.registerCommand('pie.testWorkspace', () => runPackageCommand('test')),
    vscode.commands.registerCommand('pie.restartLanguageServer', () => restartClient(context)),
    vscode.commands.registerCommand('pie.showLanguageServerOutput', () => outputChannel.show(true)),
    vscode.commands.registerCommand('pie.showCompilerVersion', showCompilerVersion),
    vscode.commands.registerCommand('pie.showResolvedCompilerPath', showResolvedCompilerPath),
    vscode.commands.registerCommand('pie.showPackageInfo', showPackageInfo),
    vscode.commands.registerCommand('pie.showReferences', showReferences),
    vscode.tasks.registerTaskProvider('pie', taskProvider),
    vscode.window.onDidChangeActiveTextEditor(updateStatusVisibility),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (event.affectsConfiguration('pie.server.enabled')) restartClient(context);
      if (event.affectsConfiguration('pie.statusBar.showOutsidePie')) updateStatusVisibility();
    }),
  );

  await startClient(context);
}

async function deactivate() {
  if (client) await client.stop();
}

module.exports = { activate, deactivate };
