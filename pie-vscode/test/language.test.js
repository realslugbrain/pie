'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { SymbolKind } = require('vscode-languageserver/node');
const { formatDocument, lex, parseDocument, symbolAt, tokenAt } = require('../server/language');
const analyzer = require('../server/analyzer');

const sample = `require "io"

struct User:
    id: int
    name: string
end

enum State:
    case Ready
    case Failed(string)
end

fn User.rename(self: &mut User, value: string):
    self.name = value
end

fn main() -> int:
    let mut user: User -> new User(id: 1, name: "Ada")
    user.rename("Grace")
    return 0
end
`;

test('lexer recognizes Pie tokens without false diagnostics', () => {
  const result = lex(sample);
  assert.equal(result.diagnostics.length, 0);
  assert.ok(result.tokens.some((token) => token.kind === 'keyword' && token.text === 'struct'));
  assert.ok(result.tokens.some((token) => token.kind === 'string' && token.text === '"Ada"'));
  assert.ok(result.tokens.some((token) => token.kind === 'operator' && token.text === '->'));
});

test('parser indexes custom language symbols', () => {
  const parsed = parseDocument('file:///sample.pie', sample);
  assert.equal(parsed.diagnostics.length, 0);
  const user = parsed.types.find((symbol) => symbol.name === 'User');
  const state = parsed.types.find((symbol) => symbol.name === 'State');
  const rename = parsed.functions.find((symbol) => symbol.name === 'rename');
  const local = parsed.locals.find((symbol) => symbol.name === 'user');
  assert.equal(user.kind, SymbolKind.Struct);
  assert.deepEqual(user.children.map((field) => field.name), ['id', 'name']);
  assert.equal(state.kind, SymbolKind.Enum);
  assert.deepEqual(state.children.map((variant) => variant.name), ['Ready', 'Failed']);
  assert.equal(rename.kind, SymbolKind.Method);
  assert.equal(rename.receiver, 'User');
  assert.deepEqual(rename.data.params.map((parameter) => parameter.name), ['self', 'value']);
  assert.equal(local.type, 'User');
});

test('symbol lookup resolves a local binding', () => {
  const parsed = parseDocument('file:///sample.pie', sample);
  const line = parsed.lines.findIndex((value) => value.includes('user.rename'));
  const character = parsed.lines[line].indexOf('user') + 1;
  assert.equal(tokenAt(parsed, { line, character }).text, 'user');
  assert.equal(symbolAt(parsed, { line, character }).type, 'User');
});

test('live analysis reports missing blocks and delimiters', () => {
  const parsed = parseDocument('file:///broken.pie', 'fn main():\n    print((1)\n');
  assert.ok(parsed.diagnostics.some((item) => item.code === 'missing-end'));
  assert.ok(parsed.diagnostics.some((item) => item.code === 'unclosed-delimiter'));
});

test('formatter applies stable block indentation', () => {
  const input = 'fn main():\nlet x: int -> 1\nif x > 0:\nprintln(x)\nelse:\nprintln(0)\nend\nreturn 0\nend\n';
  const expected = 'fn main():\n    let x: int -> 1\n    if x > 0:\n        println(x)\n    else:\n        println(0)\n    end\n    return 0\nend\n';
  assert.equal(formatDocument(input, { tabSize: 4, insertSpaces: true }), expected);
  assert.equal(formatDocument(expected, { tabSize: 4, insertSpaces: true }), expected);
});

test('semantic analyzer reports simple type mismatch and immutable assignment', () => {
  const source = `fn main() -> int:
    let name: string -> "Ada"
    name <- "Grace"
    let age: int -> "old"
    return 0
end
`;
  const parsed = parseDocument('file:///semantics.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.ok(diagnostics.some((item) => item.code === 'immutable-assignment'));
  assert.ok(diagnostics.some((item) => item.code === 'type-mismatch'));
});

test('semantic analyzer reports unsafe operation outside unsafe block', () => {
  const source = `fn main() -> int:
    let ptr: int -> raw value
    return 0
end
`;
  const parsed = parseDocument('file:///unsafe.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.ok(diagnostics.some((item) => item.code === 'unsafe-required'));
});

test('parser narrows local scope for anonymous blocks', () => {
  const source = `:
    inside: int -> 1
end

println(inside)
`;
  const parsed = parseDocument('file:///anonymous_block_scope_leak_error.pie', source);
  const local = parsed.locals.find((symbol) => symbol.name === 'inside');
  assert.equal(local.scopeStart, 0);
  assert.equal(local.scopeEnd, 2);
  assert.equal(languageVisible(parsed, 4).some((symbol) => symbol.name === 'inside'), false);
});

test('semantic analyzer reports anonymous block scope leaks', () => {
  const source = `:
    inside: int -> 1
end

println(inside)
`;
  const parsed = parseDocument('file:///anonymous_block_scope_leak_error.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.ok(diagnostics.some((item) => item.code === 'out-of-scope'));
});

test('semantic analyzer accepts integer bitwise and shift expressions', () => {
  const source = `fn main() -> int:
    a: int -> 12
    b: int -> 10
    r1: int -> a & b
    r2: int -> a | b
    r3: int -> a ^ b
    r4: int -> a << 2
    r5: int -> a >> 1
    r6: int -> ~0
    return 0
end
`;
  const parsed = parseDocument('file:///bitwise_basic.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.filter((item) => item.code === 'type-mismatch' || item.code === 'operator-type-mismatch').length, 0);
});

test('semantic analyzer accepts comparisons as logical operands', () => {
  const source = `fn main() -> int:
    let by_three: int -> 3
    let by_five: int -> 5
    if by_three == 3 and by_five == 5:
        println("FizzBuzz")
    end
    return 0
end
`;
  const parsed = parseDocument('file:///logical_comparisons.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => item.code === 'operator-type-mismatch'), false);
});

test('semantic analyzer understands method arity and struct-field arithmetic', () => {
  const source = `struct Counter:
    value: int
end

fn Counter.add(self: &mut Counter, amount: int) -> int:
    let next: int -> self.value + amount
    self.value = next
    return next
end

fn main() -> int:
    let mut counter: Counter -> new Counter(value: 1)
    let result: int -> counter.add(2)
    return result
end
`;
  const parsed = parseDocument('file:///method_field_arithmetic.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => ['type-mismatch', 'operator-type-mismatch', 'argument-count-mismatch'].includes(item.code)), false);
});

test('semantic analyzer rejects non-boolean logical operands', () => {
  const source = `fn main() -> int:
    let count: int -> 3
    if count and true:
        println(count)
    end
    return 0
end
`;
  const parsed = parseDocument('file:///logical_type_error.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.ok(diagnostics.some((item) => item.code === 'operator-type-mismatch'));
});

test('semantic analyzer reports moved values and borrow conflicts', () => {
  const source = `fn take(value: string):
    println(value)
end

fn main() -> int:
    mut name: string -> "Simon"
    take(name)
    println(name)
    shared: &string -> &name
    name <- "Grace"
    return 0
end
`;
  const parsed = parseDocument('file:///move_borrow.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.ok(diagnostics.some((item) => item.code === 'use-after-move'));
  assert.ok(diagnostics.some((item) => item.code === 'assign-while-borrowed'));
});

test('semantic analyzer reports mutable borrow of immutable values', () => {
  const source = `fn main() -> int:
    name: string -> "Simon"
    active: &mut string -> &mut name
    return 0
end
`;
  const parsed = parseDocument('file:///mut_borrow_immutable.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.ok(diagnostics.some((item) => item.code === 'mut-borrow-immutable'));
});

function languageVisible(parsed, line) {
  return require('../server/language').visibleSymbols(parsed, line);
}

test('semantic analyzer checks closure returns against the inner return type', () => {
  const source = `fn main() -> int:
    let s: string -> "hello"
    let f: () -> string -> fn() -> string:
        return s
    end
    println("ok")
    return 0
end
`;
  const parsed = parseDocument('file:///borrowcheck_closure_capture_moved.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => item.code === 'return-type-mismatch'), false);
});

test('semantic analyzer accepts reference expressions as references, not bitwise operators', () => {
  const source = `fn main() -> int:
    let nums: list(int) -> [1, 2, 3]
    let r: &list(int) -> &nums
    let x: int -> nums[0]
    println(x)
    return 0
end
`;
  const parsed = parseDocument('file:///borrowcheck_index_while_shared_borrowed.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => item.code === 'operator-type-mismatch'), false);
});

test('semantic analyzer ignores operators inside strings, arrows, and type widths', () => {
  const source = `value: int -> 1

:
    value: int -> 2
    borrowed: string -> "scoped"
    view: &string -> &borrowed
    println("block-inner=", value, " ", view)
end

println("block-outer=", value)

x: float<32> -> 3.14
y: float<64> -> 2.718281828
println(x)
println(y)
`;
  const parsed = parseDocument('file:///false_positive_strings_types.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => item.code === 'operator-type-mismatch' || item.code === 'unsafe-required'), false);
});

test('semantic analyzer accepts closure parameters and pattern bindings', () => {
  const source = `fn main() -> int:
    let add: (int, int) -> int -> fn(a: int, b: int) -> int:
        return a + b
    end
    let result: int -> add(3, 4)
    let x: Option(int) -> Some(42)
    if let Some(val) = x:
        println(val)
    else:
        println(0)
    end
    return result
end
`;
  const parsed = parseDocument('file:///closure_params_if_let.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => item.code === 'undefined-symbol'), false);
});

test('semantic analyzer accepts simple generic return flow', () => {
  const source = `fn identity[T](x: T) -> T:
    return x
end

fn main() -> int:
    val: int -> identity(42)
    print(val)
    return 0
end
`;
  const parsed = parseDocument('file:///generic_fn_basic.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => item.code === 'type-mismatch' || item.code === 'return-type-mismatch'), false);
});


test('semantic analyzer accepts sized and wide numeric arithmetic', () => {
  const source = `x: int<8> -> 10
y: int<8> -> 20
z: int<8> -> x + y

big: int<wide> -> 999999999999999999
bigger: int<wide> -> big * big

f: float<wide> -> 3.14159
g: float<wide> -> f * 2
`;
  const parsed = parseDocument('file:///wide_numeric.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => item.code === 'operator-type-mismatch' || item.code === 'type-mismatch'), false);
});

test('semantic analyzer accepts ternary expressions and reference-return calls', () => {
  const source = `fn keep(value: &string) -> &string:
    return value
end

fn main() -> int:
    name: string -> "Simon"
    view: &string -> keep(&name)
    x: int -> 10
    y: int -> x < 5 ? 100 : 200
    return y
end
`;
  const parsed = parseDocument('file:///ternary_reference_return.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => item.code === 'type-mismatch' || item.code === 'return-type-mismatch'), false);
});

test('semantic analyzer accepts raw pointer arithmetic and typed raw operations', () => {
  const source = `unsafe fn read_byte(ptr: *byte) -> byte:
    return *ptr
end

fn main() -> int:
    mut b: byte -> 1
    mut n: int<32> -> 7
    unsafe:
        bp: *byte -> &raw b
        p: *int<32> -> &raw n
        same1: *int<32> -> p + 0
        same2: *int<32> -> 0 + p
        same3: *int<32> -> same1 - 0
        loaded: byte -> read_byte(bp)
        *bp = 255
    end
    return 0
end
`;
  const parsed = parseDocument('file:///raw_pointer_valid.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => item.code === 'type-mismatch' || item.code === 'operator-type-mismatch'), false);
});


test('semantic analyzer accepts inc, dec, and power expressions', () => {
  const source = `fn main() -> int:
    mut x: int -> 5
    a: int -> x++
    b: int -> ++x
    c: int -> x--
    d: int -> --x
    e: int -> 2 ** 10
    f: float -> 2.0 ** 3.0
    return 0
end
`;
  const parsed = parseDocument('file:///operators_plus.pie', source);
  const diagnostics = analyzer.analyzeParsed(parsed, [parsed]);
  assert.equal(diagnostics.some((item) => item.code === 'type-mismatch' || item.code === 'operator-type-mismatch'), false);
});
