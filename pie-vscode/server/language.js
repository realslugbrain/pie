'use strict';

const { DiagnosticSeverity, SymbolKind } = require('vscode-languageserver/node');

const KEYWORDS = new Set([
  'and', 'as', 'assert', 'assert_eq', 'auto', 'bool', 'break', 'byte', 'case',
  'char', 'const', 'continue', 'defer', 'do', 'elif', 'else', 'end', 'enum',
  'export', 'false', 'float', 'fn', 'for', 'format', 'from', 'if', 'import', 'in', 'int',
  'let', 'list', 'map', 'match', 'mut', 'new', 'not', 'null', 'or', 'package',
  'pass', 'print', 'println', 'pub', 'raw', 'region', 'require', 'return', 'self',
  'string', 'struct', 'true', 'try', 'type', 'unsafe', 'void', 'where', 'while',
]);

const BUILTIN_TYPES = new Set([
  'int', 'float', 'char', 'byte', 'string', 'bool', 'void', 'list', 'map',
  'Option', 'Result', 'channel', 'mutex', 'thread',
]);

const TYPE_CONSTRAINTS = new Set(['numeric', 'comparable', 'printable', 'integer', 'string_like']);

const STANDARD_MODULES = [
  'io', 'print', 'assert', 'threads', 'format', 'json', 'math', 'fs', 'path',
  'process', 'time', 'net', 'http', 'crypto', 'regex', 'sync', 'task', 'test',
];

const BUILTINS = [
  { name: 'print', kind: SymbolKind.Function, signature: 'print(values...) -> void', documentation: 'Writes values without appending a newline.' },
  { name: 'println', kind: SymbolKind.Function, signature: 'println(values...) -> void', documentation: 'Writes values followed by a newline.' },
  { name: 'format', kind: SymbolKind.Function, signature: 'format(parts...) -> string', documentation: 'Combines values into a formatted string.' },
  { name: 'len', kind: SymbolKind.Function, signature: 'len(value) -> int', documentation: 'Returns the length of a supported collection or string.' },
  { name: 'maybe', kind: SymbolKind.Function, signature: 'maybe() -> bool', documentation: 'Returns a maybe boolean value.' },
  { name: 'assert', kind: SymbolKind.Function, signature: 'assert(condition: bool) -> void', documentation: 'Stops execution when the condition is false.' },
  { name: 'assert_eq', kind: SymbolKind.Function, signature: 'assert_eq(actual, expected) -> void', documentation: 'Stops execution when two values differ.' },
  { name: 'Some', kind: SymbolKind.Constructor, signature: 'Some(value) -> Option', documentation: 'Constructs the populated Option variant.' },
  { name: 'None', kind: SymbolKind.EnumMember, signature: 'None -> Option', documentation: 'The empty Option variant.' },
  { name: 'Ok', kind: SymbolKind.Constructor, signature: 'Ok(value) -> Result', documentation: 'Constructs the successful Result variant.' },
  { name: 'Err', kind: SymbolKind.Constructor, signature: 'Err(error) -> Result', documentation: 'Constructs the failed Result variant.' },
];

const THREAD_OPERATIONS = [
  { name: 'spawn', signature: 'thread.spawn(worker) -> thread', documentation: 'Starts a closure on a new thread.' },
  { name: 'join', signature: 'thread.join(handle) -> int', documentation: 'Waits for a thread and returns its result.' },
  { name: 'mutex', signature: 'thread.mutex() -> mutex', documentation: 'Creates a mutex.' },
  { name: 'mutex_lock', signature: 'thread.mutex_lock(mutex) -> void', documentation: 'Locks a mutex.' },
  { name: 'mutex_unlock', signature: 'thread.mutex_unlock(mutex) -> void', documentation: 'Unlocks a mutex.' },
  { name: 'sleep', signature: 'thread.sleep(milliseconds: int) -> void', documentation: 'Suspends the current thread.' },
  { name: 'channel', signature: 'thread.channel(capacity: int) -> channel', documentation: 'Creates a channel.' },
  { name: 'channel_send', signature: 'thread.channel_send(channel, value) -> void', documentation: 'Sends a value through a channel.' },
  { name: 'channel_recv', signature: 'thread.channel_recv(channel) -> int', documentation: 'Receives a value from a channel.' },
  { name: 'channel_close', signature: 'thread.channel_close(channel) -> void', documentation: 'Closes a channel.' },
];

const METHODS = {
  list: [
    ['len', 'list.len() -> int'], ['push', 'list.push(value) -> void'],
    ['pop', 'list.pop()'], ['insert', 'list.insert(index: int, value) -> void'],
    ['remove', 'list.remove(index: int) -> void'], ['reverse', 'list.reverse() -> void'],
    ['sort', 'list.sort() -> void'],
  ],
  map: [
    ['get', 'map.get(key)'], ['put', 'map.put(key, value) -> void'], ['len', 'map.len() -> int'],
  ],
  string: [
    ['contains', 'string.contains(value: string) -> bool'], ['upper', 'string.upper() -> string'],
    ['lower', 'string.lower() -> string'], ['trim', 'string.trim() -> string'],
    ['replace', 'string.replace(from: string, to: string) -> string'],
    ['repeat', 'string.repeat(count: int) -> string'],
  ],
  channel: [
    ['send', 'channel.send(value) -> void'], ['recv', 'channel.recv()'], ['close', 'channel.close() -> void'],
  ],
};

const OPERATORS = [
  '**=', '..=', '==', '!=', '<=', '>=', '+=', '-=', '*=', '/=', '%=', '++',
  '--', '**', '..', '<<', '>>', '<-', '->', '=>', '::', '+', '-', '*', '/', '%',
  '&', '=', '!', '<', '>', '?', '|', '^', '~', '.', ':', ',', '(', ')', '[',
  ']', '{', '}',
];

function position(line, character) {
  return { line: Math.max(0, line), character: Math.max(0, character) };
}

function range(startLine, startCharacter, endLine, endCharacter) {
  return {
    start: position(startLine, startCharacter),
    end: position(endLine, endCharacter),
  };
}

function diagnostic(line, character, length, message, code, severity = DiagnosticSeverity.Error) {
  return {
    range: range(line, character, line, character + Math.max(1, length)),
    severity,
    source: 'pie-lsp',
    code,
    message,
  };
}

function lex(text) {
  const tokens = [];
  const diagnostics = [];
  const delimiters = [];
  let offset = 0;
  let line = 0;
  let column = 0;

  function current() { return text[offset] || ''; }
  function peek(distance = 1) { return text[offset + distance] || ''; }
  function advance() {
    const value = text[offset++] || '';
    if (value === '\n') {
      line += 1;
      column = 0;
    } else {
      column += 1;
    }
    return value;
  }
  function push(kind, startOffset, startLine, startColumn) {
    tokens.push({
      kind,
      text: text.slice(startOffset, offset),
      start: startOffset,
      end: offset,
      line: startLine,
      column: startColumn,
      endLine: line,
      endColumn: column,
    });
  }

  while (offset < text.length) {
    const startOffset = offset;
    const startLine = line;
    const startColumn = column;
    const value = current();

    if (value === ' ' || value === '\t' || value === '\r') {
      advance();
      continue;
    }
    if (value === '\n') {
      advance();
      push('newline', startOffset, startLine, startColumn);
      continue;
    }
    if (value === '#') {
      while (current() && current() !== '\n') advance();
      push('comment', startOffset, startLine, startColumn);
      continue;
    }
    if (value === '/' && peek() === '*') {
      advance();
      advance();
      let closed = false;
      while (current()) {
        if (current() === '*' && peek() === '/') {
          advance();
          advance();
          closed = true;
          break;
        }
        advance();
      }
      push('comment', startOffset, startLine, startColumn);
      if (!closed) diagnostics.push(diagnostic(startLine, startColumn, 2, 'Unterminated block comment.', 'unterminated-block-comment'));
      continue;
    }
    if (value === '"') {
      advance();
      let closed = false;
      while (current()) {
        if (current() === '\n') break;
        if (current() === '\\') {
          advance();
          if (current()) advance();
          continue;
        }
        if (current() === '"') {
          advance();
          closed = true;
          break;
        }
        advance();
      }
      push('string', startOffset, startLine, startColumn);
      if (!closed) diagnostics.push(diagnostic(startLine, startColumn, 1, 'Unterminated string literal.', 'unterminated-string'));
      continue;
    }
    if (value === '\'') {
      advance();
      if (current() === '\\') {
        advance();
        if (current()) advance();
      } else if (current() && current() !== '\n') {
        advance();
      }
      const closed = current() === '\'';
      if (closed) advance();
      push('character', startOffset, startLine, startColumn);
      if (!closed) diagnostics.push(diagnostic(startLine, startColumn, 1, 'Invalid or unterminated character literal.', 'invalid-character'));
      continue;
    }
    if (/[A-Za-z_]/.test(value)) {
      advance();
      while (/[A-Za-z0-9_]/.test(current())) advance();
      const word = text.slice(startOffset, offset);
      push(KEYWORDS.has(word) ? 'keyword' : 'identifier', startOffset, startLine, startColumn);
      continue;
    }
    if (/[0-9]/.test(value)) {
      advance();
      if (value === '0' && /[xX]/.test(current())) {
        advance();
        while (/[0-9a-fA-F]/.test(current())) advance();
      } else if (value === '0' && /[bB]/.test(current())) {
        advance();
        while (/[01]/.test(current())) advance();
      } else if (value === '0' && /[oO]/.test(current())) {
        advance();
        while (/[0-7]/.test(current())) advance();
      } else {
        while (/[0-9]/.test(current())) advance();
        if (current() === '.' && /[0-9]/.test(peek())) {
          advance();
          while (/[0-9]/.test(current())) advance();
        }
        if (/[eE]/.test(current())) {
          advance();
          if (/[+-]/.test(current())) advance();
          while (/[0-9]/.test(current())) advance();
        }
      }
      push('number', startOffset, startLine, startColumn);
      continue;
    }

    const operator = OPERATORS.find((candidate) => text.startsWith(candidate, offset));
    if (operator) {
      for (let index = 0; index < operator.length; index += 1) advance();
      push('operator', startOffset, startLine, startColumn);
      const opening = { '(': ')', '[': ']', '{': '}' };
      if (opening[operator]) {
        delimiters.push({ text: operator, expected: opening[operator], line: startLine, column: startColumn });
      } else if ([')', ']', '}'].includes(operator)) {
        const top = delimiters.pop();
        if (!top || top.expected !== operator) {
          diagnostics.push(diagnostic(startLine, startColumn, 1, `Unexpected '${operator}'.`, 'unexpected-delimiter'));
          if (top) delimiters.push(top);
        }
      }
      continue;
    }

    advance();
    push('invalid', startOffset, startLine, startColumn);
    diagnostics.push(diagnostic(startLine, startColumn, 1, `Unexpected character '${value}'.`, 'unexpected-character'));
  }

  for (const item of delimiters) {
    diagnostics.push(diagnostic(item.line, item.column, 1, `Expected '${item.expected}' to close '${item.text}'.`, 'unclosed-delimiter'));
  }
  return { tokens, diagnostics };
}

function stripLineComment(line) {
  let quote = '';
  let escaped = false;
  for (let index = 0; index < line.length; index += 1) {
    const value = line[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (value === '\\' && quote) {
      escaped = true;
      continue;
    }
    if ((value === '"' || value === '\'') && (!quote || quote === value)) {
      quote = quote ? '' : value;
      continue;
    }
    if (value === '#' && !quote) return line.slice(0, index);
  }
  return line;
}

function splitTopLevel(value, separator = ',') {
  const parts = [];
  let start = 0;
  let round = 0;
  let square = 0;
  let brace = 0;
  for (let index = 0; index < value.length; index += 1) {
    const character = value[index];
    if (character === '(') round += 1;
    else if (character === ')') round -= 1;
    else if (character === '[') square += 1;
    else if (character === ']') square -= 1;
    else if (character === '{') brace += 1;
    else if (character === '}') brace -= 1;
    else if (character === separator && round === 0 && square === 0 && brace === 0) {
      parts.push(value.slice(start, index).trim());
      start = index + 1;
    }
  }
  parts.push(value.slice(start).trim());
  return parts.filter(Boolean);
}

function declarationDocumentation(lines, declarationLine) {
  const collected = [];
  for (let index = declarationLine - 1; index >= 0; index -= 1) {
    const trimmed = lines[index].trim();
    if (!trimmed.startsWith('#')) break;
    collected.unshift(trimmed.replace(/^#\s?/, ''));
  }
  return collected.join('\n');
}

function blockKind(cleaned) {
  const line = cleaned.trim();
  if (!line || !line.endsWith(':')) return undefined;
  if (line === ':') return 'block';
  if (/^(?:pub\s+|export\s+)?(?:unsafe\s+)?fn\b/.test(line) || /\bfn\s*\([^)]*\).*:$/.test(line)) return 'function';
  if (/^(?:pub\s+|export\s+)?struct\b/.test(line)) return 'struct';
  if (/^(?:pub\s+|export\s+)?enum\b/.test(line)) return 'enum';
  if (/^if\b/.test(line)) return 'if';
  if (/^(?::[A-Za-z_]\w*\s+)?while\b/.test(line)) return 'while';
  if (/^(?::[A-Za-z_]\w*\s+)?for\b/.test(line)) return 'for';
  if (/\bmatch\b.*:$/.test(line)) return 'match';
  if (/^region\b/.test(line)) return 'region';
  if (/^unsafe\s*:$/.test(line)) return 'unsafe';
  if (/^do\s*:$/.test(line)) return 'do';
  return undefined;
}

function analyzeBlocks(lines) {
  const stack = [];
  const ends = new Map();
  const diagnostics = [];
  for (let line = 0; line < lines.length; line += 1) {
    const cleaned = stripLineComment(lines[line]).trim();
    if (/^end\b/.test(cleaned)) {
      const open = stack.pop();
      if (!open) diagnostics.push(diagnostic(line, lines[line].indexOf('end'), 3, "Unexpected 'end'.", 'unexpected-end'));
      else ends.set(open.line, line);
      continue;
    }
    if (stack.at(-1)?.kind === 'do' && /^while\b/.test(cleaned) && !cleaned.endsWith(':')) {
      const open = stack.pop();
      ends.set(open.line, line);
      continue;
    }
    const kind = blockKind(cleaned);
    if (kind) stack.push({ kind, line });
  }
  for (const open of stack) {
    diagnostics.push(diagnostic(open.line, Math.max(0, lines[open.line].length - 1), 1, `Expected 'end' after ${open.kind} block.`, 'missing-end'));
    ends.set(open.line, Math.max(open.line, lines.length - 1));
  }
  return { ends, diagnostics };
}

function wordRange(lineText, line, word, from = 0) {
  const index = lineText.indexOf(word, from);
  const start = index >= 0 ? index : Math.max(0, from);
  return range(line, start, line, start + word.length);
}

function parseNamedFunctionHeader(cleaned) {
  const start = cleaned.match(/^\s*(pub\s+|export\s+)?(unsafe\s+)?fn\s+(?:([A-Za-z_]\w*)\.)?([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*\(/);
  if (!start) return undefined;
  const open = cleaned.indexOf('(', start[0].lastIndexOf('('));
  let depth = 0;
  let close = -1;
  for (let index = open; index < cleaned.length; index += 1) {
    const ch = cleaned[index];
    if (ch === '(') depth += 1;
    else if (ch === ')') {
      depth -= 1;
      if (depth === 0) { close = index; break; }
    }
  }
  if (close < 0) return undefined;
  const tail = cleaned.slice(close + 1).trim();
  const returnMatch = tail.match(/^(?:->\s*(.+?))?\s*:\s*$/);
  if (!returnMatch) return undefined;
  return {
    pub: start[1] || '',
    unsafe: start[2] || '',
    receiver: start[3] || '',
    name: start[4],
    generics: start[5] || '',
    params: cleaned.slice(open + 1, close).trim(),
    returnType: (returnMatch[1] || 'void').trim(),
  };
}

function makeSymbol(uri, lines, name, kind, line, startColumn, endLine, details = {}) {
  const selectionRange = range(line, startColumn, line, startColumn + name.length);
  return {
    id: `${uri}:${line}:${startColumn}:${name}`,
    name,
    kind,
    uri,
    range: range(line, Math.max(0, lines[line].search(/\S|$/)), endLine, lines[endLine]?.length || 0),
    selectionRange,
    detail: details.detail || '',
    signature: details.signature || '',
    documentation: details.documentation || declarationDocumentation(lines, line),
    type: details.type || '',
    containerName: details.containerName || '',
    receiver: details.receiver || '',
    scopeStart: details.scopeStart ?? line,
    scopeEnd: details.scopeEnd ?? endLine,
    isPublic: Boolean(details.isPublic),
    data: details.data || {},
    children: [],
  };
}

function parseDocument(uri, text) {
  const lines = text.split(/\r?\n/);
  const lexical = lex(text);
  const blocks = analyzeBlocks(lines);
  const symbols = [];
  const globals = [];
  const locals = [];
  const requires = [];
  const functions = [];
  const types = [];

  function add(symbol, global = true) {
    symbols.push(symbol);
    (global ? globals : locals).push(symbol);
    return symbol;
  }

  for (let line = 0; line < lines.length; line += 1) {
    const original = lines[line];
    const cleaned = stripLineComment(original);
    let match;

    match = cleaned.match(/^\s*package\s+(?:"([^"]+)"|([A-Za-z_]\w*))/);
    if (match) {
      const name = match[1] || match[2];
      add(makeSymbol(uri, lines, name, SymbolKind.Package, line, original.indexOf(name), line, { detail: 'package' }));
    }

    match = original.match(/^\s*require\s+"([^"]+)"/);
    if (match) {
      const modulePath = match[1];
      const start = original.indexOf(modulePath);
      requires.push({ path: modulePath, uri, range: range(line, start, line, start + modulePath.length) });
    }

    match = parseNamedFunctionHeader(cleaned);
    if (match) {
      const receiver = match.receiver;
      const name = match.name;
      const genericText = match.generics;
      const paramsText = match.params;
      const returnType = match.returnType;
      const endLine = blocks.ends.get(line) ?? line;
      const nameStart = receiver
        ? original.indexOf(name, original.indexOf(receiver) + receiver.length)
        : original.indexOf(name, original.indexOf('fn') + 2);
      const signature = `${match.pub}${match.unsafe}fn ${receiver ? `${receiver}.` : ''}${name}${genericText}(${paramsText}) -> ${returnType}`.trim();
      const fnSymbol = add(makeSymbol(uri, lines, name, receiver ? SymbolKind.Method : SymbolKind.Function, line, nameStart, endLine, {
        signature,
        detail: returnType,
        type: returnType,
        receiver,
        containerName: receiver,
        scopeStart: line,
        scopeEnd: endLine,
        isPublic: Boolean(match.pub),
        data: { params: [], generics: [] },
      }));
      functions.push(fnSymbol);

      if (genericText) {
        for (const generic of splitTopLevel(genericText.slice(1, -1))) {
          const genericMatch = generic.match(/^([A-Za-z_]\w*)(?:\s*:\s*([A-Za-z_]\w*))?/);
          if (!genericMatch) continue;
          const genericName = genericMatch[1];
          const start = original.indexOf(genericName, original.indexOf('['));
          const genericSymbol = add(makeSymbol(uri, lines, genericName, SymbolKind.TypeParameter, line, start, line, {
            detail: genericMatch[2] ? `type parameter: ${genericMatch[2]}` : 'type parameter',
            scopeStart: line,
            scopeEnd: endLine,
            containerName: name,
            data: { typeParameter: true },
          }), false);
          fnSymbol.data.generics.push(genericSymbol);
        }
      }

      for (const parameter of splitTopLevel(paramsText)) {
        const parameterMatch = parameter.match(/^([A-Za-z_]\w*)\s*:\s*(.+)$/);
        if (!parameterMatch) continue;
        const parameterName = parameterMatch[1];
        const parameterType = parameterMatch[2].trim();
        const searchStart = original.indexOf('(') + 1;
        const start = original.indexOf(parameterName, searchStart);
        const parameterSymbol = add(makeSymbol(uri, lines, parameterName, SymbolKind.Variable, line, start, line, {
          detail: parameterType,
          type: parameterType,
          containerName: name,
          scopeStart: line,
          scopeEnd: endLine,
          data: { parameter: true },
        }), false);
        fnSymbol.data.params.push(parameterSymbol);
        fnSymbol.children.push(parameterSymbol);
      }
      continue;
    }

    match = cleaned.match(/^\s*(?:pub\s+|export\s+)?struct\s+([A-Za-z_]\w*)\s*:\s*$/);
    if (match) {
      const name = match[1];
      const endLine = blocks.ends.get(line) ?? line;
      const symbol = add(makeSymbol(uri, lines, name, SymbolKind.Struct, line, original.indexOf(name), endLine, {
        detail: 'struct', type: name, scopeStart: line, scopeEnd: endLine,
      }));
      types.push(symbol);
      const parentIndent = original.search(/\S|$/);
      for (let fieldLine = line + 1; fieldLine < endLine; fieldLine += 1) {
        const fieldText = stripLineComment(lines[fieldLine]);
        if (fieldText.search(/\S|$/) <= parentIndent) continue;
        const fieldMatch = fieldText.match(/^\s*([A-Za-z_]\w*)\s*:\s*(.+?)\s*$/);
        if (!fieldMatch || /^(?:let|mut|const|fn)\b/.test(fieldText.trim())) continue;
        const field = add(makeSymbol(uri, lines, fieldMatch[1], SymbolKind.Field, fieldLine, lines[fieldLine].indexOf(fieldMatch[1]), fieldLine, {
          detail: fieldMatch[2].trim(), type: fieldMatch[2].trim(), containerName: name,
          scopeStart: line, scopeEnd: endLine,
        }), false);
        symbol.children.push(field);
      }
      continue;
    }

    match = cleaned.match(/^\s*(?:pub\s+|export\s+)?enum\s+([A-Za-z_]\w*)\s*:\s*$/);
    if (match) {
      const name = match[1];
      const endLine = blocks.ends.get(line) ?? line;
      const symbol = add(makeSymbol(uri, lines, name, SymbolKind.Enum, line, original.indexOf(name), endLine, {
        detail: 'enum', type: name, scopeStart: line, scopeEnd: endLine,
      }));
      types.push(symbol);
      for (let caseLine = line + 1; caseLine < endLine; caseLine += 1) {
        const caseMatch = stripLineComment(lines[caseLine]).match(/^\s*case\s+([A-Za-z_]\w*)(?:\s*\((.*)\))?/);
        if (!caseMatch) continue;
        const variant = add(makeSymbol(uri, lines, caseMatch[1], SymbolKind.EnumMember, caseLine, lines[caseLine].indexOf(caseMatch[1]), caseLine, {
          signature: `${name}.${caseMatch[1]}${caseMatch[2] ? `(${caseMatch[2]})` : ''}`,
          detail: caseMatch[2] || name,
          type: name,
          containerName: name,
          scopeStart: line,
          scopeEnd: lines.length - 1,
        }), false);
        symbol.children.push(variant);
      }
      continue;
    }

    match = cleaned.match(/^\s*type\s+([A-Za-z_]\w*)\s*:\s*(.+?)\s*$/);
    if (match) {
      const symbol = add(makeSymbol(uri, lines, match[1], SymbolKind.TypeParameter, line, original.indexOf(match[1]), line, {
        signature: `type ${match[1]}: ${match[2]}`,
        detail: match[2].trim(), type: match[1], scopeStart: line, scopeEnd: lines.length - 1,
        data: { alias: true },
      }));
      types.push(symbol);
      continue;
    }

    match = cleaned.match(/^\s*const\s+([A-Za-z_]\w*)\s*:\s*(.+?)\s*->\s*/);
    if (match) {
      add(makeSymbol(uri, lines, match[1], SymbolKind.Constant, line, original.indexOf(match[1]), line, {
        signature: `const ${match[1]}: ${match[2].trim()}`,
        detail: match[2].trim(), type: match[2].trim(), scopeStart: line, scopeEnd: lines.length - 1,
      }));
    }
  }

  function containingFunction(line) {
    return functions
      .filter((symbol) => symbol.range.start.line <= line && symbol.range.end.line >= line)
      .sort((left, right) => right.range.start.line - left.range.start.line)[0];
  }

  function innermostBlockForLine(line) {
    let best;
    for (const [start, end] of blocks.ends.entries()) {
      if (start < line && line < end && (!best || start > best.start || (start === best.start && end < best.end))) {
        best = { start, end };
      }
    }
    return best;
  }

  function localScopeForLine(line, owner) {
    const block = innermostBlockForLine(line);
    if (block) return { scopeStart: block.start, scopeEnd: block.end };
    return {
      scopeStart: owner?.range.start.line ?? 0,
      scopeEnd: owner?.range.end.line ?? lines.length - 1,
    };
  }

  for (let line = 0; line < lines.length; line += 1) {
    const original = lines[line];
    const cleaned = stripLineComment(original);
    if (/^\s*(?:pub\s+)?(?:unsafe\s+)?fn\b/.test(cleaned) || /^\s*(?:struct|enum|type|const)\b/.test(cleaned)) continue;
    const owner = containingFunction(line);
    const { scopeStart, scopeEnd } = localScopeForLine(line, owner);
    let match = cleaned.match(/^\s*(?:let\s+)?(?:mut\s+)?([A-Za-z_]\w*)\s*:\s*(.+?)\s*(?:->|<-)(?!=)/);
    if (match) {
      add(makeSymbol(uri, lines, match[1], SymbolKind.Variable, line, original.indexOf(match[1]), line, {
        detail: match[2].trim(), type: match[2].trim(), containerName: owner?.name || '', scopeStart, scopeEnd,
      }), false);
    }
    match = cleaned.match(/^\s*(?::[A-Za-z_]\w*\s+)?for\s+([A-Za-z_]\w*)(?:\s*,\s*([A-Za-z_]\w*))?\s+in\b/);
    if (match) {
      for (const name of [match[1], match[2]].filter(Boolean)) {
        add(makeSymbol(uri, lines, name, SymbolKind.Variable, line, original.indexOf(name), line, {
          detail: 'loop variable', containerName: owner?.name || '', scopeStart: line, scopeEnd: blocks.ends.get(line) ?? scopeEnd,
        }), false);
      }
    }
    match = cleaned.match(/^\s*case\s+(?:[A-Za-z_]\w*\.)?[A-Za-z_]\w*\s*\(([^)]*)\)/);
    if (match) {
      for (const name of splitTopLevel(match[1])) {
        if (!/^[A-Za-z_]\w*$/.test(name)) continue;
        add(makeSymbol(uri, lines, name, SymbolKind.Variable, line, original.indexOf(name), line, {
          detail: 'pattern binding', containerName: owner?.name || '', scopeStart: line, scopeEnd,
        }), false);
      }
    }
  }

  const duplicateDiagnostics = [];
  const declarationsByName = new Map();
  for (const symbol of globals) {
    if (symbol.kind === SymbolKind.Package) continue;
    const key = `${symbol.kind}:${symbol.receiver}:${symbol.name}`;
    const previous = declarationsByName.get(key);
    if (previous) {
      duplicateDiagnostics.push({
        ...diagnostic(symbol.selectionRange.start.line, symbol.selectionRange.start.character, symbol.name.length, `Duplicate declaration '${symbol.name}'.`, 'duplicate-declaration'),
        relatedInformation: [{ location: { uri, range: previous.selectionRange }, message: 'First declaration is here.' }],
      });
    } else declarationsByName.set(key, symbol);
  }

  return {
    uri,
    text,
    lines,
    tokens: lexical.tokens,
    symbols,
    globals,
    locals,
    functions,
    types,
    requires,
    blockEnds: blocks.ends,
    diagnostics: [...lexical.diagnostics, ...blocks.diagnostics, ...duplicateDiagnostics],
  };
}

function tokenAt(parsed, target) {
  return parsed.tokens.find((token) => {
    if (token.line > target.line || token.endLine < target.line) return false;
    if (token.line === target.line && target.character < token.column) return false;
    if (token.endLine === target.line && target.character > token.endColumn) return false;
    return token.kind === 'identifier' || token.kind === 'keyword';
  });
}

function previousToken(parsed, token) {
  const index = parsed.tokens.indexOf(token);
  for (let current = index - 1; current >= 0; current -= 1) {
    if (!['newline', 'comment'].includes(parsed.tokens[current].kind)) return parsed.tokens[current];
  }
  return undefined;
}

function nextToken(parsed, token) {
  const index = parsed.tokens.indexOf(token);
  for (let current = index + 1; current < parsed.tokens.length; current += 1) {
    if (!['newline', 'comment'].includes(parsed.tokens[current].kind)) return parsed.tokens[current];
  }
  return undefined;
}

function visibleSymbols(parsed, line) {
  return parsed.locals
    .filter((symbol) => symbol.scopeStart <= line && symbol.scopeEnd >= line && symbol.selectionRange.start.line <= line)
    .sort((left, right) => {
      const lineDifference = right.selectionRange.start.line - left.selectionRange.start.line;
      return lineDifference || right.selectionRange.start.character - left.selectionRange.start.character;
    });
}

function baseType(type) {
  if (!type) return '';
  return type
    .replace(/^\?/, '')
    .replace(/^&\s*(?:mut\s+)?/, '')
    .replace(/^\*/, '')
    .replace(/<[^>]*>/g, '')
    .trim()
    .match(/^[A-Za-z_]\w*/)?.[0] || '';
}

function symbolAt(parsed, target) {
  const token = tokenAt(parsed, target);
  if (!token) return undefined;
  const exact = parsed.symbols.find((symbol) =>
    symbol.selectionRange.start.line === token.line
    && symbol.selectionRange.start.character === token.column
    && symbol.name === token.text);
  if (exact) return exact;
  return visibleSymbols(parsed, target.line).find((symbol) => symbol.name === token.text)
    || parsed.globals.find((symbol) => symbol.name === token.text);
}

function formatDocument(text, options = {}) {
  const lines = text.split(/\r?\n/);
  const size = Number(options.tabSize || options.indentSize || 4);
  const unit = options.insertSpaces === false ? '\t' : ' '.repeat(Math.max(1, size));
  let level = 0;
  let activeCase = false;
  const stack = [];
  const output = [];

  for (const rawLine of lines) {
    const trailingTrimmed = rawLine.replace(/[ \t]+$/g, '');
    const trimmed = trailingTrimmed.trimStart();
    if (!trimmed) {
      output.push('');
      continue;
    }
    const cleaned = stripLineComment(trimmed).trim();
    const closesDo = stack.at(-1) === 'do' && /^while\b/.test(cleaned) && !cleaned.endsWith(':');
    if (/^end\b/.test(cleaned) || closesDo) {
      if (activeCase) {
        level = Math.max(0, level - 1);
        activeCase = false;
      }
      level = Math.max(0, level - 1);
      stack.pop();
    } else if (/^(?:elif|else)\b/.test(cleaned)) {
      level = Math.max(0, level - 1);
    } else if (/^case\b/.test(cleaned)) {
      if (activeCase) level = Math.max(0, level - 1);
    }

    output.push(`${unit.repeat(level)}${trimmed}`);

    if (/^(?:elif|else)\b.*:\s*$/.test(cleaned)) {
      level += 1;
    } else if (/^case\b.*:\s*$/.test(cleaned)) {
      level += 1;
      activeCase = true;
    } else {
      const kind = blockKind(cleaned);
      if (kind) {
        stack.push(kind);
        level += 1;
        activeCase = false;
      }
    }
  }
  return output.join('\n');
}

function documentSymbols(parsed) {
  const childIds = new Set();
  for (const symbol of parsed.globals) {
    for (const child of symbol.children) childIds.add(child.id);
  }
  return parsed.globals
    .filter((symbol) => !childIds.has(symbol.id))
    .map((symbol) => ({
      name: symbol.name,
      detail: symbol.signature || symbol.detail,
      kind: symbol.kind,
      range: symbol.range,
      selectionRange: symbol.selectionRange,
      children: symbol.children.map((child) => ({
        name: child.name,
        detail: child.signature || child.detail,
        kind: child.kind,
        range: child.range,
        selectionRange: child.selectionRange,
      })),
    }));
}

module.exports = {
  BUILTINS,
  BUILTIN_TYPES,
  KEYWORDS,
  METHODS,
  STANDARD_MODULES,
  THREAD_OPERATIONS,
  TYPE_CONSTRAINTS,
  baseType,
  documentSymbols,
  formatDocument,
  lex,
  nextToken,
  parseDocument,
  previousToken,
  range,
  splitTopLevel,
  symbolAt,
  tokenAt,
  visibleSymbols,
  wordRange,
};
