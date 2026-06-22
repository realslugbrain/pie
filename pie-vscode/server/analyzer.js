'use strict';

const { DiagnosticSeverity, SymbolKind } = require('vscode-languageserver/node');
const language = require('./language');

const PRIMITIVE_TYPES = new Set(['int', 'float', 'char', 'byte', 'string', 'bool', 'void']);
const VALUE_KEYWORDS = new Set(['true', 'false', 'null', 'self']);
const DECLARATION_HEADS = /^(?:package|require|pub\s+|unsafe\s+|fn\b|struct\b|enum\b|type\b|case\b|const\b)/;

function diagnostic(line, character, length, message, code, severity = DiagnosticSeverity.Error, data = undefined) {
  return {
    range: language.range(line, character, line, character + Math.max(1, length)),
    severity,
    source: 'pie-lsp',
    code,
    message,
    data,
  };
}

function stripComment(line) {
  let quote = '';
  let escaped = false;
  for (let index = 0; index < line.length; index += 1) {
    const value = line[index];
    if (escaped) { escaped = false; continue; }
    if (value === '\\' && quote) { escaped = true; continue; }
    if ((value === '"' || value === "'") && (!quote || quote === value)) { quote = quote ? '' : value; continue; }
    if (value === '#' && !quote) return line.slice(0, index);
  }
  return line;
}

function maskStringLiterals(line) {
  let quote = '';
  let escaped = false;
  let out = '';
  for (let index = 0; index < line.length; index += 1) {
    const value = line[index];
    if (quote) {
      out += ' ';
      if (escaped) escaped = false;
      else if (value === '\\') escaped = true;
      else if (value === quote) quote = '';
      continue;
    }
    if (value === '"' || value === "'") {
      quote = value;
      out += ' ';
      continue;
    }
    out += value;
  }
  return out;
}

function codeOnly(line) {
  return maskStringLiterals(stripComment(line));
}

function isGenericType(type) {
  const value = baseType(type || '').trim();
  return /^[A-Z][A-Za-z0-9_]*$/.test(value);
}

function isInvalidType(type) {
  return baseType(type || '') === 'invalid';
}

function shouldSkipOperatorType(type) {
  return !type || isGenericType(type);
}

function baseType(type) {
  return language.baseType(type);
}

function isNumericType(type) {
  return ['int', 'float', 'byte'].includes(baseType(type));
}

function isIntegerType(type) {
  return ['int', 'byte'].includes(baseType(type));
}

function isPointerType(type) {
  return /^\s*\*/.test(type || '');
}

function isReferenceType(type) {
  return /^\s*&/.test(type || '');
}

function numericResultType(left, right) {
  const leftBase = baseType(left);
  const rightBase = baseType(right);
  if (!leftBase && !rightBase) return '';
  if (!isNumericType(left) || !isNumericType(right)) return 'invalid';
  if (leftBase === 'float' || rightBase === 'float') return leftBase === 'float' ? left : right;
  if (left && right && left === right) return left;
  if (leftBase === 'byte' && rightBase === 'byte') return 'byte';
  return left || right || 'int';
}

function integerResultType(left, right) {
  const leftBase = baseType(left);
  const rightBase = baseType(right);
  if (!leftBase && !rightBase) return '';
  if (!isIntegerType(left) || !isIntegerType(right)) return 'invalid';
  if (left && right && left === right) return left;
  return left || right || 'int';
}

function splitTernary(value) {
  let round = 0;
  let square = 0;
  let brace = 0;
  let quote = '';
  let escaped = false;
  let question = -1;
  for (let index = 0; index < value.length; index += 1) {
    const ch = value[index];
    if (quote) {
      if (escaped) escaped = false;
      else if (ch === '\\') escaped = true;
      else if (ch === quote) quote = '';
      continue;
    }
    if (ch === '"' || ch === "'") { quote = ch; continue; }
    if (ch === '(') round += 1;
    else if (ch === ')') round -= 1;
    else if (ch === '[') square += 1;
    else if (ch === ']') square -= 1;
    else if (ch === '{') brace += 1;
    else if (ch === '}') brace -= 1;
    else if (ch === '?' && round === 0 && square === 0 && brace === 0) { question = index; break; }
  }
  if (question < 0) return undefined;
  round = 0; square = 0; brace = 0; quote = ''; escaped = false;
  for (let index = question + 1; index < value.length; index += 1) {
    const ch = value[index];
    if (quote) {
      if (escaped) escaped = false;
      else if (ch === '\\') escaped = true;
      else if (ch === quote) quote = '';
      continue;
    }
    if (ch === '"' || ch === "'") { quote = ch; continue; }
    if (ch === '(') round += 1;
    else if (ch === ')') round -= 1;
    else if (ch === '[') square += 1;
    else if (ch === ']') square -= 1;
    else if (ch === '{') brace += 1;
    else if (ch === '}') brace -= 1;
    else if (ch === ':' && round === 0 && square === 0 && brace === 0) {
      return {
        condition: value.slice(0, question).trim(),
        yes: value.slice(question + 1, index).trim(),
        no: value.slice(index + 1).trim(),
      };
    }
  }
  return undefined;
}

function isMutableDeclaration(line, name) {
  const cleaned = stripComment(line);
  const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  return new RegExp(`^\\s*(?:let\\s+)?mut\\s+${escaped}\\b`).test(cleaned)
    || new RegExp(`^\\s*${escaped}\\s*:\\s*&\\s*mut\\b`).test(cleaned)
    || new RegExp(`^\\s*let\\s+mut\\s+${escaped}\\b`).test(cleaned);
}

function symbolDeclaredAt(parsed, token) {
  return parsed.symbols.find((symbol) => symbol.name === token.text
    && symbol.selectionRange.start.line === token.line
    && symbol.selectionRange.start.character === token.column);
}

function resolveLocalOrGlobal(parsed, allDocuments, name, line) {
  const local = language.visibleSymbols(parsed, line).find((symbol) => symbol.name === name);
  if (local) return local;
  const own = parsed.globals.find((symbol) => symbol.name === name);
  if (own) return own;
  for (const doc of allDocuments || []) {
    const global = doc.globals.find((symbol) => symbol.name === name);
    if (global) return global;
  }
  const closureParam = resolveClosureParameter(parsed, name, line);
  if (closureParam) return closureParam;
  const patternBinding = resolvePatternBinding(parsed, name, line);
  if (patternBinding) return patternBinding;
  const builtin = language.BUILTINS.find((item) => item.name === name);
  if (builtin) return { name, kind: builtin.kind, type: builtin.signature?.split('->').pop()?.trim() || '', signature: builtin.signature };
  if (language.BUILTIN_TYPES.has(name) || VALUE_KEYWORDS.has(name)) return { name, kind: SymbolKind.Variable };
  return undefined;
}

function inferLiteralType(expr, parsed, allDocuments, line) {
  const value = expr.trim();
  if (!value) return '';
  if (/^"(?:\\.|[^"\\])*"$/.test(value)) return 'string';
  if (/^'(?:\\.|[^'\\])'$/.test(value)) return 'char';
  if (/^(?:true|false)$/.test(value)) return 'bool';
  if (/^null$/.test(value)) return 'null';
  const ternary = splitTernary(value);
  if (ternary) {
    const yesType = inferLiteralType(ternary.yes, parsed, allDocuments, line);
    const noType = inferLiteralType(ternary.no, parsed, allDocuments, line);
    if (yesType && noType && compatible(yesType, noType)) return yesType;
    return yesType || noType || '';
  }
  const castMatch = value.match(/^(.+?)\s+as\s+(.+)$/);
  if (castMatch) return castMatch[2].trim();
  const ifExpressionMatch = value.match(/^if\s+.+?:\s*(.+?)\s+else\s*:\s*(.+?)\s+end$/);
  if (ifExpressionMatch) {
    const yesType = inferLiteralType(ifExpressionMatch[1], parsed, allDocuments, line);
    const noType = inferLiteralType(ifExpressionMatch[2], parsed, allDocuments, line);
    if (yesType && noType && compatible(yesType, noType)) return yesType;
    return yesType || noType || '';
  }
  const incDecMatch = value.match(/^(?:\+\+|--)?\s*([A-Za-z_]\w*)\s*(?:\+\+|--)?$/);
  if (incDecMatch && /^(?:\+\+|--|[A-Za-z_]\w*\s*(?:\+\+|--))/.test(value)) {
    const symbol = resolveLocalOrGlobal(parsed, allDocuments, incDecMatch[1], line);
    const type = symbol?.type || 'int';
    if (isIntegerType(type)) return type;
    if (type) return 'invalid';
    return 'int';
  }
  const fieldMatch = value.match(/^([A-Za-z_]\w*)\.([A-Za-z_]\w*)$/);
  if (fieldMatch) {
    const owner = resolveLocalOrGlobal(parsed, allDocuments, fieldMatch[1], line);
    const ownerType = baseType(owner?.type || '');
    const documents = [parsed, ...(allDocuments || [])];
    const definition = documents.flatMap((document) => document.types || [])
      .find((type) => type.name === ownerType);
    const field = definition?.children?.find((child) => child.name === fieldMatch[2]);
    if (field?.type) return field.type;
  }
  const callMatch = value.match(/^([A-Za-z_]\w*)\s*\((.*)\)$/);
  if (callMatch) {
    const symbol = resolveLocalOrGlobal(parsed, allDocuments, callMatch[1], line);
    if (symbol?.signature?.includes('->')) return symbol.signature.split('->').pop().trim();
    if (symbol?.type) return symbol.type;
    if (['Some', 'None'].includes(callMatch[1])) return 'Option';
    if (['Ok', 'Err'].includes(callMatch[1])) return 'Result';
  }
  const rawReferenceMatch = value.match(/^&\s*raw\s+([A-Za-z_]\w*)$/);
  if (rawReferenceMatch) {
    const symbol = resolveLocalOrGlobal(parsed, allDocuments, rawReferenceMatch[1], line);
    return symbol?.type ? `*${symbol.type}` : '';
  }
  const referenceMatch = value.match(/^&\s*(mut\s+)?([A-Za-z_]\w*)$/);
  if (referenceMatch) {
    const symbol = resolveLocalOrGlobal(parsed, allDocuments, referenceMatch[2], line);
    return `&${referenceMatch[1] ? 'mut ' : ''}${symbol?.type || ''}`.trim();
  }
  const derefMatch = value.match(/^\*\s*([A-Za-z_]\w*)$/);
  if (derefMatch) {
    const symbol = resolveLocalOrGlobal(parsed, allDocuments, derefMatch[1], line);
    return (symbol?.type || '').replace(/^\s*\*/, '').trim();
  }
  if (/^-?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|\d+)$/.test(value)) return 'int';
  if (/^-?(?:\d+\.\d+|\d+[eE][+-]?\d+|\d+\.\d+[eE][+-]?\d+)$/.test(value)) return 'float';
  const newMatch = value.match(/^new\s+([A-Za-z_]\w*)\b/);
  if (newMatch) return newMatch[1];
  if (/^\[.*\]$/.test(value)) return 'list';
  if (/^\{.*\}$/.test(value)) return 'map';
  const indexMatch = value.match(/^([A-Za-z_]\w*)\s*\[(.+)\]$/);
  if (indexMatch) {
    const symbol = resolveLocalOrGlobal(parsed, allDocuments, indexMatch[1], line);
    const type = symbol?.type || '';
    const listMatch = type.match(/^list\s*\(\s*(.+?)\s*\)$/);
    if (listMatch) return listMatch[1].trim();
    const mapMatch = type.match(/^map\s*(?:\[[^\]]+\])?\s*(?:\(\s*(.+?)\s*\))?/);
    if (mapMatch?.[1]) return mapMatch[1].trim();
    if (baseType(type) === 'string') return 'char';
    if (baseType(type) === 'list') return 'int';
    if (baseType(type) === 'map') return 'int';
  }
  const powerMatch = value.match(/^(.+?)\s*\*\*\s*(.+)$/);
  if (powerMatch) {
    const left = inferLiteralType(powerMatch[1], parsed, allDocuments, line);
    const right = inferLiteralType(powerMatch[2], parsed, allDocuments, line);
    if (isGenericType(left) || isGenericType(right)) return left || right || '';
    const result = numericResultType(left, right);
    if (result) return result;
  }
  const shiftMatch = value.match(/^(.+?)\s*(<<|>>)\s*(.+)$/);
  if (shiftMatch) {
    const left = inferLiteralType(shiftMatch[1], parsed, allDocuments, line);
    const right = inferLiteralType(shiftMatch[3], parsed, allDocuments, line);
    if (isIntegerType(left) && isIntegerType(right)) return left || 'int';
    if (left || right) return 'invalid';
  }
  const bitwiseMatch = value.match(/^(.+?)\s*([&|^])\s*(.+)$/);
  if (bitwiseMatch && !/^&\s*(?:mut\s+|raw\s+)?[A-Za-z_]\w*$/.test(value)) {
    const left = inferLiteralType(bitwiseMatch[1], parsed, allDocuments, line);
    const right = inferLiteralType(bitwiseMatch[3], parsed, allDocuments, line);
    const result = integerResultType(left, right);
    if (result) return result;
  }
  const bitwiseNotMatch = value.match(/^~\s*(.+)$/);
  if (bitwiseNotMatch) {
    const inner = inferLiteralType(bitwiseNotMatch[1], parsed, allDocuments, line);
    if (isIntegerType(inner)) return inner || 'int';
    if (inner) return 'invalid';
  }
  const compareMatch = value.match(/^(.+?)\s*(==|!=|<=|>=|<|>)\s*(.+)$/);
  if (compareMatch) return 'bool';
  const logicalMatch = value.match(/^(.+?)\s+(and|or)\s+(.+)$/);
  if (logicalMatch) return 'bool';
  const notMatch = value.match(/^!\s*(.+)$/);
  if (notMatch) return 'bool';
  const pointerArithmeticMatch = value.match(/^(.+?)\s*(\+|-)\s*(.+)$/);
  if (pointerArithmeticMatch) {
    const left = inferLiteralType(pointerArithmeticMatch[1], parsed, allDocuments, line);
    const right = inferLiteralType(pointerArithmeticMatch[3], parsed, allDocuments, line);
    if (isPointerType(left) && isIntegerType(right)) return left;
    if (pointerArithmeticMatch[2] === '+' && isIntegerType(left) && isPointerType(right)) return right;
  }
  const arithmeticMatch = value.match(/^(.+?)\s*(\+\+|\+|-|\*|\/|%)\s*(.+)$/);
  if (arithmeticMatch) {
    const op = arithmeticMatch[2];
    const left = inferLiteralType(arithmeticMatch[1], parsed, allDocuments, line);
    const right = inferLiteralType(arithmeticMatch[3], parsed, allDocuments, line);
    if (op === '++') return 'string';
    if (isGenericType(left) || isGenericType(right)) return left || right || '';
    if ((op === '+' || op === '-') && ((isPointerType(left) && isIntegerType(right)) || (op === '+' && isIntegerType(left) && isPointerType(right)))) return isPointerType(left) ? left : right;
    const result = numericResultType(left, right);
    if (result) return result;
  }
  const varMatch = value.match(/^([A-Za-z_]\w*)$/);
  if (varMatch) {
    const symbol = resolveLocalOrGlobal(parsed, allDocuments, varMatch[1], line);
    if (symbol?.type) return symbol.type;
  }
  return '';
}

function compatible(expected, actual) {
  const leftRaw = (expected || '').trim();
  const rightRaw = (actual || '').trim();
  const left = baseType(leftRaw);
  const right = baseType(rightRaw);
  if (!left || !right || left === 'auto' || right === 'null') return true;
  if (isGenericType(left) || isGenericType(right)) return true;
  if (leftRaw === rightRaw) return true;
  if (left === right) return true;
  if (left === 'float' && ['int', 'byte'].includes(right)) return true;
  if (left === 'int' && right === 'byte') return true;
  if (left === 'byte' && right === 'int') return true;
  if (isReferenceType(leftRaw) && isReferenceType(rightRaw) && left === right) return true;
  if (isPointerType(leftRaw) && isPointerType(rightRaw) && left === right) return true;
  return false;
}

function containingFunction(parsed, line) {
  return parsed.functions
    .filter((symbol) => symbol.range.start.line <= line && symbol.range.end.line >= line)
    .sort((left, right) => {
      const leftSize = left.range.end.line - left.range.start.line;
      const rightSize = right.range.end.line - right.range.start.line;
      if (leftSize !== rightSize) return leftSize - rightSize;
      return right.range.start.line - left.range.start.line;
    })[0];
}

function returnContext(parsed, line) {
  const contexts = [];
  for (let index = 0; index <= line; index += 1) {
    const raw = parsed.lines[index] || '';
    const cleaned = stripComment(raw).trim();
    const end = parsed.blockEnds?.get(index);
    if (end === undefined || end < line) continue;
    let match = cleaned.match(/^\s*(?:pub\s+)?(?:unsafe\s+)?fn\s+(?:[A-Za-z_]\w*\.)?[A-Za-z_]\w*(?:\[[^\]]*\])?\s*\([^)]*\)\s*(?:->\s*(.+?))?\s*:\s*$/);
    if (match) {
      contexts.push({ start: index, end, type: (match[1] || 'void').trim() });
      continue;
    }
    match = cleaned.match(/\bfn\s*\([^)]*\)\s*(?:->\s*(.+?))?\s*:\s*$/);
    if (match) contexts.push({ start: index, end, type: (match[1] || 'void').trim() });
  }
  if (contexts.length) {
    return contexts.sort((left, right) => {
      const leftSize = left.end - left.start;
      const rightSize = right.end - right.start;
      if (leftSize !== rightSize) return leftSize - rightSize;
      return right.start - left.start;
    })[0];
  }
  const fn = containingFunction(parsed, line);
  if (!fn) return undefined;
  return { start: fn.range.start.line, end: fn.range.end.line, type: fn.type || 'void' };
}

function isInsideBlock(parsed, line, kindMatcher) {
  for (const [start, end] of parsed.blockEnds.entries()) {
    if (start < line && line <= end && kindMatcher(stripComment(parsed.lines[start]).trim())) return true;
  }
  return false;
}

function analyzeSimpleTypeMismatches(parsed, allDocuments) {
  const diagnostics = [];
  for (let line = 0; line < parsed.lines.length; line += 1) {
    const raw = parsed.lines[line];
    const cleaned = stripComment(raw);
    let match = cleaned.match(/^\s*(?:let\s+)?(?:mut\s+)?([A-Za-z_]\w*)\s*:\s*(.+)\s*(?:->|<-|=)\s*(.+)$/);
    if (match) {
      const expected = match[2].trim();
      const expr = match[3].trim();
      const actual = inferLiteralType(expr, parsed, allDocuments, line);
      if (actual && !compatible(expected, actual)) {
        const start = raw.indexOf(expr);
        diagnostics.push(diagnostic(line, start, expr.length, `Type mismatch: expected ${expected}, found ${actual}.`, 'type-mismatch', DiagnosticSeverity.Error, { expected, actual }));
      }
      continue;
    }
    match = cleaned.match(/^\s*return\s+(.+)$/);
    if (match) {
      const context = returnContext(parsed, line);
      const expected = context?.type || '';
      if (expected && baseType(expected) !== 'void') {
        const expr = match[1].trim();
        const actual = inferLiteralType(expr, parsed, allDocuments, line);
        if (actual && !compatible(expected, actual)) {
          const start = raw.indexOf(expr);
          diagnostics.push(diagnostic(line, start, expr.length, `Return type mismatch: expected ${expected}, found ${actual}.`, 'return-type-mismatch', DiagnosticSeverity.Error, { expected, actual }));
        }
      }
    }
  }
  return diagnostics;
}

function analyzeAssignmentMutability(parsed) {
  const diagnostics = [];
  for (let line = 0; line < parsed.lines.length; line += 1) {
    const raw = parsed.lines[line];
    const cleaned = stripComment(raw);
    const match = cleaned.match(/^\s*([A-Za-z_]\w*)\s*(?:<-|=|\+=|-=|\*=|\/=|%=)/);
    if (!match) continue;
    const name = match[1];
    const symbol = language.visibleSymbols(parsed, line).find((candidate) => candidate.name === name);
    if (!symbol || symbol.data?.parameter) continue;
    if (!isMutableDeclaration(parsed.lines[symbol.selectionRange.start.line], name)) {
      diagnostics.push(diagnostic(line, raw.indexOf(name), name.length, `Cannot assign to immutable variable '${name}'.`, 'immutable-assignment', DiagnosticSeverity.Error, { name }));
    }
  }
  return diagnostics;
}

function analyzeControlFlow(parsed) {
  const diagnostics = [];
  for (let line = 0; line < parsed.lines.length; line += 1) {
    const raw = parsed.lines[line];
    const cleaned = stripComment(raw).trim();
    const match = cleaned.match(/^(break|continue)\b/);
    if (!match) continue;
    const inLoop = isInsideBlock(parsed, line, (open) => /^(?::[A-Za-z_]\w*\s+)?(?:while|for)\b/.test(open) || /^do\s*:/.test(open));
    if (!inLoop) diagnostics.push(diagnostic(line, raw.indexOf(match[1]), match[1].length, `'${match[1]}' is only valid inside a loop.`, 'invalid-loop-control'));
  }
  for (const fn of parsed.functions) {
    if (baseType(fn.type) === 'void') continue;
    const bodyLines = parsed.lines.slice(fn.range.start.line + 1, fn.range.end.line).map(stripComment).map((x) => x.trim()).filter(Boolean);
    if (!bodyLines.some((value) => /^return\b/.test(value))) {
      diagnostics.push(diagnostic(fn.selectionRange.start.line, fn.selectionRange.start.character, fn.name.length, `Function '${fn.name}' declares return type ${fn.type} but has no obvious return statement.`, 'missing-return', DiagnosticSeverity.Warning));
    }
  }
  return diagnostics;
}

function analyzeUnsafe(parsed) {
  const diagnostics = [];
  for (let line = 0; line < parsed.lines.length; line += 1) {
    const raw = parsed.lines[line];
    const cleaned = codeOnly(raw);
    const rawAddress = /&\s*raw\b|\braw\b/.test(cleaned);
    const rawDeref = /(^|(?:->|<-|=|\(|,|return)\s*)\*\s*[A-Za-z_]\w*/.test(cleaned) || /^\s*\*\s*[A-Za-z_]\w*\s*=/.test(cleaned);
    if (!rawAddress && !rawDeref) continue;
    const fn = containingFunction(parsed, line);
    const inUnsafeFunction = fn && /^\s*(?:pub\s+)?unsafe\s+fn\b/.test(stripComment(parsed.lines[fn.range.start.line]));
    const inUnsafeBlock = isInsideBlock(parsed, line, (open) => /^unsafe\s*:/.test(open));
    if (!inUnsafeFunction && !inUnsafeBlock) {
      const idx = Math.max(0, raw.search(/&\s*raw|\braw\b|\*\s*[A-Za-z_]\w*/));
      diagnostics.push(diagnostic(line, idx, 3, 'Raw pointer or unsafe operation requires an unsafe block.', 'unsafe-required', DiagnosticSeverity.Error));
    }
  }
  return diagnostics;
}

function analyzeDuplicateLocals(parsed) {
  const diagnostics = [];
  const byScope = new Map();
  for (const symbol of parsed.locals) {
    if (symbol.data?.parameter) continue;
    const key = `${symbol.containerName}:${symbol.scopeStart}:${symbol.scopeEnd}:${symbol.name}`;
    const previous = byScope.get(key);
    if (previous) {
      diagnostics.push({
        ...diagnostic(symbol.selectionRange.start.line, symbol.selectionRange.start.character, symbol.name.length, `Duplicate local declaration '${symbol.name}'.`, 'duplicate-local'),
        relatedInformation: [{ location: { uri: parsed.uri, range: previous.selectionRange }, message: 'First local declaration is here.' }],
      });
    } else byScope.set(key, symbol);
  }
  return diagnostics;
}

function isIgnoredIdentifierUse(parsed, token) {
  if (symbolDeclaredAt(parsed, token)) return true;
  const line = stripComment(parsed.lines[token.line] || '').trim();
  if (!line || DECLARATION_HEADS.test(line)) return true;
  const previous = language.previousToken(parsed, token);
  const next = language.nextToken(parsed, token);
  if (previous?.text === '.') return true;
  if (next?.text === ':') {
    const rawLine = stripComment(parsed.lines[token.line] || '');
    const before = rawLine.slice(0, token.column);
    if (/new\s+[A-Za-z_]\w*\s*\([^)]*$/.test(before) || /[,({[]\s*$/.test(before)) return true;
    if (/^\s*[A-Za-z_]\w*\s*:/.test(line)) return true;
  }
  if (previous?.text === 'new') return true;
  if (previous?.text === 'require') return true;
  if (tokenInsideClosureParamList(parsed, token)) return true;
  if (tokenInsideTypeSyntax(parsed, token)) return true;
  if (isGenericType(token.text)) return true;
  return false;
}

function closestOutOfScopeLocal(parsed, name, line) {
  return parsed.locals
    .filter((symbol) => symbol.name === name && !(symbol.scopeStart <= line && symbol.scopeEnd >= line))
    .sort((left, right) => {
      const leftDistance = Math.min(Math.abs(line - left.scopeStart), Math.abs(line - left.scopeEnd), Math.abs(line - left.selectionRange.start.line));
      const rightDistance = Math.min(Math.abs(line - right.scopeStart), Math.abs(line - right.scopeEnd), Math.abs(line - right.selectionRange.start.line));
      return leftDistance - rightDistance;
    })[0];
}


function resolvePatternBinding(parsed, name, line) {
  for (const [start, end] of parsed.blockEnds.entries()) {
    if (start > line || end < line) continue;
    const cleaned = stripComment(parsed.lines[start] || '').trim();
    const ifLet = cleaned.match(/^if\s+let\s+[A-Za-z_]\w*\s*\(\s*([A-Za-z_]\w*)\s*\)\s*=/);
    if (ifLet?.[1] === name) return { name, kind: SymbolKind.Variable, type: '', data: { pattern: true } };
    const caseBind = cleaned.match(/^case\s+[A-Za-z_][\w.]*\s*\(\s*([A-Za-z_]\w*)\s*\)\s*:/);
    if (caseBind?.[1] === name) return { name, kind: SymbolKind.Variable, type: '', data: { pattern: true } };
  }
  return undefined;
}

function resolveClosureParameter(parsed, name, line) {
  for (const [start, end] of parsed.blockEnds.entries()) {
    if (start > line || end < line) continue;
    const cleaned = stripComment(parsed.lines[start] || '');
    const fnIndex = cleaned.indexOf('fn(');
    if (fnIndex < 0) continue;
    const match = cleaned.slice(fnIndex).match(/^fn\s*\(([^)]*)\)/);
    if (!match) continue;
    for (const part of language.splitTopLevel(match[1])) {
      const param = part.trim().match(/^([A-Za-z_]\w*)\s*:\s*(.+)$/);
      if (param?.[1] === name) return { name, kind: SymbolKind.Variable, type: param[2].trim(), data: { parameter: true } };
    }
  }
  return undefined;
}

function tokenInsideClosureParamList(parsed, token) {
  const cleaned = stripComment(parsed.lines[token.line] || '');
  const fnIndex = cleaned.indexOf('fn(');
  if (fnIndex < 0) return false;
  const open = cleaned.indexOf('(', fnIndex);
  const close = cleaned.indexOf(')', open);
  return open >= 0 && close >= 0 && token.column > open && token.column < close;
}

function tokenInsideTypeSyntax(parsed, token) {
  const line = stripComment(parsed.lines[token.line] || '');
  const before = line.slice(0, token.column);
  const after = line.slice(token.column + token.text.length);
  if (/[<&*?]\s*$/.test(before)) return true;
  if (/^\s*[>\]]/.test(after) && /[<,(]\s*$/.test(before)) return true;
  if (/^\s*(?:let\s+)?(?:mut\s+)?[A-Za-z_]\w*\s*:[^=]*$/.test(before) && !/(?:->|<-|=)/.test(before)) return true;
  return false;
}

function analyzeUndefinedNames(parsed, allDocuments) {
  const diagnostics = [];
  const typeNames = new Set([
    ...language.BUILTIN_TYPES,
    ...parsed.types.map((symbol) => symbol.name),
    ...(allDocuments || []).flatMap((doc) => doc.types.map((symbol) => symbol.name)),
  ]);
  const globallyKnown = new Set([
    ...language.KEYWORDS,
    ...language.BUILTINS.map((item) => item.name),
    ...language.STANDARD_MODULES,
    ...typeNames,
    'true', 'false', 'null', 'self', 'thread', 'auto', 'wide',
  ]);
  for (const token of parsed.tokens) {
    if (token.kind !== 'identifier') continue;
    if (globallyKnown.has(token.text)) continue;
    if (isIgnoredIdentifierUse(parsed, token)) continue;
    if (resolveLocalOrGlobal(parsed, allDocuments, token.text, token.line)) continue;

    const outOfScope = closestOutOfScopeLocal(parsed, token.text, token.line);
    if (outOfScope) {
      diagnostics.push({
        ...diagnostic(token.line, token.column, token.text.length, `Variable '${token.text}' is out of scope.`, 'out-of-scope', DiagnosticSeverity.Error, { name: token.text }),
        relatedInformation: [{
          location: { uri: parsed.uri, range: outOfScope.selectionRange },
          message: `Declared in a scope that ends on line ${outOfScope.scopeEnd + 1}.`,
        }],
      });
      continue;
    }

    diagnostics.push(diagnostic(token.line, token.column, token.text.length, `Undefined symbol '${token.text}'.`, 'undefined-symbol', DiagnosticSeverity.Warning, { name: token.text }));
  }
  return diagnostics;
}

function analyzeCallArity(parsed, allDocuments) {
  const diagnostics = [];
  const functions = new Map();
  for (const doc of [parsed, ...(allDocuments || [])]) {
    for (const fn of doc.functions || []) if (!functions.has(fn.name)) functions.set(fn.name, fn);
  }
  for (let line = 0; line < parsed.lines.length; line += 1) {
    const raw = parsed.lines[line];
    const cleaned = stripComment(raw);
    if (/^\s*(?:pub\s+)?(?:unsafe\s+)?fn\b/.test(cleaned)) continue;
    for (const match of cleaned.matchAll(/\b([A-Za-z_]\w*)\s*\(([^()]*)\)/g)) {
      const name = match[1];
      if (['if', 'while', 'for', 'match', 'return', 'new'].includes(name)) continue;
      const fn = functions.get(name);
      if (!fn?.data?.params) continue;
      const argsText = match[2].trim();
      const args = argsText ? language.splitTopLevel(argsText) : [];
      const isMethodCall = cleaned[match.index - 1] === '.' && fn.kind === SymbolKind.Method;
      const expected = fn.data.params.length - (isMethodCall ? 1 : 0);
      if (args.length !== expected) {
        diagnostics.push(diagnostic(line, raw.indexOf(name, match.index), name.length, `Expected ${expected} argument${expected === 1 ? '' : 's'} for '${name}', got ${args.length}.`, 'argument-count-mismatch', DiagnosticSeverity.Warning));
      }
    }
  }
  return diagnostics;
}


function isUnaryReferenceOperator(cleaned, op, opIndex, leftText) {
  if (op !== '&') return false;
  if (/[:=,([{<>-]$/.test((leftText || '').trim())) return true;
  const before = cleaned.slice(0, opIndex).trimEnd();
  if (!before) return true;
  if (/(?:->|<-|=|:|,|\(|\[|\{|return)$/.test(before)) return true;
  if (/^\s*(?:let\s+)?(?:mut\s+)?[A-Za-z_]\w*\s*:\s*$/.test(before)) return true;
  return false;
}

function topLevelLogicalOperators(value) {
  const operators = [];
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
    if (round !== 0 || square !== 0 || brace !== 0) continue;
    const match = value.slice(index).match(/^(and|or)\b/);
    if (!match || (index > 0 && /[A-Za-z0-9_]/.test(value[index - 1]))) continue;
    operators.push({ index, operator: match[1] });
    index += match[1].length - 1;
  }
  return operators;
}

function logicalOperandBounds(value, operatorIndex, operatorLength) {
  let start = 0;
  let round = 0;
  let square = 0;
  let brace = 0;
  for (let index = operatorIndex - 1; index >= 0; index -= 1) {
    const character = value[index];
    if (character === ')') { round += 1; continue; }
    if (character === ']') { square += 1; continue; }
    if (character === '}') { brace += 1; continue; }
    if (character === '(') {
      if (round === 0) { start = index + 1; break; }
      round -= 1;
      continue;
    }
    if (character === '[') {
      if (square === 0) { start = index + 1; break; }
      square -= 1;
      continue;
    }
    if (character === '{') {
      if (brace === 0) { start = index + 1; break; }
      brace -= 1;
      continue;
    }
    if (round !== 0 || square !== 0 || brace !== 0) continue;
    const pair = value.slice(Math.max(0, index - 1), index + 1);
    const assignment = character === '='
      && value[index - 1] !== '='
      && !/[!<>]/.test(value[index - 1] || '')
      && value[index + 1] !== '=';
    if (character === ',' || character === ':' || character === '?' || pair === '->' || pair === '<-' || assignment) {
      start = index + 1;
      break;
    }
  }

  let end = value.length;
  round = 0;
  square = 0;
  brace = 0;
  for (let index = operatorIndex + operatorLength; index < value.length; index += 1) {
    const character = value[index];
    if (character === '(') { round += 1; continue; }
    if (character === '[') { square += 1; continue; }
    if (character === '{') { brace += 1; continue; }
    if (character === ')') {
      if (round === 0) { end = index; break; }
      round -= 1;
      continue;
    }
    if (character === ']') {
      if (square === 0) { end = index; break; }
      square -= 1;
      continue;
    }
    if (character === '}') {
      if (brace === 0) { end = index; break; }
      brace -= 1;
      continue;
    }
    if (round === 0 && square === 0 && brace === 0 && (character === ',' || character === ':' || character === '?')) {
      end = index;
      break;
    }
  }
  return { start, end };
}

function logicalOperations(value) {
  const operations = [];
  for (const match of value.matchAll(/\b(and|or)\b/g)) {
    const operator = match[1];
    const index = match.index;
    const bounds = logicalOperandBounds(value, index, operator.length);
    let left = value.slice(bounds.start, index).trim().replace(/^(?:if|elif|while|return)\s+/, '');
    let right = value.slice(index + operator.length, bounds.end).trim();
    const previous = topLevelLogicalOperators(left).pop();
    if (previous) left = left.slice(previous.index + previous.operator.length).trim();
    const next = topLevelLogicalOperators(right)[0];
    if (next) right = right.slice(0, next.index).trim();
    operations.push({ operator, index, left, right });
  }
  return operations;
}

function analyzeExpressionOperators(parsed, allDocuments) {
  const diagnostics = [];
  function typeOf(expr, line) {
    let value = expr.trim();
    while (/^\(.+\)$/.test(value)) value = value.slice(1, -1).trim();
    return inferLiteralType(value, parsed, allDocuments, line);
  }
  for (let line = 0; line < parsed.lines.length; line += 1) {
    const raw = parsed.lines[line];
    const cleaned = codeOnly(raw);
    const condition = cleaned.match(/^\s*(if|while)\s+(.+?)\s*:/);
    if (condition) {
      const expr = condition[2].trim();
      const actual = typeOf(expr, line);
      if (actual && actual !== 'bool') diagnostics.push(diagnostic(line, raw.indexOf(expr), expr.length, `${condition[1]} condition must be bool, found ${actual}.`, 'condition-type-mismatch'));
    }
    for (const match of cleaned.matchAll(/!\s*([^\s:)]+)/g)) {
      const expr = match[1].trim();
      const actual = typeOf(expr, line);
      if (actual && actual !== 'bool') diagnostics.push(diagnostic(line, match.index, match[0].length, `Operator 'not' requires bool, found ${actual}.`, 'operator-type-mismatch'));
    }
    for (const operation of logicalOperations(cleaned)) {
      const left = typeOf(operation.left, line);
      const right = typeOf(operation.right, line);
      if ((left && left !== 'bool') || (right && right !== 'bool')) diagnostics.push(diagnostic(line, operation.index, operation.operator.length, `Operator '${operation.operator}' requires bool operands.`, 'operator-type-mismatch'));
    }
    for (const match of cleaned.matchAll(/([^\s,()]+)\s*(<<|>>|[&|^])\s*([^\s,()]+)/g)) {
      const op = match[2];
      const opIndex = raw.indexOf(op, match.index);
      if (isUnaryReferenceOperator(cleaned, op, opIndex, match[1])) continue;
      const left = typeOf(match[1], line);
      const right = typeOf(match[3], line);
      if (shouldSkipOperatorType(left) || shouldSkipOperatorType(right)) continue;
      if ((left && !isIntegerType(left)) || (right && !isIntegerType(right))) diagnostics.push(diagnostic(line, opIndex, op.length, `Operator '${op}' requires integer operands.`, 'operator-type-mismatch'));
    }
    for (const match of cleaned.matchAll(/~\s*([^\s,()]+)/g)) {
      const actual = typeOf(match[1], line);
      if (actual && !isIntegerType(actual)) diagnostics.push(diagnostic(line, match.index, match[0].length, `Operator '~' requires an integer operand.`, 'operator-type-mismatch'));
    }
    for (const match of cleaned.matchAll(/([^\s,()]+)\s*(\*\*)\s*([^\s,()]+)/g)) {
      const op = match[2];
      const opIndex = raw.indexOf(op, match.index);
      const left = typeOf(match[1], line);
      const right = typeOf(match[3], line);
      if (shouldSkipOperatorType(left) || shouldSkipOperatorType(right)) continue;
      if ((left && !isNumericType(left)) || (right && !isNumericType(right))) diagnostics.push(diagnostic(line, opIndex, op.length, `Operator '${op}' requires numeric operands.`, 'operator-type-mismatch'));
    }
    for (const match of cleaned.matchAll(/(?:^|[^A-Za-z0-9_])(\+\+|--)([A-Za-z_]\w*)|([A-Za-z_]\w*)(\+\+|--)(?:$|[^A-Za-z0-9_])/g)) {
      const name = match[2] || match[3];
      const op = match[1] || match[4];
      const opIndex = raw.indexOf(op, match.index);
      const actual = typeOf(name, line);
      if (shouldSkipOperatorType(actual)) continue;
      if (actual && !isIntegerType(actual)) diagnostics.push(diagnostic(line, opIndex, op.length, `Operator '${op}' requires an integer operand.`, 'operator-type-mismatch'));
    }
    for (const match of cleaned.matchAll(/([^\s,()]+)\s*(\+\+|\+|-|\*|\/|%)\s*([^\s,()]+)/g)) {
      const op = match[2];
      const opIndex = raw.indexOf(op, match.index);
      if (op === '-' && (cleaned[opIndex + 1] === '>' || cleaned[opIndex - 1] === '<')) continue;
      if (op === '*' && (cleaned[opIndex + 1] === '*' || cleaned[opIndex - 1] === '*')) continue;
      const left = typeOf(match[1], line);
      const right = typeOf(match[3], line);
      if (shouldSkipOperatorType(left) || shouldSkipOperatorType(right)) continue;
      if (op === '++') {
        if ((left && left !== 'string') || (right && right !== 'string')) diagnostics.push(diagnostic(line, opIndex, op.length, `Operator '${op}' requires string operands.`, 'operator-type-mismatch'));
        continue;
      }
      if ((op === '+' || op === '-') && ((isPointerType(left) && isIntegerType(right)) || (op === '+' && isIntegerType(left) && isPointerType(right)))) continue;
      if ((left && !isNumericType(left)) || (right && !isNumericType(right))) diagnostics.push(diagnostic(line, opIndex, op.length, `Operator '${op}' requires numeric operands.`, 'operator-type-mismatch'));
    }
  }
  return diagnostics;
}


function isCopyType(type, parsed) {
  const base = baseType(type || '');
  if (!base) return true;
  if (base.startsWith('&') || /^\*/.test(base)) return true;
  if (['int', 'float', 'char', 'byte', 'bool', 'void', 'null'].includes(base)) return true;
  return false;
}

function symbolAtLine(parsed, name, line) {
  return language.visibleSymbols(parsed, line).find((symbol) => symbol.name === name);
}

function ignoredMoveUse(parsed, token, lineInfo) {
  if (symbolDeclaredAt(parsed, token)) return true;
  if (lineInfo.assignedName === token.text) return true;
  if (lineInfo.declaredName === token.text) return true;
  const previous = language.previousToken(parsed, token);
  if (previous?.text === '.' || previous?.text === 'new') return true;
  return false;
}

function callMovesFromLine(parsed, allDocuments, cleaned) {
  const moves = [];
  const functions = new Map();
  for (const doc of [parsed, ...(allDocuments || [])]) {
    for (const fn of doc.functions || []) if (!functions.has(fn.name)) functions.set(fn.name, fn);
  }
  for (const match of cleaned.matchAll(/\b([A-Za-z_]\w*)\s*\(([^()]*)\)/g)) {
    const name = match[1];
    if (['print', 'println', 'format', 'len', 'assert', 'assert_eq', 'Some', 'Ok', 'Err', 'if', 'while', 'for', 'match', 'new'].includes(name)) continue;
    const fn = functions.get(name);
    if (!fn?.data?.params) continue;
    const args = language.splitTopLevel(match[2]).map((arg) => arg.trim());
    fn.data.params.forEach((param, index) => {
      const arg = args[index];
      if (!arg || !/^[A-Za-z_]\w*$/.test(arg)) return;
      if (!isCopyType(param.type, parsed)) moves.push({ name: arg, reason: `passed to '${name}'` });
    });
  }
  return moves;
}

function analyzeMoveAndBorrow(parsed, allDocuments) {
  const diagnostics = [];
  const moved = new Map();
  const sharedBorrows = new Map();
  const mutBorrows = new Map();

  function markMove(name, line, reason) {
    const symbol = symbolAtLine(parsed, name, line);
    if (!symbol || isCopyType(symbol.type, parsed)) return;
    moved.set(name, { line, reason });
  }

  function addBorrow(target, owner, mutable, line, column) {
    const targetSymbol = symbolAtLine(parsed, target, line);
    if (!targetSymbol) return;
    if (mutable && !isMutableDeclaration(parsed.lines[targetSymbol.selectionRange.start.line], target)) {
      diagnostics.push(diagnostic(line, column, target.length, `Cannot take mutable reference to immutable variable '${target}'.`, 'mut-borrow-immutable', DiagnosticSeverity.Error, { name: target }));
    }
    if (mutable && sharedBorrows.has(target)) {
      diagnostics.push(diagnostic(line, column, target.length, `Cannot mutably borrow '${target}' while it is shared borrowed.`, 'shared-then-mut-borrow', DiagnosticSeverity.Error, { name: target }));
    }
    if (!mutable && mutBorrows.has(target)) {
      diagnostics.push(diagnostic(line, column, target.length, `Cannot shared borrow '${target}' while it is mutably borrowed.`, 'mut-then-shared-borrow', DiagnosticSeverity.Error, { name: target }));
    }
    if (mutable) mutBorrows.set(target, { owner, line });
    else sharedBorrows.set(target, { owner, line });
  }

  for (let line = 0; line < parsed.lines.length; line += 1) {
    const raw = parsed.lines[line] || '';
    const cleaned = stripComment(raw);
    const declaration = cleaned.match(/^\s*(?:let\s+)?(?:mut\s+)?([A-Za-z_]\w*)\s*:\s*([^<-=]+?)\s*(?:->|=)\s*(.+)$/);
    const assignment = cleaned.match(/^\s*([A-Za-z_]\w*)\s*(?:<-|=|\+=|-=|\*=|\/=|%=)/);
    const lineInfo = { declaredName: declaration?.[1], assignedName: assignment?.[1] };

    if (assignment?.[1]) {
      moved.delete(assignment[1]);
      if (sharedBorrows.has(assignment[1]) || mutBorrows.has(assignment[1])) {
        diagnostics.push(diagnostic(line, raw.indexOf(assignment[1]), assignment[1].length, `Cannot assign to '${assignment[1]}' while it is borrowed.`, 'assign-while-borrowed', DiagnosticSeverity.Error, { name: assignment[1] }));
      }
    }

    const seenMoved = new Set();
    const seenBorrowUse = new Set();
    for (const token of parsed.tokens.filter((item) => item.line === line && item.kind === 'identifier')) {
      if (ignoredMoveUse(parsed, token, lineInfo)) continue;
      if (moved.has(token.text) && !seenMoved.has(token.text)) {
        const info = moved.get(token.text);
        diagnostics.push(diagnostic(token.line, token.column, token.text.length, `Use of moved value '${token.text}'.`, 'use-after-move', DiagnosticSeverity.Error, { name: token.text, movedAt: info.line + 1 }));
        seenMoved.add(token.text);
      }
      if (mutBorrows.has(token.text) && !seenBorrowUse.has(token.text)) {
        const info = mutBorrows.get(token.text);
        if (line > info.line) {
          diagnostics.push(diagnostic(token.line, token.column, token.text.length, `Cannot use '${token.text}' while it is mutably borrowed.`, 'use-while-mut-borrowed', DiagnosticSeverity.Error, { name: token.text }));
          seenBorrowUse.add(token.text);
        }
      }
    }

    const borrow = declaration?.[3]?.trim().match(/^&\s*(mut\s+)?([A-Za-z_]\w*)$/);
    if (declaration && borrow) {
      addBorrow(borrow[2], declaration[1], Boolean(borrow[1]), line, raw.lastIndexOf(borrow[2]));
    }

    if (declaration) {
      const expr = declaration[3].trim();
      const source = expr.match(/^([A-Za-z_]\w*)$/);
      if (source) markMove(source[1], line, `assigned to '${declaration[1]}'`);
    }

    for (const move of callMovesFromLine(parsed, allDocuments, cleaned)) {
      markMove(move.name, line, move.reason);
    }
  }

  return diagnostics;
}

function analyzeUnreachable(parsed) {
  const diagnostics = [];
  for (let line = 0; line < parsed.lines.length - 1; line += 1) {
    const cleaned = stripComment(parsed.lines[line]).trim();
    if (!/^(return|break|continue)\b/.test(cleaned)) continue;
    let next = line + 1;
    while (next < parsed.lines.length && !stripComment(parsed.lines[next]).trim()) next += 1;
    if (next >= parsed.lines.length) continue;
    const nextText = stripComment(parsed.lines[next]).trim();
    if (/^(end|elif|else|case)\b/.test(nextText)) continue;
    diagnostics.push(diagnostic(next, parsed.lines[next].search(/\S|$/), Math.max(1, nextText.length), 'Unreachable code after control flow statement.', 'unreachable-code', DiagnosticSeverity.Warning));
  }
  return diagnostics;
}

function analyzeParsed(parsed, allDocuments = []) {
  const docs = allDocuments.filter((doc) => doc && doc.uri !== parsed.uri);
  return [
    ...analyzeUndefinedNames(parsed, docs),
    ...analyzeSimpleTypeMismatches(parsed, docs),
    ...analyzeAssignmentMutability(parsed),
    ...analyzeDuplicateLocals(parsed),
    ...analyzeControlFlow(parsed),
    ...analyzeUnsafe(parsed),
    ...analyzeExpressionOperators(parsed, docs),
    ...analyzeCallArity(parsed, docs),
    ...analyzeMoveAndBorrow(parsed, docs),
    ...analyzeUnreachable(parsed),
  ];
}

module.exports = {
  analyzeParsed,
  analyzeDocument: analyzeParsed,
  analyzeParsedDocument: analyzeParsed,
  analyze: analyzeParsed,
  diagnosticsForParsedDocument: analyzeParsed,
  inferLiteralType,
  compatible,
};
