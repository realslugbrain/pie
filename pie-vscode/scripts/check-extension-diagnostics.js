#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const cp = require('child_process');

const STANDARD_MODULES = new Set(['io', 'json', 'print', 'assert', 'threads', 'format']);

function parseArgs(argv) {
  const args = {
    root: null,
    cmake: null,
    pie: null,
    expectedOnly: false,
    fixtureSweep: false,
    includePie: true,
    liveOnly: false,
    json: false,
    verbose: false,
    strict: false,
    failOnPieMismatch: true,
    timeout: 15000,
  };
  for (let i = 2; i < argv.length; i += 1) {
    const item = argv[i];
    if (item === '--root') args.root = argv[++i];
    else if (item === '--cmake') args.cmake = argv[++i];
    else if (item === '--pie') args.pie = argv[++i];
    else if (item === '--expected-only') args.expectedOnly = true;
    else if (item === '--fixture-sweep') args.fixtureSweep = true;
    else if (item === '--all') { args.expectedOnly = false; args.fixtureSweep = true; }
    else if (item === '--no-compiler' || item === '--no-pie') args.includePie = false;
    else if (item === '--live-only') { args.liveOnly = true; args.includePie = false; }
    else if (item === '--json') args.json = true;
    else if (item === '--verbose' || item === '-v') args.verbose = true;
    else if (item === '--strict') args.strict = true;
    else if (item === '--no-fail-on-pie-mismatch') args.failOnPieMismatch = false;
    else if (item === '--timeout') args.timeout = Number(argv[++i] || 15000);
    else if (item === '--help' || item === '-h') { printHelp(); process.exit(0); }
    else die(`Unknown argument: ${item}`);
  }
  return args;
}

function printHelp() {
  console.log(`Pie extension diagnostic tester

Usage:
  node scripts/check-extension-diagnostics.js --root .. --strict

Options:
  --root <path>          Pie repo root
  --cmake <path>         CMakeLists.txt path
  --pie <path>           Pie CLI path
  --expected-only        Only check expected failing tests from CMake
  --fixture-sweep        Run every tests/fixtures/**/*.pie file too
  --all                  Same as fixture sweep with all CMake Pie tests
  --no-pie               Skip Pie CLI commands
  --live-only            Only run extension parser and live analyzer
  --json                 Print JSON
  --verbose, -v          Show diagnostics and output
  --strict               Exit non-zero on extension, Pie, or output mismatch
  --no-fail-on-pie-mismatch
                         Do not fail strict mode on Pie command mismatch
  --timeout <ms>         Pie command timeout
  --help, -h             Show help`.trim());
}

function die(message) {
  console.error(`error: ${message}`);
  process.exit(2);
}

function exists(filePath) {
  try { return fs.existsSync(filePath); } catch { return false; }
}

function findUp(startDir, name) {
  let current = path.resolve(startDir);
  for (;;) {
    const candidate = path.join(current, name);
    if (exists(candidate)) return candidate;
    const parent = path.dirname(current);
    if (parent === current) return null;
    current = parent;
  }
}

function detectRoot(userRoot) {
  if (userRoot) return path.resolve(userRoot);
  const cmake = findUp(process.cwd(), 'CMakeLists.txt');
  if (cmake) return path.dirname(cmake);
  die('Could not find CMakeLists.txt. Run from the repo or pass --root <path>.');
}

function loadExtensionModules(root) {
  const candidates = [path.join(root, 'pie-vscode'), process.cwd(), path.join(process.cwd(), 'pie-vscode'), path.resolve(__dirname, '..')];
  const extensionRoot = candidates.find((dir) => exists(path.join(dir, 'server', 'language.js')));
  if (!extensionRoot) die('Could not find pie-vscode/server/language.js.');
  let language;
  let analyzer = null;
  try { language = require(path.join(extensionRoot, 'server', 'language.js')); } catch (error) { die(error.stack || error.message); }
  try { analyzer = require(path.join(extensionRoot, 'server', 'analyzer.js')); } catch { analyzer = null; }
  return { extensionRoot, language, analyzer };
}

function stripCmakeComments(text) {
  return text.replace(/#[^\n]*/g, '');
}

function readParenBlock(text, startIndex) {
  const open = text.indexOf('(', startIndex);
  if (open < 0) return null;
  let depth = 0;
  let quote = null;
  let escaped = false;
  for (let i = open; i < text.length; i += 1) {
    const ch = text[i];
    if (quote) {
      if (escaped) escaped = false;
      else if (ch === '\\') escaped = true;
      else if (ch === quote) quote = null;
      continue;
    }
    if (ch === '"' || ch === "'") { quote = ch; continue; }
    if (ch === '(') depth += 1;
    else if (ch === ')') {
      depth -= 1;
      if (depth === 0) return { body: text.slice(open + 1, i), end: i + 1 };
    }
  }
  return null;
}

function extractCalls(text, name) {
  const calls = [];
  const pattern = new RegExp(`\\b${name}\\s*\\(`, 'g');
  let match;
  while ((match = pattern.exec(text))) {
    const block = readParenBlock(text, match.index);
    if (!block) continue;
    calls.push(block.body);
    pattern.lastIndex = block.end;
  }
  return calls;
}

function normalizeCmake(body) {
  return body
    .replace(/\$\{PIE_ROOT\}/g, '<PIE_ROOT>')
    .replace(/\$\{CMAKE_CURRENT_BINARY_DIR\}/g, '<BUILD_DIR>')
    .replace(/\$<TARGET_FILE:pie>/g, '<PIE_EXE>')
    .replace(/\\\s*\n/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

function commandForTest(body, name, willFail) {
  if (/\b(?:pie|piec)\s+run\b/.test(body) || (/\brun\b/i.test(name) && willFail)) return 'run';
  if (/\b(?:pie|piec)\s+test\b/.test(body)) return 'test';
  return 'check';
}

function expectedFailureFor(name, command, willFail, passRegex) {
  if (willFail) return true;
  if (command !== 'check') return false;
  if (/(?:^|_)(?:error|fail|oob)(?:_|$)/.test(name) || /_requires_/.test(name)) return true;
  return /(?:error|undefined|already declared|expected|requires|moved|borrow|panic|segmentation|runtime)/i.test(passRegex || '');
}

function parseTestsFromCmake(cmakeText, root) {
  const text = stripCmakeComments(cmakeText);
  const props = extractCalls(text, 'set_tests_properties');
  const willFail = new Map();
  const passRegex = new Map();
  for (const raw of props) {
    const body = normalizeCmake(raw);
    const name = body.match(/^([A-Za-z0-9_.:-]+)/)?.[1];
    if (!name) continue;
    if (/\bWILL_FAIL\s+TRUE\b/i.test(body)) willFail.set(name, true);
    const regex = body.match(/\bPASS_REGULAR_EXPRESSION\s+"([^"]*)"/);
    if (regex) passRegex.set(name, regex[1]);
  }
  const tests = [];
  for (const raw of extractCalls(text, 'add_test')) {
    const body = normalizeCmake(raw);
    const name = body.match(/\bNAME\s+([A-Za-z0-9_.:-]+)/)?.[1];
    if (!name) continue;
    for (const fixture of body.matchAll(/<PIE_ROOT>\/([^"\s)]+\.pie)/g)) {
      const relPath = fixture[1];
      const command = commandForTest(body, name, willFail.get(name) === true);
      const regex = passRegex.get(name) || '';
      const expectedFailure = expectedFailureFor(name, command, willFail.get(name) === true, regex);
      tests.push({ name, relPath, filePath: path.join(root, relPath), command, expectedFailure, passRegex: regex, origin: 'cmake', raw: body });
    }
  }
  return dedupeTests(tests);
}

function dedupeTests(tests) {
  const seen = new Set();
  return tests.filter((test) => {
    const key = `${test.name}|${test.relPath}|${test.command}`;
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

function walkPieFiles(dir) {
  if (!exists(dir)) return [];
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...walkPieFiles(full));
    else if (entry.isFile() && entry.name.endsWith('.pie')) out.push(full);
  }
  return out.sort();
}

function fixtureSweepTests(root, knownTests) {
  const known = new Map();
  for (const test of knownTests) {
    const key = path.normalize(test.relPath);
    if (!known.has(key)) known.set(key, test);
  }
  const files = walkPieFiles(path.join(root, 'tests', 'fixtures'));
  return files.map((filePath) => {
    const relPath = path.relative(root, filePath).replace(/\\/g, '/');
    const knownTest = known.get(path.normalize(relPath));
    if (knownTest) return { ...knownTest, origin: knownTest.origin === 'cmake' ? 'cmake+fixture' : knownTest.origin };
    const base = path.basename(filePath, '.pie');
    const expectedFailure = /(?:^|_)(?:error|fail|oob)(?:_|$)/.test(base) || /_requires_/.test(base);
    return { name: `fixture_${base}`, relPath, filePath, command: expectedFailure ? 'check' : 'run', expectedFailure, passRegex: '', origin: 'fixture' };
  });
}

function buildTestPlan(root, cmakeTests, args) {
  let tests;
  if (args.fixtureSweep) tests = fixtureSweepTests(root, cmakeTests);
  else tests = cmakeTests;
  if (args.expectedOnly) tests = tests.filter((test) => test.expectedFailure);
  return dedupeTests(tests);
}

function runAnalyzer(analyzer, parsed, docs) {
  if (!analyzer) return [];
  const names = ['analyzeParsed', 'analyzeDocument', 'analyzeParsedDocument', 'analyze', 'diagnosticsForParsedDocument'];
  for (const name of names) {
    if (typeof analyzer[name] === 'function') {
      const value = analyzer[name](parsed, docs);
      return Array.isArray(value) ? value : [];
    }
  }
  if (typeof analyzer === 'function') {
    const value = analyzer(parsed, docs);
    return Array.isArray(value) ? value : [];
  }
  return [];
}

function localDiagnostic(line, character, length, message, code, severity = 1) {
  return { range: { start: { line, character }, end: { line, character: character + Math.max(1, length) } }, severity, source: 'pie-test', code, message };
}

function staticDiagnostics(text) {
  const diagnostics = [];
  const lines = text.split(/\r?\n/);
  for (let line = 0; line < lines.length; line += 1) {
    const raw = lines[line];
    const cleaned = raw.replace(/#[^\n]*/, '').trim();
    const req = cleaned.match(/^require\s+"([^"]+)"/);
    if (req && !STANDARD_MODULES.has(req[1]) && !req[1].includes('/')) diagnostics.push(localDiagnostic(line, raw.indexOf(req[1]), req[1].length, `Unknown standard module '${req[1]}'.`, 'unknown-module'));
  }
  return diagnostics;
}

function runLiveDiagnostics(modules, filePath, docs) {
  const text = fs.readFileSync(filePath, 'utf8');
  const parsed = modules.language.parseDocument(`file://${path.resolve(filePath)}`, text);
  const parseDiagnostics = Array.isArray(parsed.diagnostics) ? parsed.diagnostics : [];
  const analyzerDiagnostics = runAnalyzer(modules.analyzer, parsed, docs || [parsed]);
  const extraDiagnostics = staticDiagnostics(text);
  const diagnostics = [...parseDiagnostics, ...analyzerDiagnostics, ...extraDiagnostics];
  return { text, parsed, diagnostics, parseDiagnostics, analyzerDiagnostics, staticDiagnostics: extraDiagnostics };
}

function commandExists(command) {
  try {
    const result = cp.spawnSync(command, ['--version'], { encoding: 'utf8', timeout: 3000, stdio: ['ignore', 'pipe', 'pipe'] });
    return result.status === 0 || result.status === 1;
  } catch { return false; }
}

function detectPieExecutable(root, userPie) {
  const candidates = [];
  if (userPie) candidates.push(path.resolve(userPie));
  candidates.push(path.join(root, 'build', 'pie'));
  candidates.push('pie');
  for (const candidate of candidates) {
    if (candidate === 'pie') { if (commandExists(candidate)) return candidate; }
    else if (exists(candidate)) return candidate;
  }
  return null;
}

function looksFailed(output) {
  return /\b(error|panic|assertion failed|assert failed|segmentation fault|runtime error|crash|failed)\b/i.test(output || '');
}

function runPie(pieExe, root, test, timeout) {
  if (!pieExe) return { ran: false, ok: null, status: null, output: '', args: [], failedByOutput: false };
  const args = test.command === 'run' ? ['run', test.filePath] : test.command === 'test' ? ['test', test.filePath] : ['check', test.filePath];
  const result = cp.spawnSync(pieExe, args, { cwd: root, encoding: 'utf8', timeout, maxBuffer: 16 * 1024 * 1024 });
  const output = `${result.stdout || ''}\n${result.stderr || ''}`.trim();
  const failedByOutput = looksFailed(output);
  const ok = result.status === 0 && !failedByOutput;
  return { ran: true, ok, status: result.status, output, args, failedByOutput, error: result.error ? String(result.error.message || result.error) : null };
}

function diagnosticFromPieOutput(processResult, test, outputPass) {
  if (!processResult.ran) return [];
  if (!test.expectedFailure && test.command !== 'check' && outputPass && !processResult.failedByOutput) return [];
  if (processResult.ok) return [];
  const line = processResult.output.split(/\r?\n/).find((value) => value.trim()) || processResult.error || 'Pie command failed.';
  const parsed = parseDiagnosticLocation(line);
  return [localDiagnostic(parsed.line, parsed.column, parsed.length, parsed.message, 'pie-command')];
}

function parseDiagnosticLocation(line) {
  const full = String(line || '').trim();
  const withFile = full.match(/^(.*?):(\d+):(\d+):\s*(?:error|warning)?:?\s*(.*)$/i);
  if (withFile) return { line: Number(withFile[2]) - 1, column: Number(withFile[3]) - 1, length: 1, message: withFile[4] || full };
  const pie = full.match(/^pie:\s*(?:error|warning):\s*(?:(\d+):(\d+):\s*)?(.*)$/i);
  if (pie) return { line: pie[1] ? Number(pie[1]) - 1 : 0, column: pie[2] ? Number(pie[2]) - 1 : 0, length: 1, message: pie[3] || full };
  return { line: 0, column: 0, length: 1, message: full };
}

function unescapeCmakeRegex(pattern) {
  return String(pattern)
    .replace(/\\([|^])/g, '\\$1')
    .replace(/\n/g, '\n')
    .replace(/\t/g, '\t');
}

function compactOutput(value) {
  return String(value || '').replace(/\s+/g, '');
}

function regexMatches(pattern, output) {
  if (!pattern) return true;
  const text = output || '';
  const value = unescapeCmakeRegex(pattern);
  if (value.includes(';')) {
    const parts = value.split(';').filter(Boolean);
    if (parts.every((part) => regexMatches(part, text))) return true;
    const compactExpected = parts.map((part) => part.replace(/\\([|^])/g, '$1')).join('');
    return compactOutput(text).includes(compactExpected);
  }
  try { return new RegExp(value, 's').test(text); }
  catch { return text.includes(value.replace(/\\([|^])/g, '$1')); }
}

function errorDiagnostics(diagnostics) {
  return diagnostics.filter((item) => item.severity === 1 || item.severity === undefined);
}

function shortDiag(diagnostic) {
  const start = diagnostic.range?.start;
  const where = start ? `${start.line + 1}:${start.character + 1} ` : '';
  const code = diagnostic.code ? `[${diagnostic.code}] ` : '';
  return `${where}${code}${diagnostic.message || '<no message>'}`;
}

function evaluate(test, live, pieResult, args) {
  const outputPass = !test.passRegex || regexMatches(test.passRegex, pieResult.output);
  const pieDiagnostics = diagnosticFromPieOutput(pieResult, test, outputPass);
  const liveErrors = errorDiagnostics(live.diagnostics);
  const extensionDiagnostics = [...live.diagnostics, ...pieDiagnostics];
  const extensionErrors = errorDiagnostics(extensionDiagnostics);
  let piePass = true;
  if (pieResult.ran) {
    if (test.expectedFailure) piePass = pieResult.status !== 0 || pieResult.failedByOutput;
    else if (test.command !== 'check' && test.passRegex) piePass = outputPass && !pieResult.failedByOutput;
    else piePass = pieResult.status === 0 && !pieResult.failedByOutput;
  }
  const extensionPass = test.expectedFailure ? extensionErrors.length > 0 : liveErrors.length === 0;
  const pass = extensionPass && outputPass && (!args.failOnPieMismatch || piePass);
  return { pass, piePass, outputPass, extensionPass, liveErrors, extensionDiagnostics, pieDiagnostics };
}

function status(value) {
  return value ? 'PASS' : 'FAIL';
}

function printResult(result, args) {
  const marker = result.pass ? '✓' : '✗';
  console.log(`${marker} ${result.test.relPath}`);
  console.log(`  test:      ${result.test.name}`);
  console.log(`  origin:    ${result.test.origin}`);
  console.log(`  command:   pie ${result.test.command}`);
  console.log(`  expected:  ${result.test.expectedFailure ? 'error' : 'ok'}`);
  console.log(`  editor:    ${status(result.livePass)} (${result.liveCount} diagnostics)`);
  console.log(`  command:   ${result.commandDiagCount} diagnostics`);
  console.log(`  combined:  ${status(result.extensionPass)} (${result.extensionCount} diagnostics)`);
  console.log(`  pie:       ${result.pie.ran ? `${status(result.piePass)} (exit ${result.pie.status})` : 'SKIP'}`);
  if (result.test.passRegex) console.log(`  output:    ${status(result.outputPass)} / ${result.test.passRegex}`);
  if (args.verbose || !result.pass) {
    if (result.diagnostics.length) {
      console.log('  diagnostics:');
      for (const item of result.diagnostics.slice(0, 8)) console.log(`    - ${item}`);
      if (result.diagnostics.length > 8) console.log(`    ... ${result.diagnostics.length - 8} more`);
    } else {
      console.log('  diagnostics: none');
    }
    if (result.pie.output && (args.verbose || !result.piePass || !result.outputPass)) {
      console.log('  pie output:');
      for (const line of result.pie.output.split(/\r?\n/).slice(0, 12)) console.log(`    ${line}`);
    }
  }
}

function main() {
  const args = parseArgs(process.argv);
  const root = detectRoot(args.root);
  const cmakePath = args.cmake ? path.resolve(args.cmake) : path.join(root, 'CMakeLists.txt');
  if (!exists(cmakePath)) die(`CMakeLists.txt not found: ${cmakePath}`);
  const modules = loadExtensionModules(root);
  const cmakeTests = parseTestsFromCmake(fs.readFileSync(cmakePath, 'utf8'), root);
  const tests = buildTestPlan(root, cmakeTests, args);
  const pieExe = args.includePie ? detectPieExecutable(root, args.pie) : null;
  const results = [];
  for (const test of tests) {
    if (!exists(test.filePath)) {
      results.push({ test, skipped: true, pass: false, reason: 'missing fixture' });
      continue;
    }
    let live;
    try { live = runLiveDiagnostics(modules, test.filePath); }
    catch (error) { live = { diagnostics: [localDiagnostic(0, 0, 1, error.stack || error.message, 'tester-error')], parseDiagnostics: [], analyzerDiagnostics: [], staticDiagnostics: [] }; }
    const pieResult = args.liveOnly ? { ran: false, ok: null, status: null, output: '', args: [] } : runPie(pieExe, root, test, args.timeout);
    const evalResult = evaluate(test, live, pieResult, args);
    const livePass = test.expectedFailure ? evalResult.liveErrors.length > 0 : evalResult.liveErrors.length === 0;
    results.push({
      test,
      pass: evalResult.pass,
      livePass,
      liveCount: live.diagnostics.length,
      extensionPass: evalResult.extensionPass,
      extensionCount: evalResult.extensionDiagnostics.length,
      commandDiagCount: evalResult.pieDiagnostics.length,
      piePass: evalResult.piePass,
      outputPass: evalResult.outputPass,
      pie: pieResult,
      diagnostics: evalResult.extensionDiagnostics.map(shortDiag),
    });
  }
  const summary = {
    root,
    extensionRoot: modules.extensionRoot,
    cmakePath,
    pie: pieExe,
    mode: args.fixtureSweep ? 'fixture-sweep' : args.expectedOnly ? 'expected-only' : 'cmake-tests',
    checked: results.filter((item) => !item.skipped).length,
    skipped: results.filter((item) => item.skipped).length,
    combinedMismatches: results.filter((item) => !item.skipped && !item.extensionPass).length,
    editorMismatches: results.filter((item) => !item.skipped && !item.livePass).length,
    pieMismatches: results.filter((item) => !item.skipped && !item.piePass).length,
    outputMismatches: results.filter((item) => !item.skipped && !item.outputPass).length,
    passed: results.every((item) => item.pass),
    results,
  };
  if (args.json) {
    console.log(JSON.stringify(summary, null, 2));
  } else {
    console.log('Pie extension fixture test');
    console.log(`repo:      ${root}`);
    console.log(`extension: ${modules.extensionRoot}`);
    console.log(`cmake:     ${cmakePath}`);
    console.log(`pie:       ${args.includePie ? (pieExe || '<not found>') : '<disabled>'}`);
    console.log(`mode:      ${summary.mode}`);
    console.log('');
    for (const result of results) {
      if (result.skipped) {
        console.log(`SKIP ${result.test.relPath} (${result.reason})`);
        continue;
      }
      printResult(result, args);
    }
    console.log('');
    console.log(`checked: ${summary.checked}`);
    console.log(`skipped: ${summary.skipped}`);
    console.log(`editor mismatches: ${summary.editorMismatches}`);
    console.log(`combined mismatches: ${summary.combinedMismatches}`);
    console.log(`pie mismatches: ${summary.pieMismatches}`);
    console.log(`output mismatches: ${summary.outputMismatches}`);
    console.log(summary.passed ? 'overall: PASS' : 'overall: FAIL');
    if (args.includePie && !pieExe) console.log('build Pie first or pass --pie /path/to/pie');
  }
  if (args.strict && !summary.passed) process.exit(1);
}

main();
