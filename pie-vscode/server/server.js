'use strict';

const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');
const { fileURLToPath, pathToFileURL } = require('url');
const {
  CodeActionKind,
  CompletionItemKind,
  CompletionItemTag,
  createConnection,
  DiagnosticSeverity,
  DidChangeConfigurationNotification,
  DocumentHighlightKind,
  FileChangeType,
  InsertTextFormat,
  Location,
  MarkupKind,
  ProposedFeatures,
  SymbolKind,
  TextDocumentSyncKind,
} = require('vscode-languageserver/node');
const { TextDocument } = require('vscode-languageserver-textdocument');
const { TextDocuments } = require('vscode-languageserver/node');
const language = require('./language');
const analyzer = require('./analyzer');

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);
const index = new Map();
const nativeDiagnostics = new Map();
const settingsCache = new Map();
const workspaceRoots = [];
const compilerWarnings = new Set();
const pendingCompilerChecks = new Map();

const semanticTokenTypes = [
  'namespace', 'type', 'class', 'enum', 'interface', 'struct', 'typeParameter',
  'parameter', 'variable', 'property', 'enumMember', 'function', 'method',
  'keyword', 'comment', 'string', 'number', 'operator',
];
const semanticTokenModifiers = ['declaration', 'definition', 'readonly', 'static', 'defaultLibrary', 'modification'];

let hasConfigurationCapability = false;
let hasWorkspaceFolderCapability = false;

function pathToUri(filePath) {
  return pathToFileURL(path.resolve(filePath)).toString();
}

function uriToPath(uri) {
  try {
    return fileURLToPath(uri);
  } catch {
    return undefined;
  }
}

function workspaceRootForUri(uri) {
  const filePath = uriToPath(uri);
  if (!filePath) return workspaceRoots[0];
  return workspaceRoots
    .filter((root) => filePath === root || filePath.startsWith(`${root}${path.sep}`))
    .sort((left, right) => right.length - left.length)[0] || workspaceRoots[0];
}

function findPackageRoot(filePath) {
  let directory = path.dirname(filePath);
  const workspaceRoot = workspaceRootForUri(pathToUri(filePath));
  for (;;) {
    if (fs.existsSync(path.join(directory, 'pie.toml'))) return directory;
    if (directory === workspaceRoot || directory === path.dirname(directory)) return workspaceRoot || path.dirname(filePath);
    directory = path.dirname(directory);
  }
}

async function getSettings(uri) {
  if (!hasConfigurationCapability) {
    return {
      compiler: { path: 'pie', diagnostics: true, timeout: 15000, jsonDiagnostics: true, checkOnChange: false },
      diagnostics: { live: true, semantic: true },
      server: { diagnosticDebounce: 400 },
      completion: { autoImports: true },
      formatting: { indentSize: 4 },
      codeLens: { enabled: true },
    };
  }
  if (!settingsCache.has(uri)) {
    settingsCache.set(uri, connection.workspace.getConfiguration({ scopeUri: uri, section: 'pie' }));
  }
  return settingsCache.get(uri);
}

function parsedForUri(uri) {
  const document = documents.get(uri);
  if (document) {
    const cached = index.get(uri);
    if (!cached || cached.text !== document.getText()) {
      const parsed = language.parseDocument(uri, document.getText());
      index.set(uri, parsed);
      return parsed;
    }
  }
  return index.get(uri);
}

function allParsedDocuments() {
  return [...index.values()];
}

function isIgnoredDirectory(name) {
  return name === '.git' || name === 'node_modules' || name === 'build'
    || name === 'dist' || name === '.cache' || name === '.vscode-test';
}

function scanDirectory(root, files, depth = 0) {
  if (depth > 30 || files.length >= 10000) return;
  let entries;
  try {
    entries = fs.readdirSync(root, { withFileTypes: true });
  } catch {
    return;
  }
  for (const entry of entries) {
    const fullPath = path.join(root, entry.name);
    if (entry.isDirectory() && !isIgnoredDirectory(entry.name)) scanDirectory(fullPath, files, depth + 1);
    else if (entry.isFile() && entry.name.endsWith('.pie')) files.push(fullPath);
  }
}

function indexFile(filePath) {
  const uri = pathToUri(filePath);
  if (documents.get(uri)) return;
  try {
    index.set(uri, language.parseDocument(uri, fs.readFileSync(filePath, 'utf8')));
  } catch (error) {
    connection.console.warn(`Could not index ${filePath}: ${error.message}`);
  }
}

function indexWorkspaces() {
  const files = [];
  for (const root of workspaceRoots) scanDirectory(root, files);
  for (const filePath of files) indexFile(filePath);
  connection.console.log(`Indexed ${files.length} Pie source files.`);
}

function symbolLocation(symbol) {
  return Location.create(symbol.uri, symbol.selectionRange);
}

function symbolDeclaredAt(parsed, token) {
  return parsed.symbols.find((symbol) => symbol.name === token.text
    && symbol.selectionRange.start.line === token.line
    && symbol.selectionRange.start.character === token.column);
}

function globalSymbolsNamed(name) {
  return allParsedDocuments().flatMap((parsed) => parsed.globals.filter((symbol) => symbol.name === name));
}

function typeSymbolsNamed(name) {
  return allParsedDocuments().flatMap((parsed) => parsed.types.filter((symbol) => symbol.name === name));
}

function methodsForReceiver(name) {
  return allParsedDocuments().flatMap((parsed) => parsed.functions.filter((symbol) => symbol.receiver === name));
}

function childrenForType(name) {
  return typeSymbolsNamed(name).flatMap((symbol) => symbol.children);
}

function resolveSimpleSymbol(parsed, name, line) {
  const local = language.visibleSymbols(parsed, line).find((symbol) => symbol.name === name);
  if (local) return local;
  const sameDocument = parsed.globals.find((symbol) => symbol.name === name);
  if (sameDocument) return sameDocument;
  return globalSymbolsNamed(name)[0];
}

function tokenBefore(parsed, token, distance = 1) {
  let current = token;
  for (let count = 0; count < distance; count += 1) {
    current = language.previousToken(parsed, current);
    if (!current) return undefined;
  }
  return current;
}

function resolveSymbol(parsed, token, line) {
  const declared = symbolDeclaredAt(parsed, token);
  if (declared) return declared;
  const previous = language.previousToken(parsed, token);
  if (previous?.text === '.') {
    const ownerToken = tokenBefore(parsed, token, 2);
    if (ownerToken) {
      const ownerSymbol = resolveSimpleSymbol(parsed, ownerToken.text, line);
      const ownerType = ownerSymbol ? language.baseType(ownerSymbol.type) : ownerToken.text;
      return childrenForType(ownerType).find((symbol) => symbol.name === token.text)
        || methodsForReceiver(ownerType).find((symbol) => symbol.name === token.text);
    }
  }
  return resolveSimpleSymbol(parsed, token.text, line);
}

function findModuleTarget(parsed, requiredPath) {
  const sourcePath = uriToPath(parsed.uri);
  if (!sourcePath || language.STANDARD_MODULES.includes(requiredPath)) return undefined;
  const root = workspaceRootForUri(parsed.uri);
  const relative = `${requiredPath}.pie`;
  const candidates = [
    path.resolve(path.dirname(sourcePath), relative),
    root ? path.resolve(root, 'src', relative) : '',
    root ? path.resolve(root, relative) : '',
  ].filter(Boolean);
  const segments = requiredPath.split('/');
  if (root && segments.length > 1) candidates.push(path.resolve(root, 'src', `${segments.slice(1).join('/')}.pie`));
  const direct = candidates.find((candidate) => fs.existsSync(candidate));
  if (direct) return pathToUri(direct);
  const normalizedSuffix = `/src/${requiredPath.replace(/\\/g, '/')}.pie`;
  return [...index.keys()].find((uri) => uri.replace(/\\/g, '/').endsWith(normalizedSuffix));
}

function publishDiagnostics(uri) {
  const parsed = parsedForUri(uri);
  if (!parsed) return;
  getSettings(uri).then((settings) => {
    const syntax = settings.diagnostics?.live === false ? [] : parsed.diagnostics;
    const semantic = settings.diagnostics?.semantic === false ? [] : analyzer.analyzeParsed(parsed, allParsedDocuments());
    connection.sendDiagnostics({ uri, diagnostics: [...syntax, ...semantic, ...(nativeDiagnostics.get(uri) || [])] });
  }).catch((error) => connection.console.error(error.stack || error.message));
}

function scheduleCompilerCheck(uri) {
  getSettings(uri).then((settings) => {
    if (!settings.compiler?.checkOnChange) return;
    const delay = Math.max(100, Number(settings.server?.diagnosticDebounce || 400));
    if (pendingCompilerChecks.has(uri)) clearTimeout(pendingCompilerChecks.get(uri));
    pendingCompilerChecks.set(uri, setTimeout(() => {
      pendingCompilerChecks.delete(uri);
      checkDocument(uri).catch((error) => connection.console.error(error.stack || error.message));
    }, delay));
  }).catch((error) => connection.console.error(error.stack || error.message));
}

function compilerCandidates(uri, configuredPath) {
  const candidates = [];
  if (configuredPath) candidates.push(configuredPath);
  const root = workspaceRootForUri(uri);
  if (root) candidates.push(path.join(root, 'build', 'pie'));
  return [...new Set(candidates)];
}

function runExecutable(command, args, options) {
  return new Promise((resolve, reject) => {
    execFile(command, args, options, (error, stdout, stderr) => {
      if (error && error.code === 'ENOENT') reject(error);
      else resolve({
        error,
        stdout: stdout || '',
        stderr: stderr || '',
        exitCode: error && typeof error.code === 'number' ? error.code : 0,
        signal: error && error.signal ? error.signal : null,
      });
    });
  });
}

function diagnosticPositionForMessage(parsed, message) {
  const quoted = message.match(/'([A-Za-z_]\w*)'/)?.[1];
  if (quoted) {
    const token = parsed.tokens.find((item) => item.text === quoted && item.kind === 'identifier');
    if (token) return { line: token.line, character: token.column, length: token.text.length };
  }
  return { line: 0, character: 0, length: Math.max(1, parsed.lines[0]?.length || 1) };
}


function normalizeCompilerRange(parsed, entry) {
  const fileLine = entry.line ?? entry.startLine ?? entry.start?.line ?? entry.range?.start?.line;
  const fileColumn = entry.column ?? entry.character ?? entry.startColumn ?? entry.start?.character ?? entry.range?.start?.character;
  const line = Number.isInteger(fileLine) ? Math.max(0, fileLine > 0 ? fileLine - 1 : fileLine) : 0;
  const character = Number.isInteger(fileColumn) ? Math.max(0, fileColumn > 0 ? fileColumn - 1 : fileColumn) : 0;
  const endLineRaw = entry.endLine ?? entry.end?.line ?? entry.range?.end?.line;
  const endColumnRaw = entry.endColumn ?? entry.endCharacter ?? entry.end?.character ?? entry.range?.end?.character;
  const endLine = Number.isInteger(endLineRaw) ? Math.max(line, endLineRaw > 0 ? endLineRaw - 1 : endLineRaw) : line;
  const endCharacter = Number.isInteger(endColumnRaw) ? Math.max(character + 1, endColumnRaw > 0 ? endColumnRaw - 1 : endColumnRaw) : character + Math.max(1, Number(entry.length || 1));
  const maxLine = Math.max(0, parsed.lines.length - 1);
  const safeLine = Math.min(line, maxLine);
  const safeEndLine = Math.min(endLine, maxLine);
  return language.range(safeLine, character, safeEndLine, endCharacter);
}

function parseJsonCompilerDiagnostics(parsed, output) {
  const trimmed = output.trim();
  if (!trimmed) return undefined;
  let payload;
  try {
    payload = JSON.parse(trimmed);
  } catch {
    const lines = trimmed.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
    const diagnostics = [];
    for (const line of lines) {
      try {
        const item = JSON.parse(line);
        if (item) diagnostics.push(item);
      } catch {
        return undefined;
      }
    }
    payload = diagnostics;
  }
  const rawDiagnostics = Array.isArray(payload) ? payload : payload.diagnostics || payload.items || payload.errors;
  if (!Array.isArray(rawDiagnostics)) return undefined;
  return rawDiagnostics.map((entry) => ({
    range: normalizeCompilerRange(parsed, entry),
    severity: String(entry.severity || entry.level || '').toLowerCase() === 'warning' ? DiagnosticSeverity.Warning : DiagnosticSeverity.Error,
    source: entry.source || 'pie',
    code: entry.code || 'compiler',
    message: entry.message || entry.text || String(entry),
    relatedInformation: Array.isArray(entry.relatedInformation) ? entry.relatedInformation : undefined,
    data: entry.data,
  }));
}

function parseCompilerDiagnostics(parsed, output) {
  const diagnostics = [];
  for (const rawLine of output.split(/\r?\n/)) {
    const match = rawLine.match(/^pie:\s*(error|warning):\s*(?:(\d+):(\d+):\s*)?(.*)$/i);
    if (!match) continue;
    let message = match[4].trim();
    let fallback = diagnosticPositionForMessage(parsed, message);
    let line = match[2] ? Math.max(0, Number(match[2]) - 1) : fallback.line;
    let character = match[3] ? Math.max(0, Number(match[3]) - 1) : fallback.character;
    const embedded = message.match(/^(\d+):(\d+):\s*(.*)$/);
    if (!match[2] && embedded) {
      line = Math.max(0, Number(embedded[1]) - 1);
      character = Math.max(0, Number(embedded[2]) - 1);
      message = embedded[3].trim();
      fallback = diagnosticPositionForMessage(parsed, message);
    }
    diagnostics.push({
      range: language.range(line, character, line, character + fallback.length),
      severity: match[1].toLowerCase() === 'warning' ? DiagnosticSeverity.Warning : DiagnosticSeverity.Error,
      source: 'pie',
      code: 'compiler',
      message,
    });
  }
  return diagnostics;
}

function looksLikeCommandFailure(output) {
  return /\b(error|panic|assertion failed|assert failed|segmentation fault|runtime error|crash|failed)\b/i.test(output || '');
}

function processFailureDiagnostic(parsed, output, code) {
  const text = (output || '').trim();
  const firstLine = text.split(/\r?\n/).find((line) => line.trim()) || 'Pie command failed.';
  const position = diagnosticPositionForMessage(parsed, firstLine);
  return {
    range: language.range(position.line, position.character, position.line, position.character + position.length),
    severity: DiagnosticSeverity.Error,
    source: 'pie',
    code,
    message: firstLine,
    data: { output: text },
  };
}

async function runDocument(uri) {
  const parsed = parsedForUri(uri);
  const filePath = uriToPath(uri);
  if (!parsed || !filePath) return { ok: false, message: 'Pie run requires a saved Pie file.', output: '' };
  const settings = await getSettings(uri);
  const candidates = compilerCandidates(uri, settings.compiler?.path || 'pie');
  let result;
  let command;
  for (const candidate of candidates) {
    try {
      result = await runExecutable(candidate, ['run', filePath], {
        cwd: findPackageRoot(filePath),
        timeout: Number(settings.compiler?.timeout || 15000),
        maxBuffer: 8 * 1024 * 1024,
        windowsHide: true,
      });
      command = candidate;
      break;
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
    }
  }
  if (!result) {
    const key = candidates.join('|');
    if (!compilerWarnings.has(key)) {
      compilerWarnings.add(key);
      connection.window.showWarningMessage(`Pie compiler not found. Configure pie.compiler.path or build ${path.join(workspaceRootForUri(uri) || '.', 'build', 'pie')}.`);
    }
    return { ok: false, message: 'Pie compiler was not found.', output: '' };
  }
  const output = `${result.stdout || ''}${result.stderr || ''}`;
  const diagnostics = parseCompilerDiagnostics(parsed, output);
  const failedByDiagnostics = diagnostics.length > 0;
  const failedByOutput = looksLikeCommandFailure(output);
  const failedBySignal = Boolean(result.signal);
  const failed = failedByDiagnostics || failedByOutput || failedBySignal;
  if ((failedByOutput || failedBySignal) && diagnostics.length === 0) {
    diagnostics.push(processFailureDiagnostic(parsed, output || result.error?.message, 'runtime'));
  }
  if (failed) nativeDiagnostics.set(uri, diagnostics);
  else nativeDiagnostics.delete(uri);
  publishDiagnostics(uri);
  return {
    ok: !failed,
    message: failed
      ? `Pie run failed with ${diagnostics.length || 1} diagnostic${diagnostics.length === 1 ? '' : 's'}.`
      : `Pie run completed using ${command}${result.exitCode ? ` (exit ${result.exitCode})` : ''}.`,
    output,
  };
}

async function checkDocument(uri) {
  const parsed = parsedForUri(uri);
  const filePath = uriToPath(uri);
  if (!parsed || !filePath) return { ok: false, message: 'Compiler checks require a saved Pie file.' };
  const settings = await getSettings(uri);
  if (settings.compiler?.diagnostics === false) {
    nativeDiagnostics.delete(uri);
    publishDiagnostics(uri);
    return { ok: true, message: 'Compiler diagnostics are disabled.' };
  }
  const candidates = compilerCandidates(uri, settings.compiler?.path || 'pie');
  const preferJson = settings.compiler?.jsonDiagnostics !== false;
  let result;
  let command;
  let usedJson = false;
  for (const candidate of candidates) {
    try {
      const options = {
        cwd: findPackageRoot(filePath),
        timeout: Number(settings.compiler?.timeout || 15000),
        maxBuffer: 4 * 1024 * 1024,
        windowsHide: true,
      };
      if (preferJson) {
        const jsonResult = await runExecutable(candidate, ['check', '--json', filePath], options);
        const jsonOutput = `${jsonResult.stderr}
${jsonResult.stdout}`;
        const parsedJson = parseJsonCompilerDiagnostics(parsed, jsonOutput);
        if (parsedJson) {
          result = jsonResult;
          result.parsedDiagnostics = parsedJson;
          command = candidate;
          usedJson = true;
          break;
        }
      }
      result = await runExecutable(candidate, ['check', filePath], options);
      command = candidate;
      break;
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
    }
  }
  if (!result) {
    const key = candidates.join('|');
    if (!compilerWarnings.has(key)) {
      compilerWarnings.add(key);
      connection.window.showWarningMessage(`Pie compiler not found. Configure pie.compiler.path or build ${path.join(workspaceRootForUri(uri) || '.', 'build', 'pie')}.`);
    }
    nativeDiagnostics.delete(uri);
    publishDiagnostics(uri);
    return { ok: false, message: 'Pie compiler was not found; live language-server diagnostics remain active.' };
  }
  const compilerOutput = `${result.stderr}\n${result.stdout}`;
  const diagnostics = result.parsedDiagnostics || (result.error ? parseCompilerDiagnostics(parsed, compilerOutput) : []);
  if (result.error && diagnostics.length === 0) {
    diagnostics.push({
      range: language.range(0, 0, 0, Math.max(1, parsed.lines[0]?.length || 1)),
      severity: DiagnosticSeverity.Error,
      source: 'pie',
      code: 'compiler',
      message: compilerOutput.trim() || result.error.message,
    });
  }
  nativeDiagnostics.set(uri, diagnostics);
  publishDiagnostics(uri);
  return {
    ok: !result.error,
    message: result.error ? `Pie check failed with ${diagnostics.length} diagnostic${diagnostics.length === 1 ? '' : 's'}.` : `Pie check passed using ${command}${usedJson ? ' (JSON diagnostics)' : ''}.`,
  };
}

connection.onInitialize((params) => {
  hasConfigurationCapability = Boolean(params.capabilities.workspace?.configuration);
  hasWorkspaceFolderCapability = Boolean(params.capabilities.workspace?.workspaceFolders);
  if (params.workspaceFolders) {
    for (const folder of params.workspaceFolders) {
      const root = uriToPath(folder.uri);
      if (root) workspaceRoots.push(root);
    }
  } else if (params.rootUri) {
    const root = uriToPath(params.rootUri);
    if (root) workspaceRoots.push(root);
  } else if (params.rootPath) workspaceRoots.push(params.rootPath);

  return {
    capabilities: {
      textDocumentSync: {
        openClose: true,
        change: TextDocumentSyncKind.Incremental,
        save: { includeText: false },
      },
      completionProvider: { resolveProvider: true, triggerCharacters: ['.', '"', '/', ':', '<', '-', ' '] },
      hoverProvider: true,
      signatureHelpProvider: { triggerCharacters: ['(', ','], retriggerCharacters: [','] },
      definitionProvider: true,
      referencesProvider: true,
      documentHighlightProvider: true,
      renameProvider: { prepareProvider: true },
      documentSymbolProvider: true,
      workspaceSymbolProvider: true,
      documentFormattingProvider: true,
      foldingRangeProvider: true,
      documentLinkProvider: { resolveProvider: false },
      codeActionProvider: { codeActionKinds: [CodeActionKind.QuickFix, CodeActionKind.SourceOrganizeImports] },
      codeLensProvider: { resolveProvider: false },
      selectionRangeProvider: true,
      semanticTokensProvider: {
        legend: { tokenTypes: semanticTokenTypes, tokenModifiers: semanticTokenModifiers },
        full: true,
      },
      workspace: hasWorkspaceFolderCapability ? { workspaceFolders: { supported: true, changeNotifications: true } } : undefined,
    },
    serverInfo: { name: 'Pie Language Server', version: '1.0.0' },
  };
});

connection.onInitialized(() => {
  if (hasConfigurationCapability) connection.client.register(DidChangeConfigurationNotification.type, undefined);
  if (hasWorkspaceFolderCapability) {
    connection.workspace.onDidChangeWorkspaceFolders((event) => {
      for (const removed of event.removed) {
        const root = uriToPath(removed.uri);
        const indexOfRoot = workspaceRoots.indexOf(root);
        if (indexOfRoot >= 0) workspaceRoots.splice(indexOfRoot, 1);
      }
      for (const added of event.added) {
        const root = uriToPath(added.uri);
        if (root && !workspaceRoots.includes(root)) workspaceRoots.push(root);
      }
      indexWorkspaces();
    });
  }
  indexWorkspaces();
});

connection.onDidChangeConfiguration(() => {
  settingsCache.clear();
  for (const document of documents.all()) publishDiagnostics(document.uri);
});

documents.onDidOpen((event) => {
  index.set(event.document.uri, language.parseDocument(event.document.uri, event.document.getText()));
  publishDiagnostics(event.document.uri);
});

documents.onDidChangeContent((event) => {
  index.set(event.document.uri, language.parseDocument(event.document.uri, event.document.getText()));
  nativeDiagnostics.delete(event.document.uri);
  publishDiagnostics(event.document.uri);
  scheduleCompilerCheck(event.document.uri);
});

documents.onDidSave((event) => {
  checkDocument(event.document.uri).catch((error) => connection.console.error(error.stack || error.message));
});

documents.onDidClose((event) => {
  connection.sendDiagnostics({ uri: event.document.uri, diagnostics: [] });
  const filePath = uriToPath(event.document.uri);
  if (filePath && fs.existsSync(filePath)) indexFile(filePath);
  else index.delete(event.document.uri);
  nativeDiagnostics.delete(event.document.uri);
  if (pendingCompilerChecks.has(event.document.uri)) {
    clearTimeout(pendingCompilerChecks.get(event.document.uri));
    pendingCompilerChecks.delete(event.document.uri);
  }
  settingsCache.delete(event.document.uri);
});

connection.onDidChangeWatchedFiles((event) => {
  for (const change of event.changes) {
    const filePath = uriToPath(change.uri);
    if (!filePath || !filePath.endsWith('.pie') || documents.get(change.uri)) continue;
    if (change.type === FileChangeType.Deleted) index.delete(change.uri);
    else indexFile(filePath);
  }
});


function signatureParameterNames(signature, options = {}) {
  const open = signature.indexOf('(');
  const close = signature.indexOf(')', open + 1);
  if (open < 0 || close < 0) return [];
  const inside = signature.slice(open + 1, close).trim();
  if (!inside) return [];
  let params = language.splitTopLevel(inside)
    .map((param) => param.trim())
    .filter(Boolean)
    .map((param) => param.replace(/=.*/, '').trim())
    .filter((param) => !/^self\b/.test(param));
  if (options.dropFirst) params = params.slice(1);
  return params.map((param, index) => {
    const rawName = param.split(':')[0].trim().replace(/\.\.\.$/, '');
    const cleanName = rawName.replace(/[^A-Za-z_].*$/, '').trim();
    return cleanName || `value${index + 1}`;
  });
}

function callSnippet(name, signature, options = {}) {
  const params = signatureParameterNames(signature || `${name}()`, options);
  if (!params.length) return `${name}()`;
  return `${name}(${params.map((param, index) => `\${${index + 1}:${param}}`).join(', ')})`;
}

function typeDefaultValue(type) {
  const base = language.baseType(type || '');
  if (base === 'string') return '"\${1:value}"';
  if (base === 'char') return "'\${1:a}'";
  if (base === 'bool') return '\${1|true,false|}';
  if (base === 'float') return '\${1:0.0}';
  if (base === 'byte') return '\${1:0}';
  if (base === 'list') return '[\${1}]';
  if (base === 'map') return '{\${1}}';
  if (base === 'Option') return '\${1|None,Some(value)|}';
  if (base === 'Result') return '\${1|Ok(value),Err(error)|}';
  return '\${1:0}';
}

function symbolsMatchingType(parsed, type, line) {
  const base = language.baseType(type || '');
  if (!base) return [];
  return language.visibleSymbols(parsed, line)
    .filter((symbol) => symbol.type && language.baseType(symbol.type) === base)
    .map((symbol) => symbol.name);
}

function smartBindingCompletions(parsed, params) {
  const lineText = parsed.lines[params.position.line] || '';
  const prefix = lineText.slice(0, params.position.character);
  const items = [];
  function edit(start, newText) {
    return { range: language.range(params.position.line, start, params.position.line, params.position.character), newText };
  }
  function snippet(label, detail, start, newText, sortText, kind = CompletionItemKind.Snippet) {
    items.push({
      label,
      kind,
      detail,
      sortText: sortText || label,
      insertTextFormat: InsertTextFormat.Snippet,
      textEdit: edit(start, newText),
    });
  }
  const bare = prefix.match(/^(\s*)((?:let\s+)?(?:mut\s+)?)([A-Za-z_]\w*)$/);
  if (bare) {
    const start = bare[1].length;
    const head = `${bare[2] || ''}${bare[3]}`;
    snippet(`${bare[3]}: type -> value`, 'Create a typed binding', start, `${head}: \${1:int} -> \${2:value}`, '000_binding');
    if (!/\bmut\b/.test(head)) snippet(`mut ${bare[3]}: type -> value`, 'Create a mutable typed binding', start, `mut ${bare[3]}: \${1:int} -> \${2:value}`, '001_mut_binding');
    const symbol = language.visibleSymbols(parsed, params.position.line).find((candidate) => candidate.name === bare[3] && candidate.kind === SymbolKind.Variable);
    if (symbol) snippet(`${bare[3]} <- value`, 'Assign to this variable', start, `${bare[3]} <- \${1:value}`, '002_assign');
  }
  const typed = prefix.match(/^(\s*)((?:let\s+)?(?:mut\s+)?[A-Za-z_]\w*)\s*:\s*([^<-]*)$/);
  if (typed) {
    const start = typed[1].length;
    const head = typed[2];
    const currentType = typed[3].trim();
    if (currentType) snippet('-> value', 'Initialize this binding', start, `${head}: ${currentType} -> ${typeDefaultValue(currentType)}`, '000_arrow');
    else {
      for (const type of ['int', 'string', 'bool', 'float', 'byte', 'char', 'list(int)', 'map']) {
        snippet(`${type} -> value`, 'Choose a type and initialize', start, `${head}: ${type} -> ${typeDefaultValue(type)}`, `00_${type}`);
      }
    }
  }
  const initializing = prefix.match(/^(\s*)((?:let\s+)?(?:mut\s+)?[A-Za-z_]\w*)\s*:\s*([^<-]+?)\s*->\s*$/);
  if (initializing) {
    const type = initializing[3].trim();
    const start = params.position.character;
    snippet(`default ${language.baseType(type) || 'value'}`, 'Insert a default value for this type', start, typeDefaultValue(type), '000_default_value');
    for (const name of symbolsMatchingType(parsed, type, params.position.line).slice(0, 8)) {
      snippet(name, `Use visible ${type} value`, start, name, `001_${name}`, CompletionItemKind.Variable);
    }
  }
  const assignStart = prefix.match(/^(\s*)([A-Za-z_]\w*)\s*<$/);
  if (assignStart) {
    const start = prefix.lastIndexOf('<');
    snippet('<- value', 'Complete reassignment arrow', start, `<- \${1:value}`, '000_reassign_arrow');
  }
  const returnStart = prefix.match(/^(.*)-$/);
  if (returnStart && /\bfn\b/.test(prefix)) {
    const start = prefix.lastIndexOf('-');
    snippet('-> return type', 'Complete function return type arrow', start, `-> \${1:int}`, '000_return_arrow');
  }
  const returnValue = prefix.match(/^(\s*)return\s+$/);
  if (returnValue) {
    const fn = parsed.functions.find((symbol) => symbol.range.start.line <= params.position.line && symbol.range.end.line >= params.position.line);
    if (fn?.type) snippet(`return ${language.baseType(fn.type)}`, 'Return a default value for this function', params.position.character, typeDefaultValue(fn.type), '000_return_value');
  }
  const emptyLine = prefix.match(/^\s*$/);
  if (emptyLine) {
    const start = params.position.character;
    snippet('end', 'Close the current block', start, 'end', '900_end');
    snippet('println', 'Print a value', start, 'println(\${1:value})', '901_println');
    snippet('return 0', 'Return success from main', start, 'return 0', '902_return');
  }
  return items;
}

connection.onCompletion(async (params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return [];
  const settings = await getSettings(params.textDocument.uri);
  const lineText = parsed.lines[params.position.line] || '';
  const prefix = lineText.slice(0, params.position.character);
  const requireMatch = prefix.match(/^\s*require\s+"([^"]*)$/);
  if (requireMatch) {
    const localPaths = allParsedDocuments()
      .map((item) => uriToPath(item.uri))
      .filter(Boolean)
      .map((filePath) => {
        const root = workspaceRootForUri(params.textDocument.uri);
        const sourceRoot = root ? path.join(root, 'src') : '';
        return sourceRoot && filePath.startsWith(sourceRoot)
          ? path.relative(sourceRoot, filePath).replace(/\\/g, '/').replace(/\.pie$/, '')
          : undefined;
      })
      .filter(Boolean);
    return [...new Set([...language.STANDARD_MODULES, ...localPaths])].map((name) => ({
      label: name,
      kind: CompletionItemKind.Module,
      detail: language.STANDARD_MODULES.includes(name) ? 'Pie standard module' : 'Workspace module',
      insertText: name,
    }));
  }

  const memberMatch = prefix.match(/([A-Za-z_]\w*)\.([A-Za-z_]\w*)?$/);
  if (memberMatch) {
    if (memberMatch[1] === 'thread') {
      return language.THREAD_OPERATIONS.map((operation) => ({
        label: operation.name,
        kind: CompletionItemKind.Function,
        detail: operation.signature,
        documentation: operation.documentation,
        insertTextFormat: InsertTextFormat.Snippet,
        insertText: callSnippet(operation.name, operation.signature),
      }));
    }
    const owner = resolveSimpleSymbol(parsed, memberMatch[1], params.position.line);
    const ownerType = owner ? language.baseType(owner.type) : memberMatch[1];
    const builtInMethods = language.METHODS[ownerType] || [];
    const customMembers = [
      ...childrenForType(ownerType).map((symbol) => ({
        label: symbol.name,
        kind: symbol.kind === SymbolKind.EnumMember ? CompletionItemKind.EnumMember : CompletionItemKind.Field,
        detail: symbol.signature || symbol.detail,
        data: { symbolId: symbol.id },
      })),
      ...methodsForReceiver(ownerType).map((symbol) => ({
        label: symbol.name,
        kind: CompletionItemKind.Method,
        detail: symbol.signature,
        insertTextFormat: InsertTextFormat.Snippet,
        insertText: callSnippet(symbol.name, symbol.signature),
        data: { symbolId: symbol.id },
      })),
    ];
    return [
      ...builtInMethods.map(([name, signature]) => ({
        label: name,
        kind: CompletionItemKind.Method,
        detail: signature,
        insertTextFormat: InsertTextFormat.Snippet,
        insertText: callSnippet(name, signature),
      })),
      ...customMembers,
    ];
  }

  const smartItems = smartBindingCompletions(parsed, params);
  const items = [];
  const seen = new Set();
  for (const item of smartItems) items.push(item);
  function add(item) {
    const key = `${item.label}:${item.kind}`;
    if (!seen.has(key)) {
      seen.add(key);
      items.push(item);
    }
  }
  for (const keyword of language.KEYWORDS) add({ label: keyword, kind: CompletionItemKind.Keyword });
  for (const type of language.BUILTIN_TYPES) add({ label: type, kind: CompletionItemKind.TypeParameter, detail: 'Built-in Pie type' });
  for (const builtin of language.BUILTINS) add({
    label: builtin.name,
    kind: builtin.kind === SymbolKind.EnumMember ? CompletionItemKind.EnumMember : CompletionItemKind.Function,
    detail: builtin.signature,
    documentation: builtin.documentation,
    insertTextFormat: builtin.kind === SymbolKind.EnumMember ? undefined : InsertTextFormat.Snippet,
    insertText: builtin.kind === SymbolKind.EnumMember ? undefined : callSnippet(builtin.name, builtin.signature),
    data: { builtin: builtin.name },
  });
  for (const symbol of language.visibleSymbols(parsed, params.position.line)) add({
    label: symbol.name,
    kind: symbol.data.parameter ? CompletionItemKind.Variable : CompletionItemKind.Variable,
    detail: symbol.detail,
    data: { symbolId: symbol.id },
  });
  const completionDocuments = settings.completion?.autoImports === false ? [parsed] : allParsedDocuments();
  for (const document of completionDocuments) {
    for (const symbol of document.globals) {
      add({
        label: symbol.name,
        kind: symbol.kind === SymbolKind.Function ? CompletionItemKind.Function
          : [SymbolKind.Struct, SymbolKind.Enum, SymbolKind.TypeParameter].includes(symbol.kind) ? CompletionItemKind.Class
            : CompletionItemKind.Variable,
        detail: symbol.signature || symbol.detail,
        insertTextFormat: symbol.kind === SymbolKind.Function ? InsertTextFormat.Snippet : undefined,
        insertText: symbol.kind === SymbolKind.Function ? callSnippet(symbol.name, symbol.signature) : undefined,
        data: { symbolId: symbol.id },
      });
    }
  }
  add({
    label: 'fn', kind: CompletionItemKind.Snippet, detail: 'Function declaration', insertTextFormat: InsertTextFormat.Snippet,
    insertText: 'fn ${1:name}(${2}) -> ${3:void}:\n    ${4:pass}\nend',
  });
  add({
    label: 'match', kind: CompletionItemKind.Snippet, detail: 'Match expression', insertTextFormat: InsertTextFormat.Snippet,
    insertText: 'match \${1:value}:\n    case ${2:Type.Variant}:\n        ${3:pass}\n    case _:\n        ${4:pass}\nend',
  });
  add({
    label: 'var', kind: CompletionItemKind.Snippet, detail: 'Typed binding', insertTextFormat: InsertTextFormat.Snippet,
    insertText: '${1:name}: ${2:int} -> ${3:value}',
  });
  add({
    label: 'mut var', kind: CompletionItemKind.Snippet, detail: 'Mutable typed binding', insertTextFormat: InsertTextFormat.Snippet,
    insertText: 'mut ${1:name}: ${2:int} -> ${3:value}',
  });
  add({
    label: 'assign', kind: CompletionItemKind.Snippet, detail: 'Reassign a mutable binding', insertTextFormat: InsertTextFormat.Snippet,
    insertText: '${1:name} <- \${2:value}',
  });
  return items;
});

connection.onCompletionResolve((item) => {
  if (item.data?.builtin) {
    const builtin = language.BUILTINS.find((candidate) => candidate.name === item.data.builtin);
    if (builtin) item.documentation = { kind: MarkupKind.Markdown, value: builtin.documentation };
  } else if (item.data?.symbolId) {
    const symbol = allParsedDocuments().flatMap((parsed) => parsed.symbols).find((candidate) => candidate.id === item.data.symbolId);
    if (symbol) {
      item.detail = symbol.signature || symbol.detail;
      const filename = path.basename(uriToPath(symbol.uri) || symbol.uri);
      item.documentation = { kind: MarkupKind.Markdown, value: symbol.documentation || `Declared in ${filename}.` };
    }
  }
  return item;
});

connection.onHover((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return null;
  const token = language.tokenAt(parsed, params.position);
  if (!token) return null;
  const symbol = resolveSymbol(parsed, token, params.position.line);
  if (symbol) {
    const signature = symbol.signature || `${symbol.name}${symbol.detail ? `: ${symbol.detail}` : ''}`;
    const documentation = symbol.documentation ? `\n\n${symbol.documentation}` : '';
    return { contents: { kind: MarkupKind.Markdown, value: `\`\`\`pie\n${signature}\n\`\`\`${documentation}` }, range: language.range(token.line, token.column, token.endLine, token.endColumn) };
  }
  const builtin = language.BUILTINS.find((candidate) => candidate.name === token.text);
  if (builtin) return { contents: { kind: MarkupKind.Markdown, value: `\`\`\`pie\n${builtin.signature}\n\`\`\`\n\n${builtin.documentation}` } };
  if (language.BUILTIN_TYPES.has(token.text)) return { contents: { kind: MarkupKind.Markdown, value: `**${token.text}** — built-in Pie type` } };
  return null;
});

connection.onDefinition((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return null;
  const token = language.tokenAt(parsed, params.position);
  if (!token) return null;
  const symbol = resolveSymbol(parsed, token, params.position.line);
  return symbol ? symbolLocation(symbol) : null;
});

function referencesForSymbol(parsed, symbol, includeDeclaration) {
  const targets = [];
  const localOnly = parsed.locals.includes(symbol);
  const sources = localOnly ? [parsed] : allParsedDocuments();
  for (const source of sources) {
    for (const token of source.tokens) {
      if (token.text !== symbol.name || !['identifier', 'keyword'].includes(token.kind)) continue;
      if (localOnly && (token.line < symbol.scopeStart || token.line > symbol.scopeEnd)) continue;
      const resolved = resolveSymbol(source, token, token.line);
      if (resolved?.id !== symbol.id) continue;
      const declaration = symbolDeclaredAt(source, token);
      if (!includeDeclaration && declaration?.id === symbol.id) continue;
      targets.push(Location.create(source.uri, language.range(token.line, token.column, token.endLine, token.endColumn)));
    }
  }
  return targets;
}

connection.onReferences((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return [];
  const token = language.tokenAt(parsed, params.position);
  const symbol = token && resolveSymbol(parsed, token, params.position.line);
  return symbol ? referencesForSymbol(parsed, symbol, params.context.includeDeclaration) : [];
});

connection.onDocumentHighlight((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return [];
  const token = language.tokenAt(parsed, params.position);
  const symbol = token && resolveSymbol(parsed, token, params.position.line);
  if (!symbol) return [];
  return referencesForSymbol(parsed, symbol, true)
    .filter((location) => location.uri === parsed.uri)
    .map((location) => ({ range: location.range, kind: DocumentHighlightKind.Read }));
});

connection.onPrepareRename((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  const token = parsed && language.tokenAt(parsed, params.position);
  if (!token || language.KEYWORDS.has(token.text) || language.BUILTIN_TYPES.has(token.text)) return null;
  const symbol = resolveSymbol(parsed, token, params.position.line);
  return symbol ? { range: language.range(token.line, token.column, token.endLine, token.endColumn), placeholder: token.text } : null;
});

connection.onRenameRequest((params) => {
  if (!/^[A-Za-z_]\w*$/.test(params.newName) || language.KEYWORDS.has(params.newName)) return null;
  const parsed = parsedForUri(params.textDocument.uri);
  const token = parsed && language.tokenAt(parsed, params.position);
  const symbol = token && resolveSymbol(parsed, token, params.position.line);
  if (!symbol) return null;
  const changes = {};
  for (const location of referencesForSymbol(parsed, symbol, true)) {
    if (!changes[location.uri]) changes[location.uri] = [];
    changes[location.uri].push({ range: location.range, newText: params.newName });
  }
  return { changes };
});

connection.onSignatureHelp((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return null;
  const before = parsed.lines.slice(0, params.position.line).join('\n')
    + (params.position.line > 0 ? '\n' : '')
    + (parsed.lines[params.position.line] || '').slice(0, params.position.character);
  let depth = 0;
  let open = -1;
  for (let index = before.length - 1; index >= 0; index -= 1) {
    if (before[index] === ')') depth += 1;
    else if (before[index] === '(') {
      if (depth === 0) { open = index; break; }
      depth -= 1;
    }
  }
  if (open < 0) return null;
  const nameMatch = before.slice(0, open).match(/((?:[A-Za-z_]\w*\.)?[A-Za-z_]\w*)\s*$/);
  if (!nameMatch) return null;
  const callName = nameMatch[1];
  const shortName = callName.split('.').at(-1);
  const candidates = [
    ...allParsedDocuments().flatMap((document) => document.functions.filter((symbol) => symbol.name === shortName)),
    ...language.BUILTINS.filter((builtin) => builtin.name === shortName),
    ...language.THREAD_OPERATIONS.filter((operation) => operation.name === shortName),
  ];
  if (!candidates.length) return null;
  const argumentText = before.slice(open + 1);
  const activeParameter = Math.max(0, language.splitTopLevel(argumentText).length - (argumentText.trim() ? 1 : 0));
  return {
    activeSignature: 0,
    activeParameter,
    signatures: candidates.map((candidate) => {
      const label = candidate.signature || candidate.name;
      const paramsText = label.match(/\((.*)\)/)?.[1] || '';
      return {
        label,
        documentation: candidate.documentation || '',
        parameters: language.splitTopLevel(paramsText).map((parameter) => ({ label: parameter })),
      };
    }),
  };
});

connection.onDocumentSymbol((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  return parsed ? language.documentSymbols(parsed) : [];
});

connection.onWorkspaceSymbol((params) => {
  const query = params.query.toLowerCase();
  return allParsedDocuments().flatMap((parsed) => parsed.globals)
    .filter((symbol) => !query || symbol.name.toLowerCase().includes(query))
    .slice(0, 500)
    .map((symbol) => ({ name: symbol.name, kind: symbol.kind, location: symbolLocation(symbol), containerName: symbol.containerName }));
});

connection.onDocumentFormatting(async (params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return [];
  const settings = await getSettings(params.textDocument.uri);
  const formatted = language.formatDocument(parsed.text, { ...params.options, indentSize: settings.formatting?.indentSize || 4 });
  if (formatted === parsed.text) return [];
  const lastLine = parsed.lines.length - 1;
  return [{ range: language.range(0, 0, lastLine, parsed.lines[lastLine].length), newText: formatted }];
});

connection.onFoldingRanges((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return [];
  const ranges = [...parsed.blockEnds.entries()]
    .filter(([start, end]) => end > start)
    .map(([startLine, endLine]) => ({ startLine, endLine: Math.max(startLine, endLine - 1), kind: 'region' }));
  for (const token of parsed.tokens) {
    if (token.kind === 'comment' && token.endLine > token.line) ranges.push({ startLine: token.line, endLine: token.endLine, kind: 'comment' });
  }
  return ranges;
});

connection.onDocumentLinks((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return [];
  return parsed.requires.map((required) => ({ range: required.range, target: findModuleTarget(parsed, required.path), tooltip: `Open module ${required.path}` }))
    .filter((link) => link.target);
});

function bindingTypeRange(lineText, line) {
  const match = lineText.match(/^(\s*(?:let\s+)?(?:mut\s+)?[A-Za-z_]\w*\s*:\s*)([^<-=]+?)(\s*(?:->|<-|=))/);
  if (!match) return null;
  const start = match[1].length;
  return language.range(line, start, line, start + match[2].length);
}

function functionReturnTypeRange(lineText, line) {
  const match = lineText.match(/(->\s*)([A-Za-z_][A-Za-z0-9_<>,?&*\s()]*)\s*:/);
  if (!match) return null;
  const start = lineText.indexOf(match[2], lineText.indexOf(match[1]));
  return language.range(line, start, line, start + match[2].trim().length);
}

connection.onCodeAction((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return [];
  const actions = [];
  for (const item of params.context.diagnostics) {
    if (item.code === 'immutable-assignment' && item.data?.name) {
      const declaration = language.visibleSymbols(parsed, item.range.start.line).find((symbol) => symbol.name === item.data.name);
      if (declaration) {
        const lineText = parsed.lines[declaration.selectionRange.start.line] || '';
        const insertAt = lineText.match(/^\s*let\s+/)?.[0].length ?? lineText.search(/\S|$/);
        actions.push({
          title: `Make '${item.data.name}' mutable`,
          kind: CodeActionKind.QuickFix,
          diagnostics: [item],
          isPreferred: true,
          edit: { changes: { [parsed.uri]: [{ range: language.range(declaration.selectionRange.start.line, insertAt, declaration.selectionRange.start.line, insertAt), newText: 'mut ' }] } },
        });
      }
    }
    if (item.code === 'unsafe-required') {
      const line = item.range.start.line;
      const lineText = parsed.lines[line] || '';
      const indent = lineText.match(/^\s*/)?.[0] || '';
      actions.push({
        title: 'Wrap line in unsafe block',
        kind: CodeActionKind.QuickFix,
        diagnostics: [item],
        edit: { changes: { [parsed.uri]: [{ range: language.range(line, 0, line + 1, 0), newText: `${indent}unsafe:
${indent}    ${lineText.trimStart()}
${indent}end
` }] } },
      });
    }
    if (item.code === 'type-mismatch' && item.data?.actual) {
      const line = item.range.start.line;
      const range = bindingTypeRange(parsed.lines[line] || '', line);
      if (range) actions.push({
        title: `Change type to '${item.data.actual}'`,
        kind: CodeActionKind.QuickFix,
        diagnostics: [item],
        edit: { changes: { [parsed.uri]: [{ range, newText: item.data.actual }] } },
      });
    }
    if (item.code === 'return-type-mismatch' && item.data?.actual) {
      const fn = parsed.functions.find((symbol) => symbol.range.start.line <= item.range.start.line && symbol.range.end.line >= item.range.start.line);
      const line = fn?.range.start.line;
      const range = line === undefined ? null : functionReturnTypeRange(parsed.lines[line] || '', line);
      if (range) actions.push({
        title: `Change return type to '${item.data.actual}'`,
        kind: CodeActionKind.QuickFix,
        diagnostics: [item],
        edit: { changes: { [parsed.uri]: [{ range, newText: item.data.actual }] } },
      });
    }
    if (item.code === 'undefined-symbol' && item.data?.name) {
      const line = item.range.start.line;
      const indent = (parsed.lines[line] || '').match(/^\s*/)?.[0] || '';
      actions.push({
        title: `Create local '${item.data.name}'`,
        kind: CodeActionKind.QuickFix,
        diagnostics: [item],
        edit: { changes: { [parsed.uri]: [{ range: language.range(line, 0, line, 0), newText: `${indent}${item.data.name}: int -> 0\n` }] } },
      });
    }
    if (item.code === 'missing-end') {
      const insertLine = parsed.lines.length - 1;
      const insertCharacter = parsed.lines[insertLine].length;
      const insertion = insertCharacter === 0 ? 'end\n' : '\nend';
      actions.push({
        title: "Insert missing 'end'",
        kind: CodeActionKind.QuickFix,
        diagnostics: [item],
        isPreferred: true,
        edit: { changes: { [parsed.uri]: [{ range: language.range(insertLine, insertCharacter, insertLine, insertCharacter), newText: insertion }] } },
      });
    }
  }
  if (parsed.requires.length > 1 && params.context.only?.some((kind) => kind.startsWith(CodeActionKind.SourceOrganizeImports))) {
    const sorted = [...parsed.requires].sort((left, right) => left.path.localeCompare(right.path));
    const firstLine = Math.min(...parsed.requires.map((item) => item.range.start.line));
    const lastLine = Math.max(...parsed.requires.map((item) => item.range.start.line));
    actions.push({
      title: 'Sort Pie requires',
      kind: CodeActionKind.SourceOrganizeImports,
      edit: { changes: { [parsed.uri]: [{ range: language.range(firstLine, 0, lastLine + 1, 0), newText: `${sorted.map((item) => `require "${item.path}"`).join('\n')}\n` }] } },
    });
  }
  return actions;
});

connection.onCodeLens(async (params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return [];
  const settings = await getSettings(params.textDocument.uri);
  if (settings.codeLens?.enabled === false) return [];
  const lenses = [];
  for (const symbol of parsed.functions) {
    const references = referencesForSymbol(parsed, symbol, false).length;
    lenses.push({
      range: symbol.selectionRange,
      command: { title: `${references} reference${references === 1 ? '' : 's'}`, command: 'pie.showReferences', arguments: [symbol.uri, symbol.selectionRange.start] },
    });
    if (symbol.name === 'main' && !symbol.receiver) {
      lenses.unshift({ range: symbol.selectionRange, command: { title: '$(play) Run Pie', command: 'pie.runFile', arguments: [symbol.uri] } });
      lenses.unshift({ range: symbol.selectionRange, command: { title: '$(check) Check Pie', command: 'pie.checkFile', arguments: [symbol.uri] } });
    }
  }
  return lenses;
});

connection.onSelectionRanges((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return [];
  const documentRange = language.range(0, 0, parsed.lines.length - 1, parsed.lines.at(-1)?.length || 0);
  return params.positions.map((target) => {
    const token = language.tokenAt(parsed, target);
    const containing = parsed.symbols
      .filter((symbol) => symbol.range.start.line <= target.line && symbol.range.end.line >= target.line)
      .sort((left, right) => (left.range.end.line - left.range.start.line) - (right.range.end.line - right.range.start.line))[0];
    const parent = containing ? { range: containing.range, parent: { range: documentRange } } : { range: documentRange };
    return token ? { range: language.range(token.line, token.column, token.endLine, token.endColumn), parent } : parent;
  });
});

function semanticTypeForSymbol(symbol) {
  if (!symbol) return 'variable';
  if (symbol.data.parameter) return 'parameter';
  switch (symbol.kind) {
    case SymbolKind.Struct: return 'struct';
    case SymbolKind.Enum: return 'enum';
    case SymbolKind.TypeParameter: return symbol.data.typeParameter ? 'typeParameter' : 'type';
    case SymbolKind.Function: return 'function';
    case SymbolKind.Method: return 'method';
    case SymbolKind.Field: return 'property';
    case SymbolKind.EnumMember: return 'enumMember';
    case SymbolKind.Package: return 'namespace';
    default: return 'variable';
  }
}

connection.languages.semanticTokens.on((params) => {
  const parsed = parsedForUri(params.textDocument.uri);
  if (!parsed) return { data: [] };
  const typeNames = new Set([...language.BUILTIN_TYPES, ...allParsedDocuments().flatMap((item) => item.types.map((symbol) => symbol.name))]);
  const entries = [];
  function add(line, start, length, type, modifiers = []) {
    if (length <= 0 || !semanticTokenTypes.includes(type)) return;
    entries.push({ line, start, length, type: semanticTokenTypes.indexOf(type), modifiers: modifiers.reduce((bits, modifier) => bits | (1 << semanticTokenModifiers.indexOf(modifier)), 0) });
  }
  for (const token of parsed.tokens) {
    if (token.endLine > token.line && ['comment', 'string'].includes(token.kind)) {
      const segments = token.text.split('\n');
      segments.forEach((segment, offset) => add(token.line + offset, offset === 0 ? token.column : 0, segment.length, token.kind));
      continue;
    }
    if (['comment', 'string', 'number', 'operator'].includes(token.kind)) {
      add(token.line, token.column, token.text.length, token.kind);
      continue;
    }
    if (token.kind === 'character') {
      add(token.line, token.column, token.text.length, 'string');
      continue;
    }
    if (token.kind === 'keyword') {
      if (language.BUILTIN_TYPES.has(token.text)) add(token.line, token.column, token.text.length, 'type', ['defaultLibrary']);
      else add(token.line, token.column, token.text.length, 'keyword');
      continue;
    }
    if (token.kind !== 'identifier') continue;
    const declared = symbolDeclaredAt(parsed, token);
    if (declared) {
      add(token.line, token.column, token.text.length, semanticTypeForSymbol(declared), ['declaration']);
      continue;
    }
    if (typeNames.has(token.text)) {
      add(token.line, token.column, token.text.length, 'type');
      continue;
    }
    if (language.TYPE_CONSTRAINTS.has(token.text)) {
      add(token.line, token.column, token.text.length, 'interface');
      continue;
    }
    const resolved = resolveSymbol(parsed, token, token.line);
    if (resolved) {
      add(token.line, token.column, token.text.length, semanticTypeForSymbol(resolved), resolved.kind === SymbolKind.Constant ? ['readonly'] : []);
      continue;
    }
    const previous = language.previousToken(parsed, token);
    const next = language.nextToken(parsed, token);
    if (previous?.text === '.') add(token.line, token.column, token.text.length, next?.text === '(' ? 'method' : 'property');
    else if (next?.text === '(') add(token.line, token.column, token.text.length, 'function');
  }
  entries.sort((left, right) => left.line - right.line || left.start - right.start);
  const data = [];
  let previousLine = 0;
  let previousStart = 0;
  for (const entry of entries) {
    const deltaLine = entry.line - previousLine;
    const deltaStart = deltaLine === 0 ? entry.start - previousStart : entry.start;
    data.push(deltaLine, deltaStart, entry.length, entry.type, entry.modifiers);
    previousLine = entry.line;
    previousStart = entry.start;
  }
  return { data };
});


async function compilerInfoRequest(uri, args) {
  const settings = await getSettings(uri || '');
  const candidates = compilerCandidates(uri || '', settings.compiler?.path || 'pie');
  for (const candidate of candidates) {
    try {
      const result = await runExecutable(candidate, args, {
        cwd: uri ? findPackageRoot(uriToPath(uri) || '.') : workspaceRoots[0],
        timeout: Number(settings.compiler?.timeout || 15000),
        maxBuffer: 1024 * 1024,
        windowsHide: true,
      });
      return { command: candidate, result };
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
    }
  }
  return undefined;
}

connection.onRequest('pie/compilerVersion', async (params) => {
  const info = await compilerInfoRequest(params.uri, ['--version']);
  if (!info) return { ok: false, message: 'Pie compiler was not found.', output: '' };
  const output = `${info.result.stdout}${info.result.stderr}`.trim();
  return { ok: !info.result.error, message: output || `Pie compiler found at ${info.command}.`, output };
});

connection.onRequest('pie/resolveCompiler', async (params) => {
  const settings = await getSettings(params.uri || '');
  const candidates = compilerCandidates(params.uri || '', settings.compiler?.path || 'pie');
  const found = await compilerInfoRequest(params.uri, ['--version']);
  return found
    ? { ok: true, message: `Using Pie compiler: ${found.command}` }
    : { ok: false, message: `Pie compiler not found. Tried: ${candidates.join(', ')}` };
});

connection.onRequest('pie/packageInfo', async (params) => {
  const root = params.uri ? findPackageRoot(uriToPath(params.uri) || '') : workspaceRoots[0];
  const toml = root ? path.join(root, 'pie.toml') : '';
  if (!toml || !fs.existsSync(toml)) return { ok: false, message: 'No pie.toml package file found.', output: '' };
  const output = fs.readFileSync(toml, 'utf8');
  return { ok: true, message: `Package file: ${toml}`, output };
});

connection.onRequest('pie/checkDocument', (params) => checkDocument(params.uri));
connection.onRequest('pie/runDocument', (params) => runDocument(params.uri));

documents.listen(connection);
connection.listen();
