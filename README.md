# legacy_parser

The SQL parser DuckDB used up to 1.5, packaged as an extension for DuckDB 2.x.

DuckDB 2.0 replaced the Postgres-derived parser with a PEG parser. This extension carries the
1.5 grammar (`third_party/libpg_query`) and its transformer to DuckDB's AST, frozen at the last
commit that held both (duckdb `84dc4405aac`, plus the three parser fixes that landed on the
v1.5 branch afterwards), and registers it through DuckDB's parser override hook.

Load it when SQL that 1.5 accepted no longer parses, or parses differently, and you cannot wait
for a patch release.

## Usage

```sql
LOAD legacy_parser;                  -- the 1.5 grammar is now the parser
SELECT ...;                          -- parsed by the 1.5 grammar
SET disable_legacy_parser = true;
SELECT ...;                          -- built-in PEG parser
SET disable_legacy_parser = false;
SELECT ...;                          -- the 1.5 grammar again
```

Loading selects `allow_parser_override_extension = 'strict'`: the legacy grammar is the only
parser and reports its own errors. Two other modes exist through that core setting:

- `fallback`: the legacy grammar is tried first, and the PEG parser handles what it rejects.
- `default`: the override is switched off.

`disable_legacy_parser` is the extension's own switch. It never changes the core setting; when
set, every query is handed to the built-in parser.

Two rules follow from how DuckDB resolves the parser:

- The parser is chosen once per query string. A `SET` takes effect from the next query string,
  not within the string that contains it.
- Both settings are database-wide, not per connection.

`legacy_parser_stats()` returns how many queries the legacy grammar parsed and how many it
declined.

## Known divergences from DuckDB 2.0

These are 1.5 behaviour, confirmed against a 1.5.5 build, and stay as they are:

- `~` and `!~` are full regex matches; 2.0 defaults to partial matches.
- `B'0101'` is the `VARCHAR` `'b0101'`; 2.0 has bit-string literals.
- Rendered SQL qualifies built-in functions, e.g. `main.struct_pack(x := 1)` in view definitions.
- `ORDER BY true` is accepted; 2.0 rejects a non-integer literal.
- `ALTER TABLE ... ADD COLUMN ... UNIQUE` and other column constraints on `ADD COLUMN` are
  rejected by the transformer.
- `identifier_case_mode = 'uppercase'` is a 2.0 option; the legacy grammar treats it as `preserve`.

Syntax added to DuckDB after the grammar was frozen is rejected in strict mode and handed to the
PEG parser in fallback mode. `CREATE TRIGGER`, temporary indexes and schemas, nested schemas,
external resources, extension repositories, `NEAREST` joins, `OVERLAY` and bare expression
statements are the largest groups.

## Testing

The extension's own tests live in `test/legacy`. The DuckDB test corpus can be run under the
legacy parser with the unittest binary of an extension build:

```sh
cd duckdb
../build/release/test/unittest --test-dir "$PWD" --test-config ../test/legacy_parser_strict_config.json "test/sql/*"
../build/release/test/unittest --test-dir "$PWD" --test-config ../test/legacy_parser_config.json "test/sql/*"
```

The strict configuration lists every test that needs syntax only the PEG grammar has; the
fallback configuration lists the tests where the 1.5 AST or semantics differ from what 2.0
expects.
