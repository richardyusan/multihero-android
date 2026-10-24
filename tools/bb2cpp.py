#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
bb2cpp - Blitz3D/Blitz2D BASIC -> C++ transpiler (2D command subset).

Usage: bb2cpp.py main.bb out.cpp

Semantics follow the Blitz3D compiler as closely as practical:
  * case-insensitive identifiers, type suffixes % # $
  * int = 32-bit, float = 32-bit, float->int conversion rounds to nearest
  * int / int is integer division; And/Or/Xor are bitwise on ints
  * Not has the lowest precedence (applies to the whole And/Or expression)
  * conditions are converted to int before testing
  * For..Next re-evaluates the To expression every iteration
  * locals are zero-initialised on function entry
"""
import os
import re
import sys

# ---------------------------------------------------------------- tokenizer

KEYWORDS = {
    'if', 'then', 'else', 'elseif', 'endif', 'end', 'for', 'to', 'step', 'next',
    'while', 'wend', 'repeat', 'until', 'forever', 'select', 'case', 'default',
    'function', 'return', 'global', 'local', 'dim', 'const', 'goto', 'gosub',
    'exit', 'and', 'or', 'not', 'xor', 'mod', 'shl', 'shr', 'sar', 'true',
    'false', 'null', 'include', 'data', 'read', 'restore', 'type', 'field',
    'each', 'new', 'delete', 'first', 'last', 'insert', 'before', 'after', 'pi',
}


class Tok:
    __slots__ = ('kind', 'val', 'suffix', 'line', 'file')

    def __init__(self, kind, val, line, file, suffix=''):
        self.kind = kind      # 'id','kw','int','float','str','op','nl','eof','label'
        self.val = val
        self.suffix = suffix
        self.line = line
        self.file = file

    def __repr__(self):
        return '%s:%r%s@%s:%d' % (self.kind, self.val, self.suffix, self.file, self.line)


class BBError(Exception):
    pass


def tokenize_file(path, seen):
    toks = []
    base = os.path.dirname(path)
    with open(path, 'rb') as f:
        text = f.read().decode('latin-1')
    text = text.replace('\r\n', '\n').replace('\r', '\n')
    fname = os.path.basename(path)
    for lineno, line in enumerate(text.split('\n'), 1):
        line_toks = tokenize_line(line, lineno, fname)
        # handle Include "file"
        if len(line_toks) >= 2 and line_toks[0].kind == 'kw' and line_toks[0].val == 'include':
            inc = line_toks[1].val
            incpath = find_ci(base, inc)
            if incpath is None:
                raise BBError('%s:%d include not found: %s' % (fname, lineno, inc))
            key = os.path.realpath(incpath).lower()
            if key not in seen:
                seen.add(key)
                toks.extend(tokenize_file(incpath, seen))
            continue
        toks.extend(line_toks)
        toks.append(Tok('nl', '\n', lineno, fname))
    return toks


def find_ci(base, rel):
    rel = rel.replace('\\', '/')
    p = os.path.join(base, rel)
    if os.path.exists(p):
        return p
    cur = base
    for part in rel.split('/'):
        found = None
        for e in os.listdir(cur):
            if e.lower() == part.lower():
                found = e
                break
        if found is None:
            return None
        cur = os.path.join(cur, found)
    return cur


ID_RE = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
NUM_RE = re.compile(r'(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?')


def tokenize_line(line, lineno, fname):
    toks = []
    i = 0
    n = len(line)

    def add(kind, val, suffix=''):
        toks.append(Tok(kind, val, lineno, fname, suffix))

    while i < n:
        c = line[i]
        if c in ' \t\x0c\x1a':
            i += 1
            continue
        if c == ';':
            break
        if c == '"':
            j = line.find('"', i + 1)
            if j < 0:
                j = n
            add('str', line[i + 1:j])
            i = j + 1
            continue
        # label: '.' at start of statement followed by identifier
        if c == '.' and (not toks or (toks[-1].kind == 'op' and toks[-1].val == ':')) and i + 1 < n and (line[i + 1].isalpha() or line[i + 1] == '_'):
            m = ID_RE.match(line, i + 1)
            add('label', m.group(0).lower())
            i = m.end()
            continue
        if c.isdigit() or (c == '.' and i + 1 < n and line[i + 1].isdigit()):
            m = NUM_RE.match(line, i)
            s = m.group(0)
            if '.' in s or 'e' in s.lower():
                add('float', float(s))
            else:
                add('int', int(s))
            i = m.end()
            continue
        if c == '$' and i + 1 < n and line[i + 1] in '0123456789abcdefABCDEF':
            m = re.compile(r'[0-9A-Fa-f]+').match(line, i + 1)
            v = int(m.group(0), 16) & 0xffffffff
            if v >= 0x80000000:
                v -= 0x100000000
            add('int', v)
            i = m.end()
            continue
        if c == '%' and i + 1 < n and line[i + 1] in '01' and (not toks or toks[-1].kind in ('op', 'kw')):
            m = re.compile(r'[01]+').match(line, i + 1)
            add('int', int(m.group(0), 2))
            i = m.end()
            continue
        if c.isalpha() or c == '_':
            m = ID_RE.match(line, i)
            word = m.group(0)
            i = m.end()
            suffix = ''
            if i < n and line[i] in '#$%':
                suffix = line[i]
                i += 1
            lw = word.lower()
            if lw in KEYWORDS and not suffix:
                add('kw', lw)
            else:
                add('id', lw, suffix)
            continue
        two = line[i:i + 2]
        if two in ('<>', '<=', '>=', '=<', '=>'):
            v = {'=<': '<=', '=>': '>='}.get(two, two)
            add('op', v)
            i += 2
            continue
        if c in '=<>+-*/^(),:\\~#[].':
            add('op', c)
            i += 1
            continue
        raise BBError('%s:%d bad char %r' % (fname, lineno, c))
    # merge compound keywords
    out = []
    for t in toks:
        if out and t.kind == 'kw' and out[-1].kind == 'kw':
            p = out[-1].val
            if p == 'end' and t.val in ('if', 'function', 'select', 'type'):
                out[-1].val = 'end' + t.val
                continue
            if p == 'else' and t.val == 'if':
                out[-1].val = 'elseif'
                continue
        out.append(t)
    for t in out:
        if t.kind == 'kw' and t.val == 'endif':
            t.val = 'endif'
    return out


# ---------------------------------------------------------------- AST

class Node:
    def __init__(self, kind, **kw):
        self.kind = kind
        self.__dict__.update(kw)

    def __repr__(self):
        return 'Node(%s %s)' % (self.kind, {k: v for k, v in self.__dict__.items() if k != 'kind'})


# ---------------------------------------------------------------- parser

STOP_KW = {'else', 'elseif', 'endif', 'wend', 'next', 'until', 'forever', 'case',
           'default', 'endselect', 'endfunction', 'function'}


class Parser:
    def __init__(self, toks):
        self.t = toks + [Tok('eof', None, 0, '?')]
        self.p = 0

    @property
    def cur(self):
        return self.t[self.p]

    def peek(self, k=1):
        return self.t[min(self.p + k, len(self.t) - 1)]

    def nxt(self):
        t = self.t[self.p]
        self.p += 1
        return t

    def err(self, msg):
        c = self.cur
        raise BBError('%s:%d: %s (got %r)' % (c.file, c.line, msg, c))

    def is_op(self, v, t=None):
        t = t or self.cur
        return t.kind == 'op' and t.val == v

    def is_kw(self, v, t=None):
        t = t or self.cur
        return t.kind == 'kw' and t.val == v

    def expect_op(self, v):
        if not self.is_op(v):
            self.err("expected '%s'" % v)
        return self.nxt()

    def expect_kw(self, v):
        if not self.is_kw(v):
            self.err("expected '%s'" % v)
        return self.nxt()

    def is_term(self):
        c = self.cur
        return c.kind in ('nl', 'eof') or (c.kind == 'op' and c.val == ':')

    # program -------------------------------------------------------
    def parse_program(self):
        main = []
        funcs = []
        while True:
            stmts = self.parse_seq('block')
            main.extend(stmts)
            c = self.cur
            if c.kind == 'eof':
                break
            if self.is_kw('function'):
                funcs.append(self.parse_function())
                continue
            self.err('unexpected token at top level')
        return main, funcs

    def parse_function(self):
        ft = self.expect_kw('function')
        nt = self.nxt()
        if nt.kind != 'id':
            self.err('function name expected')
        params = []
        self.expect_op('(')
        if not self.is_op(')'):
            while True:
                pt = self.nxt()
                if pt.kind != 'id':
                    self.err('param name expected')
                ptype = suffix_type(pt.suffix)
                if self.is_op('.'):
                    self.err('typed params not supported')
                default = None
                if self.is_op('='):
                    self.nxt()
                    default = self.parse_expr()
                params.append((pt.val, ptype, default))
                if self.is_op(','):
                    self.nxt()
                    continue
                break
        self.expect_op(')')
        body = self.parse_seq('block')
        self.expect_kw('endfunction')
        return Node('function', name=nt.val, rtype=suffix_type(nt.suffix), params=params,
                    body=body, line=ft.line, file=ft.file)

    # statement sequences -------------------------------------------
    def parse_seq(self, mode):
        stmts = []
        while True:
            while self.is_op(':') or (mode == 'block' and self.cur.kind == 'nl'):
                self.nxt()
            c = self.cur
            if c.kind == 'eof':
                return stmts
            if mode == 'line' and c.kind == 'nl':
                return stmts
            if c.kind == 'kw' and c.val in STOP_KW:
                return stmts
            st = self.parse_stmt()
            if st is not None:
                if isinstance(st, list):
                    stmts.extend(st)
                else:
                    stmts.append(st)
            # Blitz does not require ':' between statements on one line

    def parse_stmt(self):
        c = self.cur
        line = c.line
        if c.kind == 'label':
            self.nxt()
            return Node('label', name=c.val, line=line)
        if c.kind == 'kw':
            k = c.val
            self.nxt()
            if k == 'if':
                return self.parse_if()
            if k == 'while':
                cond = self.parse_expr()
                body = self.parse_seq('block')
                self.expect_kw('wend')
                return Node('while', cond=cond, body=body, line=line)
            if k == 'repeat':
                body = self.parse_seq('block')
                if self.is_kw('until'):
                    self.nxt()
                    cond = self.parse_expr()
                    return Node('repeat', cond=cond, body=body, line=line)
                self.expect_kw('forever')
                return Node('repeat', cond=None, body=body, line=line)
            if k == 'for':
                vt = self.nxt()
                if vt.kind != 'id':
                    self.err('for variable expected')
                var = Node('var', name=vt.val, suffix=vt.suffix, line=line)
                self.expect_op('=')
                if self.is_kw('each'):
                    self.err('For Each not supported')
                frm = self.parse_expr()
                self.expect_kw('to')
                to = self.parse_expr()
                step = None
                if self.is_kw('step'):
                    self.nxt()
                    step = self.parse_expr()
                body = self.parse_seq('block')
                self.expect_kw('next')
                if self.cur.kind == 'id':
                    self.nxt()
                return Node('for', var=var, frm=frm, to=to, step=step, body=body, line=line)
            if k == 'select':
                e = self.parse_expr()
                cases = []
                default = None
                while True:
                    while self.is_op(':') or self.cur.kind == 'nl':
                        self.nxt()
                    if self.is_kw('case'):
                        self.nxt()
                        vals = [self.parse_expr()]
                        while self.is_op(','):
                            self.nxt()
                            vals.append(self.parse_expr())
                        body = self.parse_seq('block')
                        cases.append((vals, body))
                        continue
                    if self.is_kw('default'):
                        self.nxt()
                        default = self.parse_seq('block')
                        continue
                    if self.is_kw('endselect'):
                        self.nxt()
                        break
                    self.err('Case/Default/End Select expected')
                return Node('select', expr=e, cases=cases, default=default, line=line)
            if k == 'goto':
                t = self.nxt()
                return Node('goto', name=t.val.lower(), line=line)
            if k == 'gosub':
                self.err('Gosub not supported')
            if k == 'exit':
                return Node('exit', line=line)
            if k == 'return':
                e = None
                if not self.is_term() and not (self.cur.kind == 'kw' and self.cur.val in STOP_KW):
                    e = self.parse_expr()
                return Node('return', expr=e, line=line)
            if k == 'end':
                return Node('end', line=line)
            if k in ('global', 'local', 'const'):
                decls = []
                while True:
                    t = self.nxt()
                    if t.kind != 'id':
                        self.err('identifier expected')
                    init = None
                    if self.is_op('='):
                        self.nxt()
                        init = self.parse_expr()
                    decls.append((t.val, suffix_type(t.suffix), init, t.suffix))
                    if self.is_op(','):
                        self.nxt()
                        continue
                    break
                return Node(k, decls=decls, line=line)
            if k == 'dim':
                arrs = []
                while True:
                    t = self.nxt()
                    if t.kind != 'id':
                        self.err('array name expected')
                    self.expect_op('(')
                    dims = [self.parse_expr()]
                    while self.is_op(','):
                        self.nxt()
                        dims.append(self.parse_expr())
                    self.expect_op(')')
                    arrs.append((t.val, suffix_type(t.suffix), dims, t.suffix))
                    if self.is_op(','):
                        self.nxt()
                        continue
                    break
                return Node('dim', arrs=arrs, line=line)
            self.p -= 1
            self.err('unsupported statement keyword')
        if c.kind == 'id':
            self.nxt()
            # assignment?
            if self.is_op('='):
                self.nxt()
                e = self.parse_expr()
                return Node('assign', target=Node('var', name=c.val, suffix=c.suffix, line=line), expr=e, line=line)
            if self.is_op('('):
                # could be array assignment, or call with parens
                save = self.p
                self.nxt()
                args = []
                if not self.is_op(')'):
                    args.append(self.parse_expr())
                    while self.is_op(','):
                        self.nxt()
                        args.append(self.parse_expr())
                if self.is_op(')'):
                    self.nxt()
                    if self.is_op('='):
                        self.nxt()
                        e = self.parse_expr()
                        return Node('assign', target=Node('index', name=c.val, suffix=c.suffix, args=args, line=line), expr=e, line=line)
                    if self.is_term() or (self.cur.kind == 'kw' and self.cur.val in STOP_KW):
                        return Node('callstmt', name=c.val, args=args, line=line)
                # not a clean paren-call: reparse as command with args
                self.p = save
            args = []
            if not (self.is_term() or (self.cur.kind == 'kw' and self.cur.val in STOP_KW)):
                args.append(self.parse_expr())
                while self.is_op(','):
                    self.nxt()
                    args.append(self.parse_expr())
            return Node('callstmt', name=c.val, args=args, line=line)
        self.err('statement expected')

    def parse_if(self):
        line = self.cur.line
        cond = self.parse_expr()
        if self.is_kw('then'):
            self.nxt()
        blk = self.is_term()
        mode = 'block' if blk else 'line'
        then = self.parse_seq(mode)
        els = None
        if self.is_kw('elseif'):
            self.nxt()
            sub = self.parse_if_inner(mode)
            els = [sub]
        elif self.is_kw('else'):
            self.nxt()
            els = self.parse_seq(mode)
        if blk:
            self.expect_kw('endif')
        else:
            if self.is_kw('endif'):
                self.nxt()
        return Node('if', cond=cond, then=then, els=els, line=line)

    def parse_if_inner(self, outer_mode):
        # ElseIf: same as If but the EndIf is shared with the outer if
        line = self.cur.line
        cond = self.parse_expr()
        if self.is_kw('then'):
            self.nxt()
        blk = self.is_term() if outer_mode == 'line' else True
        mode = 'block' if (outer_mode == 'block' or blk) else 'line'
        then = self.parse_seq(mode)
        els = None
        if self.is_kw('elseif'):
            self.nxt()
            els = [self.parse_if_inner(mode)]
        elif self.is_kw('else'):
            self.nxt()
            els = self.parse_seq(mode)
        return Node('if', cond=cond, then=then, els=els, line=line)

    # expressions -----------------------------------------------------
    def parse_expr(self):
        if self.is_kw('not'):
            self.nxt()
            e = self.parse_expr1()
            return Node('not', e=e)
        return self.parse_expr1()

    def parse_expr1(self):
        lhs = self.parse_expr2()
        while self.cur.kind == 'kw' and self.cur.val in ('and', 'or', 'xor'):
            op = self.nxt().val
            rhs = self.parse_expr2()
            lhs = Node('bin', op=op, a=lhs, b=rhs)
        return lhs

    def parse_expr2(self):
        lhs = self.parse_expr3()
        while self.cur.kind == 'op' and self.cur.val in ('=', '<>', '<', '>', '<=', '>='):
            op = self.nxt().val
            rhs = self.parse_expr3()
            lhs = Node('rel', op=op, a=lhs, b=rhs)
        return lhs

    def parse_expr3(self):
        lhs = self.parse_expr4()
        while self.cur.kind == 'op' and self.cur.val in ('+', '-'):
            op = self.nxt().val
            rhs = self.parse_expr4()
            lhs = Node('bin', op=op, a=lhs, b=rhs)
        return lhs

    def parse_expr4(self):
        lhs = self.parse_expr5()
        while self.cur.kind == 'kw' and self.cur.val in ('shl', 'shr', 'sar'):
            op = self.nxt().val
            rhs = self.parse_expr5()
            lhs = Node('bin', op=op, a=lhs, b=rhs)
        return lhs

    def parse_expr5(self):
        lhs = self.parse_expr6()
        while (self.cur.kind == 'op' and self.cur.val in ('*', '/')) or self.is_kw('mod'):
            op = self.nxt().val
            rhs = self.parse_expr6()
            lhs = Node('bin', op=op, a=lhs, b=rhs)
        return lhs

    def parse_expr6(self):
        lhs = self.parse_unary()
        while self.is_op('^'):
            self.nxt()
            rhs = self.parse_unary()
            lhs = Node('bin', op='^', a=lhs, b=rhs)
        return lhs

    def parse_unary(self):
        c = self.cur
        if self.is_op('-'):
            self.nxt()
            return Node('neg', e=self.parse_unary())
        if self.is_op('+'):
            self.nxt()
            return self.parse_unary()
        if self.is_op('~'):
            self.nxt()
            return Node('bitnot', e=self.parse_unary())
        if self.is_kw('not'):
            self.nxt()
            return Node('not', e=self.parse_expr2())
        return self.parse_primary()

    def parse_primary(self):
        c = self.nxt()
        if c.kind == 'int':
            return Node('int', v=c.val)
        if c.kind == 'float':
            return Node('float', v=c.val)
        if c.kind == 'str':
            return Node('str', v=c.val)
        if c.kind == 'kw':
            if c.val == 'true':
                return Node('int', v=1)
            if c.val in ('false', 'null'):
                return Node('int', v=0)
            if c.val == 'pi':
                return Node('float', v=3.14159265358979)
        if c.kind == 'op' and c.val == '(':
            e = self.parse_expr()
            self.expect_op(')')
            return e
        if c.kind == 'id':
            if self.is_op('('):
                self.nxt()
                args = []
                if not self.is_op(')'):
                    args.append(self.parse_expr())
                    while self.is_op(','):
                        self.nxt()
                        args.append(self.parse_expr())
                self.expect_op(')')
                return Node('call', name=c.val, suffix=c.suffix, args=args, line=c.line)
            return Node('var', name=c.val, suffix=c.suffix, line=c.line)
        self.p -= 1
        self.err('expression expected')


def suffix_type(s):
    return {'#': 'f', '$': 's', '%': 'i', '': 'i'}[s]


# ---------------------------------------------------------------- builtins
# name: (return type, [param types], [defaults or None], c++ name)
# param type 'n' = numeric generic (passed as float), 'a' = any (type-preserving)

def B(ret, params, defaults=None, cname=None):
    return (ret, params, defaults or [None] * len(params), cname)


BUILTINS = {
    # graphics
    'graphics': B('v', 'iiii', [None, None, 0, 0]),
    'setbuffer': B('v', 'i'),
    'backbuffer': B('i', ''),
    'frontbuffer': B('i', ''),
    'imagebuffer': B('i', 'ii', [None, 0]),
    'flip': B('v', 'i', [1]),
    'cls': B('v', ''),
    'clscolor': B('v', 'iii'),
    'color': B('v', 'iii'),
    'rect': B('v', 'iiiii', [None, None, None, None, 1]),
    'oval': B('v', 'iiiii', [None, None, None, None, 1]),
    'line': B('v', 'iiii'),
    'plot': B('v', 'ii'),
    'text': B('v', 'iisii', [None, None, None, 0, 0]),
    'loadimage': B('i', 's'),
    'createimage': B('i', 'iii', [None, None, 1]),
    'freeimage': B('v', 'i'),
    'drawimage': B('v', 'iiii', [None, None, None, 0]),
    'drawblock': B('v', 'iiii', [None, None, None, 0]),
    'tileimage': B('v', 'iiii', [None, 0, 0, 0]),
    'maskimage': B('v', 'iiii'),
    'midhandle': B('v', 'i'),
    'imagewidth': B('i', 'i'),
    'imageheight': B('i', 'i'),
    'imagerectcollide': B('i', 'iiiiiiii'),
    'imagesoverlap': B('i', 'iiiiii'),
    'imagescollide': B('i', 'iiiiiiii'),
    'rectsoverlap': B('i', 'iiiiiiii'),
    'copyrect': B('v', 'iiiiiiii', [None, None, None, None, None, None, 0, 0]),
    'savebuffer': B('i', 'is'),
    'graphicswidth': B('i', ''),
    'graphicsheight': B('i', ''),
    'availvidmem': B('i', ''),
    'totalvidmem': B('i', ''),
    'apptitle': B('v', 'ss', [None, '']),
    # sound
    'loadsound': B('i', 's'),
    'freesound': B('v', 'i'),
    'playsound': B('i', 'i'),
    'loopsound': B('v', 'i'),
    'stopchannel': B('v', 'i'),
    'channelplaying': B('i', 'i'),
    'soundvolume': B('v', 'if'),
    'channelvolume': B('v', 'if'),
    'pausechannel': B('v', 'i'),
    'resumechannel': B('v', 'i'),
    # input
    'keydown': B('i', 'i'),
    'keyhit': B('i', 'i'),
    'getkey': B('i', ''),
    'waitkey': B('i', ''),
    'flushkeys': B('v', ''),
    'flushjoy': B('v', ''),
    'flushmouse': B('v', ''),
    'mousex': B('i', ''),
    'mousey': B('i', ''),
    'mousedown': B('i', 'i'),
    'mousehit': B('i', 'i'),
    'joyx': B('f', 'i', [0]),
    'joyy': B('f', 'i', [0]),
    'joyxdir': B('i', 'i', [0]),
    'joyydir': B('i', 'i', [0]),
    'joyhit': B('i', 'ii', [None, 0]),
    'joydown': B('i', 'ii', [None, 0]),
    'joytype': B('i', 'i', [0]),
    'joygetkey': B('i', 'i', [0]),
    # files
    'readfile': B('i', 's'),
    'writefile': B('i', 's'),
    'openfile': B('i', 's'),
    'closefile': B('v', 'i'),
    'readint': B('i', 'i'),
    'writeint': B('v', 'ii'),
    'readfloat': B('f', 'i'),
    'writefloat': B('v', 'if'),
    'readstring': B('s', 'i'),
    'writestring': B('v', 'is'),
    'readline': B('s', 'i'),
    'writeline': B('v', 'is'),
    'readbyte': B('i', 'i'),
    'writebyte': B('v', 'ii'),
    'readshort': B('i', 'i'),
    'writeshort': B('v', 'ii'),
    'eof': B('i', 'i'),
    'filetype': B('i', 's'),
    'filesize': B('i', 's'),
    'readdir': B('i', 's'),
    'nextfile': B('s', 'i'),
    'closedir': B('v', 'i'),
    'deletefile': B('v', 's'),
    'copyfile': B('v', 'ss'),
    'createdir': B('v', 's'),
    'currentdir': B('s', ''),
    # time
    'millisecs': B('i', ''),
    'delay': B('v', 'i'),
    'createtimer': B('i', 'i'),
    'waittimer': B('i', 'i'),
    'freetimer': B('v', 'i'),
    'currentdate': B('s', ''),
    'currenttime': B('s', ''),
    # math
    'rand': B('i', 'ii', [None, None]),
    'rnd': B('f', 'ff', [None, None]),
    'seedrnd': B('v', 'i'),
    'int': B('i', 'a'),
    'float': B('f', 'a'),
    'str': B('s', 'a'),
    'abs': B('a', 'a'),
    'sgn': B('a', 'a'),
    'sqr': B('f', 'f'),
    'sin': B('f', 'f'),
    'cos': B('f', 'f'),
    'tan': B('f', 'f'),
    'asin': B('f', 'f'),
    'acos': B('f', 'f'),
    'atan': B('f', 'f'),
    'atan2': B('f', 'ff'),
    'floor': B('f', 'f'),
    'ceil': B('f', 'f'),
    'exp': B('f', 'f'),
    'log': B('f', 'f'),
    'log10': B('f', 'f'),
    # strings
    'len': B('i', 's'),
    'mid': B('s', 'sii', [None, None, -1]),
    'left': B('s', 'si'),
    'right': B('s', 'si'),
    'lower': B('s', 's'),
    'upper': B('s', 's'),
    'trim': B('s', 's'),
    'chr': B('s', 'i'),
    'asc': B('i', 's'),
    'instr': B('i', 'ssi', [None, None, 1]),
    'replace': B('s', 'sss'),
    'lset': B('s', 'si'),
    'rset': B('s', 'si'),
    'hex': B('s', 'i'),
    'bin': B('s', 'i'),
    'string': B('s', 'si'),
    # misc
    'runtimeerror': B('v', 's'),
    'debuglog': B('v', 's'),
    'commandline': B('s', ''),
    'print': B('v', 's', ['']),
    'write': B('v', 's'),
    'stop': B('v', ''),
    'hidepointer': B('v', ''),
    'showpointer': B('v', ''),
    'setgfxdriver': B('v', 'i'),
    'gfxmodeexists': B('i', 'iii'),
    'vwait': B('v', 'i', [1]),
}

CTYPE = {'i': 'int', 'f': 'float', 's': 'BStr'}
ZERO = {'i': '0', 'f': '0.0f', 's': 'BStr()'}


# ---------------------------------------------------------------- semantic + codegen

class Scope:
    def __init__(self, kind, name=None):
        self.kind = kind            # 'main' or 'func'
        self.name = name
        self.vars = {}              # name -> (type, cname)
        self.temps = []             # (ctype, cname)
        self.labels = set()


class Compiler:
    def __init__(self, main, funcs):
        self.main = main
        self.funcs = {f.name: f for f in funcs}
        if len(self.funcs) != len(funcs):
            seen = set()
            for f in funcs:
                if f.name in seen:
                    raise BBError('duplicate function ' + f.name)
                seen.add(f.name)
        self.globals = {}     # name -> type
        self.consts = {}      # name -> (type, node)
        self.arrays = {}      # name -> (type, ndims)
        self.tmpn = 0
        self.warnings = []
        self.collect_decls(main)
        for f in funcs:
            self.collect_arrays(f.body)

    def warn(self, msg):
        self.warnings.append(msg)

    def collect_decls(self, stmts):
        for s in walk_stmts(stmts):
            if s.kind == 'global':
                for name, t, init, suf in s.decls:
                    if name in self.globals and self.globals[name] != t:
                        if suf:
                            raise BBError('global type mismatch %s' % name)
                        continue
                    self.globals.setdefault(name, t)
            elif s.kind == 'const':
                for name, t, init, suf in s.decls:
                    self.consts[name] = (t, init)
            elif s.kind == 'dim':
                for name, t, dims, suf in s.arrs:
                    if name in self.arrays:
                        ot, od = self.arrays[name]
                        if od != len(dims) or (suf and ot != t):
                            raise BBError('array redim mismatch %s' % name)
                    else:
                        self.arrays[name] = (t, len(dims))

    def collect_arrays(self, stmts):
        for s in walk_stmts(stmts):
            if s.kind == 'dim':
                for name, t, dims, suf in s.arrs:
                    if name not in self.arrays:
                        self.arrays[name] = (t, len(dims))
            if s.kind == 'global':
                raise BBError('Global inside function not supported')

    # --------------------------------------------------------- variable lookup
    def lookup(self, scope, name, suffix, line, declare=True):
        if name in scope.vars:
            t, cn = scope.vars[name]
            if suffix and suffix_type(suffix) != t:
                raise BBError('line %s: type mismatch for %s' % (line, name))
            return t, cn
        if name in self.consts:
            t = self.consts[name][0]
            return t, 'c_' + name
        if name in self.globals:
            t = self.globals[name]
            if suffix and suffix_type(suffix) != t:
                raise BBError('line %s: global type mismatch for %s' % (line, name))
            return t, 'g_' + name
        t = suffix_type(suffix)
        cn = ('m_' if scope.kind == 'main' else 'l_') + name
        scope.vars[name] = (t, cn)
        return t, cn

    def new_temp(self, scope, t):
        self.tmpn += 1
        cn = '_t%d' % self.tmpn
        scope.temps.append((CTYPE[t], cn))
        return cn

    # --------------------------------------------------------- expressions
    def cast(self, code, frm, to):
        if frm == to or to == 'a':
            return code
        if to == 'i':
            if frm == 'f':
                return 'bb_f2i(%s)' % code
            if frm == 's':
                return 'bb_s2i(%s)' % code
        if to == 'f':
            if frm == 'i':
                return '((float)(%s))' % code
            if frm == 's':
                return 'bb_s2f(%s)' % code
        if to == 's':
            if frm == 'i':
                return 'bb_istr(%s)' % code
            if frm == 'f':
                return 'bb_fstr(%s)' % code
        if frm == 'v':
            raise BBError('void value used in expression')
        raise BBError('bad cast %s->%s' % (frm, to))

    def expr(self, scope, e):
        k = e.kind
        if k == 'int':
            v = e.v
            if v == -2147483648:
                return '(-2147483647-1)', 'i'
            return str(v), 'i'
        if k == 'float':
            r = repr(float(e.v))
            if 'e' not in r and '.' not in r:
                r += '.0'
            return r + 'f', 'f'
        if k == 'str':
            return 'BStr(%s)' % c_string(e.v), 's'
        if k == 'var':
            if e.name in self.arrays and e.name not in scope.vars and e.name not in self.globals:
                raise BBError('line %s: array used without index: %s' % (e.line, e.name))
            t, cn = self.lookup(scope, e.name, e.suffix, e.line)
            return cn, t
        if k == 'neg':
            c, t = self.expr(scope, e.e)
            if t == 's':
                raise BBError('negating string')
            return '(-%s)' % c, t
        if k == 'bitnot':
            c, t = self.expr(scope, e.e)
            return '(~%s)' % self.cast(c, t, 'i'), 'i'
        if k == 'not':
            c, t = self.expr(scope, e.e)
            if t == 'i':
                return '((%s)==0)' % c, 'i'
            if t == 'f':
                return '((%s)==0.0f)' % c, 'i'
            return '((%s)==BStr("0"))' % c, 'i'
        if k == 'rel':
            a, ta = self.expr(scope, e.a)
            b, tb = self.expr(scope, e.b)
            if ta == 's' or tb == 's':
                t = 's'
            elif ta == 'f' or tb == 'f':
                t = 'f'
            else:
                t = 'i'
            op = {'=': '==', '<>': '!='}.get(e.op, e.op)
            return '(%s %s %s)' % (self.cast(a, ta, t), op, self.cast(b, tb, t)), 'i'
        if k == 'bin':
            op = e.op
            a, ta = self.expr(scope, e.a)
            b, tb = self.expr(scope, e.b)
            if op in ('and', 'or', 'xor'):
                cop = {'and': '&', 'or': '|', 'xor': '^'}[op]
                return '(%s %s %s)' % (self.cast(a, ta, 'i'), cop, self.cast(b, tb, 'i')), 'i'
            if op in ('shl', 'shr', 'sar'):
                a = self.cast(a, ta, 'i')
                b = self.cast(b, tb, 'i')
                if op == 'shl':
                    return '((int)((unsigned)(%s) << ((%s)&31)))' % (a, b), 'i'
                if op == 'shr':
                    return '((int)((unsigned)(%s) >> ((%s)&31)))' % (a, b), 'i'
                return '((%s) >> ((%s)&31))' % (a, b), 'i'
            if op == '+' and (ta == 's' or tb == 's'):
                return '(%s + %s)' % (self.cast(a, ta, 's'), self.cast(b, tb, 's')), 's'
            if ta == 's' or tb == 's':
                raise BBError('line ?: bad string op %s' % op)
            if op == '^':
                return 'bb_pow(%s,%s)' % (self.cast(a, ta, 'f'), self.cast(b, tb, 'f')), 'f'
            t = 'f' if (ta == 'f' or tb == 'f') else 'i'
            a = self.cast(a, ta, t)
            b = self.cast(b, tb, t)
            if op == '/':
                if t == 'i':
                    return 'bb_idiv(%s,%s)' % (a, b), 'i'
                return '(%s / %s)' % (a, b), 'f'
            if op == 'mod':
                if t == 'i':
                    return 'bb_imod(%s,%s)' % (a, b), 'i'
                return 'fmodf(%s,%s)' % (a, b), 'f'
            return '(%s %s %s)' % (a, op, b), t
        if k == 'call':
            return self.call(scope, e.name, e.args, e.line, in_expr=True)
        raise BBError('unknown expr ' + k)

    def call(self, scope, name, args, line, in_expr):
        # array access?
        if name in self.arrays and name not in self.funcs:
            t, nd = self.arrays[name]
            if len(args) != nd:
                raise BBError('line %s: wrong number of array indices for %s' % (line, name))
            idx = ','.join(self.cast(*self.expr(scope, a), 'i') for a in args)
            return 'a_%s(%s)' % (name, idx), t
        if name in self.funcs:
            f = self.funcs[name]
            if len(args) > len(f.params):
                raise BBError('line %s: too many args to %s' % (line, name))
            cargs = []
            for i, (pn, pt, pd) in enumerate(f.params):
                if i < len(args):
                    c, t = self.expr(scope, args[i])
                    cargs.append(self.cast(c, t, pt))
                else:
                    if pd is None:
                        raise BBError('line %s: missing arg %s to %s' % (line, pn, name))
                    c, t = self.const_expr(pd)
                    cargs.append(self.cast(c, t, pt))
            return 'f_%s(%s)' % (name, ', '.join(cargs)), f.rtype
        if name in BUILTINS:
            ret, ptypes, defaults, cname = BUILTINS[name]
            cname = cname or 'bb_' + name
            if len(args) > len(ptypes):
                raise BBError('line %s: too many args to %s' % (line, name))
            cargs = []
            argtypes = []
            for i, pt in enumerate(ptypes):
                if i < len(args):
                    c, t = self.expr(scope, args[i])
                else:
                    d = defaults[i]
                    if d is None:
                        if name in ('rand', 'rnd') and i == 1:
                            break
                        raise BBError('line %s: missing arg %d to %s' % (line, i + 1, name))
                    if isinstance(d, str):
                        c, t = 'BStr(%s)' % c_string(d), 's'
                    elif isinstance(d, float):
                        c, t = repr(d) + 'f', 'f'
                    else:
                        c, t = str(d), 'i'
                argtypes.append(t)
                if pt == 'a':
                    cargs.append(c)
                else:
                    cargs.append(self.cast(c, t, pt))
            if ret == 'a':
                ret = argtypes[0]
                if ret == 's':
                    ret = 'f'
                    cargs[0] = self.cast(cargs[0], 's', 'f')
            if name == 'int':
                return self.cast(cargs[0], argtypes[0], 'i'), 'i'
            if name == 'float':
                return self.cast(cargs[0], argtypes[0], 'f'), 'f'
            if name == 'str':
                return self.cast(cargs[0], argtypes[0], 's'), 's'
            return '%s(%s)' % (cname, ', '.join(cargs)), ret
        raise BBError('line %s: unknown function or array: %s' % (line, name))

    def const_expr(self, e):
        # used for default parameters - evaluate in an empty scope
        sc = Scope('func', '__const')
        return self.expr(sc, e)

    # --------------------------------------------------------- statements
    def stmts(self, scope, stmts, out, ind, loopdepth):
        for s in stmts:
            self.stmt(scope, s, out, ind, loopdepth)

    def cond(self, scope, e):
        c, t = self.expr(scope, e)
        return self.cast(c, t, 'i')

    def stmt(self, scope, s, out, ind, loopdepth):
        I = '  ' * ind
        k = s.kind
        if k == 'label':
            scope.labels.add(s.name)
            out.append('%sL_%s: ;' % (I, s.name))
        elif k == 'goto':
            out.append('%sgoto L_%s;' % (I, s.name))
            scope.gotos.add(s.name)
        elif k == 'assign':
            tg = s.target
            c, t = self.expr(scope, s.expr)
            if tg.kind == 'var':
                if tg.name in self.consts:
                    raise BBError('line %s: assign to const' % s.line)
                vt, cn = self.lookup(scope, tg.name, tg.suffix, s.line)
                out.append('%s%s = %s;' % (I, cn, self.cast(c, t, vt)))
            else:
                lc, lt = self.call(scope, tg.name, tg.args, s.line, in_expr=False)
                if tg.name not in self.arrays:
                    raise BBError('line %s: assignment to non-array %s' % (s.line, tg.name))
                out.append('%s%s = %s;' % (I, lc, self.cast(c, t, lt)))
        elif k == 'callstmt':
            if s.name not in self.funcs and s.name not in BUILTINS and s.name not in self.arrays:
                raise BBError('%s line %s: unknown command %s' % (scope.name, s.line, s.name))
            c, t = self.call(scope, s.name, s.args, s.line, in_expr=False)
            out.append('%s%s;' % (I, c if t != 'v' else c))
        elif k == 'if':
            out.append('%sif (%s) {' % (I, self.cond(scope, s.cond)))
            self.stmts(scope, s.then, out, ind + 1, loopdepth)
            if s.els:
                out.append('%s} else {' % I)
                self.stmts(scope, s.els, out, ind + 1, loopdepth)
            out.append('%s}' % I)
        elif k == 'while':
            out.append('%swhile (%s) {' % (I, self.cond(scope, s.cond)))
            self.stmts(scope, s.body, out, ind + 1, loopdepth + 1)
            out.append('%s}' % I)
        elif k == 'repeat':
            if s.cond is None:
                out.append('%sfor (;;) {' % I)
                self.stmts(scope, s.body, out, ind + 1, loopdepth + 1)
                out.append('%s}' % I)
            else:
                out.append('%sdo {' % I)
                self.stmts(scope, s.body, out, ind + 1, loopdepth + 1)
                out.append('%s} while (!(%s));' % (I, self.cond(scope, s.cond)))
        elif k == 'for':
            vt, cn = self.lookup(scope, s.var.name, s.var.suffix, s.line)
            fc, ft = self.expr(scope, s.frm)
            tc, tt = self.expr(scope, s.to)
            if s.step is not None:
                sc, st = self.expr(scope, s.step)
                sv = const_value(s.step)
                if sv is None:
                    raise BBError('line %s: Step must be constant' % s.line)
            else:
                sc, st, sv = '1', 'i', 1
            sc = self.cast(sc, st, vt)
            cmp = '<=' if sv >= 0 else '>='
            out.append('%sfor (%s = %s; %s %s %s; %s += %s) {' % (
                I, cn, self.cast(fc, ft, vt), cn, cmp, self.cast(tc, tt, vt), cn, sc))
            self.stmts(scope, s.body, out, ind + 1, loopdepth + 1)
            out.append('%s}' % I)
        elif k == 'select':
            c, t = self.expr(scope, s.expr)
            tmp = self.new_temp(scope, t)
            out.append('%s%s = %s;' % (I, tmp, c))
            first = True
            for vals, body in s.cases:
                conds = []
                for v in vals:
                    vc, vtt = self.expr(scope, v)
                    if t == 's' or vtt == 's':
                        ct = 's'
                    elif t == 'f' or vtt == 'f':
                        ct = 'f'
                    else:
                        ct = 'i'
                    conds.append('(%s == %s)' % (self.cast(tmp, t, ct), self.cast(vc, vtt, ct)))
                out.append('%s%sif (%s) {' % (I, '' if first else '} else ', ' || '.join(conds)))
                first = False
                self.stmts(scope, body, out, ind + 1, loopdepth)
            if s.default is not None:
                if first:
                    out.append('%s{' % I)
                else:
                    out.append('%s} else {' % I)
                first = False
                self.stmts(scope, s.default, out, ind + 1, loopdepth)
            if not first:
                out.append('%s}' % I)
        elif k == 'exit':
            if loopdepth == 0:
                raise BBError('line %s: Exit outside loop' % s.line)
            out.append('%sbreak;' % I)
        elif k == 'return':
            if scope.kind == 'main':
                out.append('%sreturn;' % I)
            else:
                rt = scope.rtype
                if s.expr is None:
                    out.append('%sreturn %s;' % (I, ZERO[rt]))
                else:
                    c, t = self.expr(scope, s.expr)
                    out.append('%sreturn %s;' % (I, self.cast(c, t, rt)))
        elif k == 'end':
            out.append('%sbb_end();' % I)
        elif k == 'global':
            for name, t, init, suf in s.decls:
                if init is not None:
                    c, it = self.expr(scope, init)
                    out.append('%sg_%s = %s;' % (I, name, self.cast(c, it, self.globals[name])))
        elif k == 'local':
            for name, t, init, suf in s.decls:
                cn = 'l_' + name if scope.kind == 'func' else 'm_' + name
                scope.vars[name] = (t, cn)
                if init is not None:
                    c, it = self.expr(scope, init)
                    out.append('%s%s = %s;' % (I, cn, self.cast(c, it, t)))
                else:
                    out.append('%s%s = %s;' % (I, cn, ZERO[t]))
        elif k == 'const':
            pass
        elif k == 'dim':
            for name, t, dims, suf in s.arrs:
                ds = ', '.join(self.cast(*self.expr(scope, d), 'i') for d in dims)
                out.append('%sa_%s.dim(%s);' % (I, name, ds))
        else:
            raise BBError('unhandled stmt ' + k)

    # --------------------------------------------------------- whole program
    def compile(self):
        out_funcs = []
        protos = []
        # function prototypes
        for name, f in self.funcs.items():
            ps = ', '.join('%s l_%s' % (CTYPE[pt], pn) for pn, pt, pd in f.params)
            protos.append('static %s f_%s(%s);' % (CTYPE[f.rtype], name, ps))
        # main
        ms = Scope('main', '<main>')
        ms.rtype = 'i'
        ms.gotos = set()
        body = []
        self.stmts(ms, self.main, body, 1, 0)
        self.check_labels(ms)
        # functions
        for name, f in self.funcs.items():
            fs = Scope('func', name)
            fs.rtype = f.rtype
            fs.gotos = set()
            for pn, pt, pd in f.params:
                fs.vars[pn] = (pt, 'l_' + pn)
            fbody = []
            self.stmts(fs, f.body, fbody, 1, 0)
            self.check_labels(fs)
            ps = ', '.join('%s l_%s' % (CTYPE[pt], pn) for pn, pt, pd in f.params)
            lines = ['static %s f_%s(%s) {' % (CTYPE[f.rtype], name, ps)]
            pnames = {pn for pn, pt, pd in f.params}
            for vn, (vt, cn) in sorted(fs.vars.items()):
                if vn not in pnames:
                    lines.append('  %s %s = %s;' % (CTYPE[vt], cn, ZERO[vt]))
            for ct, cn in fs.temps:
                lines.append('  %s %s;' % (ct, cn))
            lines.append('  BB_FUNC_ENTER("%s");' % name)
            lines.extend(fbody)
            lines.append('  return %s;' % ZERO[f.rtype])
            lines.append('}')
            out_funcs.append('\n'.join(lines))

        o = []
        o.append('// Generated by bb2cpp - do not edit')
        o.append('#include "bbrt.h"')
        o.append('')
        o.append('// ---- constants')
        for name, (t, init) in self.consts.items():
            c, it = self.const_expr(init)
            o.append('static const %s c_%s = %s;' % (CTYPE[t], name, self.cast(c, it, t)))
        o.append('// ---- globals')
        for name, t in sorted(self.globals.items()):
            o.append('%s g_%s = %s;' % (CTYPE[t], name, ZERO[t]))
        o.append('// ---- arrays')
        for name, (t, nd) in sorted(self.arrays.items()):
            o.append('BArray<%s> a_%s("%s");' % (CTYPE[t], name, name))
        o.append('// ---- main-program locals')
        for vn, (vt, cn) in sorted(ms.vars.items()):
            o.append('static %s %s = %s;' % (CTYPE[vt], cn, ZERO[vt]))
        for ct, cn in ms.temps:
            o.append('static %s %s;' % (ct, cn))
        o.append('')
        o.extend(protos)
        o.append('')
        o.append('void bb_main() {')
        o.extend(body)
        o.append('}')
        o.append('')
        o.extend(out_funcs)
        o.append('')
        return '\n'.join(o) + '\n'

    def check_labels(self, scope):
        missing = scope.gotos - scope.labels
        if missing:
            raise BBError('%s: goto to unknown labels %s' % (scope.name, missing))


def const_value(e):
    if e.kind == 'int':
        return e.v
    if e.kind == 'float':
        return e.v
    if e.kind == 'neg':
        v = const_value(e.e)
        return None if v is None else -v
    return None


def walk_stmts(stmts):
    for s in stmts:
        yield s
        for attr in ('then', 'els', 'body', 'default'):
            sub = getattr(s, attr, None)
            if sub:
                yield from walk_stmts(sub)
        if s.kind == 'select':
            for vals, body in s.cases:
                yield from walk_stmts(body)


def c_string(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif o < 32 or o > 126:
            out.append('\\x%02x""' % o)
        else:
            out.append(ch)
    return '"' + ''.join(out) + '"'


def main():
    src, dst = sys.argv[1], sys.argv[2]
    try:
        toks = tokenize_file(src, {os.path.realpath(src).lower()})
        p = Parser(toks)
        mainst, funcs = p.parse_program()
        comp = Compiler(mainst, funcs)
        code = comp.compile()
    except BBError as e:
        print('bb2cpp error:', e, file=sys.stderr)
        sys.exit(1)
    with open(dst, 'w') as f:
        f.write(code)
    for w in comp.warnings:
        print('warning:', w, file=sys.stderr)
    print('bb2cpp: %d functions, %d globals, %d arrays -> %s' % (
        len(funcs), len(comp.globals), len(comp.arrays), dst))


if __name__ == '__main__':
    main()
