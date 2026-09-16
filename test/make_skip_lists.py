#!/usr/bin/env python3
"""Turn the failures of a corpus run under the legacy parser into the skip list of a test config.

    make_skip_lists.py <unittest log> <config.json> [--write]

Each failing test file gets one reason, derived from its first failure. Without --write the
grouped list is printed; with --write the config's skip_tests is replaced.
"""
import json
import re
import sys
from collections import OrderedDict

BLOCK = re.compile(
    r'\n\d+\. (test/sql/[^\n:]+):(\d+)\n=+\n(.*?)\n=+\n(.*?)\n=+\n(.*?)\n-{20,}\n', re.S)
FAIL_LINE = re.compile(r'^(test/sql/[A-Za-z0-9_/.-]+\.test(?:_slow)?):\d+', re.M)


def classify(sql, kind, err, strict, path_hint=''):
    s = ' '.join(sql.split())
    up = s.upper()
    e = err.strip().split('\n')[0]
    rules = [
        ('CREATE TRIGGER', 'CREATE TRIGGER (2.0 syntax)'),
        ('EXTERNAL RESOURCE', 'EXTERNAL RESOURCE statements (2.0 syntax)'),
        ('EXTENSION REPOSITORY', 'CREATE EXTENSION REPOSITORY (2.0 syntax)'),
        ('INSTALL AND LOAD', 'INSTALL AND LOAD (2.0 syntax)'),
        ('NEAREST', 'NEAREST joins (2.0 syntax)'),
        ('JOIN BY', 'JOIN BY (2.0 syntax)'),
        ('TEMPORARY INDEX', 'CREATE TEMPORARY INDEX (2.0 syntax)'),
        ('TEMPORARY SCHEMA', 'CREATE TEMPORARY SCHEMA (2.0 syntax)'),
        ('TEMPORARY TRIGGER', 'CREATE TRIGGER (2.0 syntax)'),
        ('OVERLAY(', 'OVERLAY (2.0 syntax)'),
        ('SECURE VIEW', 'CREATE SECURE VIEW (2.0)'),
        ('ADD UNIQUE', 'ALTER TABLE ADD UNIQUE (2.0 capability)'),
        ('ADD CONSTRAINT', 'ALTER TABLE ADD CONSTRAINT (2.0 capability)'),
        ('SET LOCATION', 'ALTER TABLE SET LOCATION (2.0 syntax)'),
    ]
    for needle, reason in rules:
        if needle in up:
            return reason
    if re.search(r'\bSCHEMA\s+\w+\.\w+\.\w+', up) or re.search(r'\b\w+\.\w+\.\w+\.\w+\b', s):
        return 'nested schemas and four-part names (2.0 capability)'
    if 'ADD COLUMN' in up and re.search(r'\b(NOT NULL|UNIQUE|PRIMARY KEY|CHECK|REFERENCES)\b', up):
        return '1.5 behaviour: ADD COLUMN with constraints is rejected'
    if re.search(r"\s(~|!~|~\*|!~\*)\s", s):
        return '1.5 behaviour: ~ is a full regex match'
    if 'main.' in e or 'main.' in s and 'struct_pack' in e:
        return '1.5 rendering: built-in functions are qualified with main.'
    if 'ORDER BY TRUE' in up or 'ORDER BY NULL' in up:
        return '1.5 behaviour: ORDER BY of a non-integer literal is accepted'
    if 'PRESERVE_IDENTIFIER_CASE' in up or 'UPPERCASE' in up:
        return '2.0 uppercase identifier mode is treated as preserve'
    if 'WINDOW' in up and ' AS (' in up and re.search(r'WINDOW\s+\w+\s+AS\s*\(\s*\w+\s*\)', up):
        return '1.5 grammar supports window clause chaining; 2.0 rejects it'
    if 'T_List' in e:
        return '1.5 behaviour: PARTITION BY in a bare COPY option list is not supported'
    if 'Expected a constant as type modifier' in e:
        return '1.5 behaviour: type modifiers must be constants'
    if 'OPERATOR(' in up:
        return '1.5 behaviour: OPERATOR(schema.op) resolves the schema as a function'
    if 'Referenced table' in e and re.search(r'\)\s+\w+\s+(FULL|LEFT|RIGHT|INNER|OUTER|JOIN)', up):
        return '1.5 behaviour: no alias on a parenthesized join'
    if up.startswith('SHOW ') and 'Table with name' in e:
        return '1.5 behaviour: SHOW name describes a table only'
    if 'VIEW options' in e:
        return 'CREATE VIEW WITH options (2.0 capability)'
    if 'SWITCH' in e.upper() or 'SWITCH(' in up:
        return 'SWITCH with non-constant keys (2.0 capability)'
    if 'CTE body must be' in e and 'COPY' in up:
        return 'COPY inside a CTE (2.0 capability)'
    if 'cross-database' in e:
        return 'nested schemas and four-part names (2.0 capability)'
    if 'preserve_identifier_case' in path_hint or 'MySchema' in e:
        return '2.0 uppercase identifier mode is treated as preserve'
    if kind.startswith('Query failed, but error message did not match'):
        return 'error wording differs'
    if strict:
        m = re.search(r'syntax error at or near "([^"]*)"', e)
        if m:
            return 'syntax only the PEG grammar has (at or near "%s")' % m.group(1)
        return 'syntax only the PEG grammar has'
    if kind.startswith('Wrong result') and 'EXPLAIN' in up:
        return 'plan shape differs, results do not'
    return 'to review: ' + e[:80]


def main():
    log_path, config_path = sys.argv[1], sys.argv[2]
    write = '--write' in sys.argv
    strict = 'strict' in config_path
    log = open(log_path, errors='replace').read()
    failing = OrderedDict()
    for m in FAIL_LINE.finditer(log):
        failing.setdefault(m.group(1), None)
    for m in BLOCK.finditer(log):
        path, line, kind, sql, rest = m.groups()
        act = rest.split('Actual result:\n', 1)[1] if 'Actual result:\n' in rest else rest
        act = re.sub(r'^=+\n', '', act)
        if path in failing and failing[path] is None:
            failing[path] = (' '.join(kind.split()), sql, act)
    groups = OrderedDict()
    for path, info in failing.items():
        reason = classify(info[1], info[0], info[2], strict, path) if info else 'to review: no failure block in log'
        groups.setdefault(reason, []).append(path)
    skip = [{'reason': r, 'paths': sorted(p)} for r, p in sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0]))]
    print('%d failing files, %d reasons' % (len(failing), len(groups)))
    for entry in skip:
        print('  %3d  %s' % (len(entry['paths']), entry['reason']))
    if write:
        config = json.load(open(config_path), object_pairs_hook=OrderedDict)
        config['skip_tests'] = skip
        with open(config_path, 'w') as f:
            json.dump(config, f, indent=2)
            f.write('\n')
        print('wrote', config_path)


if __name__ == '__main__':
    main()
