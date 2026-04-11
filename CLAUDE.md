# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Tree-sitter grammar for the AMX NetLinx programming language. Generates parsers with bindings for Node.js, Rust, Python, C, Go, Swift, and Zig. NetLinx file extensions: `.axs`, `.axi`, `.axb`, `.lib`.

## Essential Commands

```bash
pnpm install              # Install dependencies (Node 22.15.0 via .nvmrc)
pnpm run generate         # Regenerate parser from grammar.js (tree-sitter generate --abi 14)
pnpm test                 # Run test corpus (tree-sitter test)
pnpm run test-node        # Run Node.js binding tests
pnpm run lint             # ESLint
pnpm start                # tree-sitter playground (requires wasm build via prestart)
```

After any change to `grammar.js` or its imported modules, run `pnpm run generate` then `pnpm test`.

## Architecture

**Grammar definition** — `grammar.js` is the source of truth. It imports supporting modules:
- `keywords.js` — keyword definitions
- `directives.js` — preprocessor directives
- `netlinx-nodes.js` — NetLinx-specific node definitions (events, sections, etc.)
- `netlinx.js` — system constants and built-in functions
- `g4api.js`, `snapi.js`, `unicodelib.js` — large library API definitions for highlighting

**External scanner** — `src/scanner.c` handles automatic semicolon insertion by analyzing whitespace, newlines, and comments to determine where semicolons are implicitly legal. This file is manually maintained (not generated).

**Generated files** (committed but never edit manually):
- `src/parser.c`, `src/grammar.json`, `src/node-types.json`

**Queries** (`queries/`):
- `highlights.scm` + library-specific highlight files (`highlights-g4api.scm`, `highlights-snapi.scm`, `highlights-unicodelib.scm`)
- `tags.scm`, `locals.scm`, `folds.scm`

**Tests** (`test/corpus/*.txt`) — Tree-sitter test format: test name, NetLinx code, expected S-expression tree separated by `---`.

**Examples** (`examples/`) — ~90 NetLinx files used for parse validation in CI (`tree-sitter parse` runs against all of them).

## Design Decisions

- **Permissive parsing**: The grammar intentionally accepts syntactically valid but semantically questionable code. Syntax correctness is the goal, not semantic validation.
- **Preprocessor in expressions**: Preprocessor directives inside expressions produce error nodes — this is a known limitation documented in the README.
- **Precedence**: The `PREC` object in `grammar.js` defines 20+ precedence levels. The conflicts array handles ~60 grammar ambiguities, primarily around preprocessor directives in various contexts.
- **Conventional commits**: Uses commitlint with `@commitlint/config-conventional`. Use `pnpm run commit` (git-cz) for interactive commit creation.
