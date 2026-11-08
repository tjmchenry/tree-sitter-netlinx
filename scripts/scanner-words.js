#!/usr/bin/env node

const fs = require("node:fs");
const path = require("node:path");

const root = path.join(__dirname, "..");
const headerPath = path.join(root, "src", "scanner_words.h");

const WORD = /^[A-Za-z0-9_]+$/;

function literalWord(rule) {
    return rule &&
        (rule.type === "PATTERN" || rule.type === "STRING") &&
        WORD.test(rule.value)
        ? rule.value.toLowerCase()
        : null;
}

// The words `rule` can begin with, and whether it can match nothing.
function first(rules, rule, entered = new Set()) {
    switch (rule.type) {
        case "PATTERN":
        case "STRING": {
            const word = literalWord(rule);
            return { words: new Set(word ? [word] : []), empty: false };
        }
        case "BLANK":
            return { words: new Set(), empty: true };
        case "SYMBOL": {
            if (!(rule.name in rules) || entered.has(rule.name)) {
                return { words: new Set(), empty: false };
            }
            entered.add(rule.name);
            const found = first(rules, rules[rule.name], entered);
            entered.delete(rule.name);
            return found;
        }
        case "CHOICE": {
            const words = new Set();
            let empty = false;
            for (const member of rule.members) {
                const found = first(rules, member, entered);
                found.words.forEach((word) => words.add(word));
                empty ||= found.empty;
            }
            return { words, empty };
        }
        case "SEQ": {
            const words = new Set();
            for (const member of rule.members) {
                const found = first(rules, member, entered);
                found.words.forEach((word) => words.add(word));
                if (!found.empty) {
                    return { words, empty: false };
                }
            }
            return { words, empty: true };
        }
        case "REPEAT":
            return {
                words: first(rules, rule.content, entered).words,
                empty: true,
            };
        default:
            return first(rules, rule.content, entered);
    }
}

function sorted(words) {
    return [...words].sort();
}

// The statements whose keyword the compiler also reads as a value where an
// operand is owed: a channel operation, a send, a buffer or level, a call,
// BREAK and CONTINUE, and the waits that name none.
const VALUE_STATEMENT_RULES = [
    "devchan_operation_statement",
    "send_string_statement",
    "send_command_statement",
    "send_level_statement",
    "clear_buffer_statement",
    "create_buffer_statement",
    "create_multi_buffer_statement",
    "create_level_statement",
    "call_statement",
    "system_call_statement",
    "break_statement",
    "continue_statement",
    "cancel_all_wait_statement",
    "cancel_all_wait_until_statement",
    "pause_all_wait_statement",
    "restart_all_wait_statement",
];

// The scanner's word lists, each read from the grammar's own rules.
function scannerWords(grammar) {
    const rules = grammar.rules;
    const reserved = new Set();
    for (const rule of Object.values(rules)) {
        const word = literalWord(rule);
        if (word) {
            reserved.add(word);
        }
    }
    const firstWords = (name) => {
        if (!(name in rules)) {
            throw new Error(`the grammar has no rule ${name}`);
        }
        return first(rules, rules[name]).words;
    };
    const firstOf = (name) => sorted(firstWords(name));
    const keywordTypes = firstOf("intrinsic_type");
    const modifiers = firstOf("_declaration_modifiers");
    const values = new Set(
        VALUE_STATEMENT_RULES.flatMap((name) => [...firstWords(name)]),
    );
    const notStatements = new Set([
        ...keywordTypes,
        ...modifiers,
        ...values,
        ...firstWords("expression"),
    ]);
    const statements = [
        ...firstWords("source_file"),
        ...firstWords("else_clause"),
        ...firstWords("active_statement"),
    ].filter((word) => !notStatements.has(word));
    return {
        RESERVED_WORDS: sorted(reserved),
        KEYWORD_TYPES: keywordTypes,
        DECLARATION_MODIFIERS: modifiers,
        FIELD_START_WORDS: firstOf("field_declaration"),
        STATEMENT_WORDS: sorted(new Set(statements)),
        VALUE_STATEMENT_WORDS: sorted(values),
    };
}

const COMMENTS = {
    RESERVED_WORDS: "Every word a grammar rule spells: no name may be one.",
    KEYWORD_TYPES: "The words an intrinsic type spells.",
    DECLARATION_MODIFIERS: "The words a declaration modifier spells.",
    FIELD_START_WORDS: "The reserved words a field may begin with.",
    STATEMENT_WORDS:
        "The words a statement, section, event, arm or ELSE begins with, short of types, modifiers, values and value statements.",
    VALUE_STATEMENT_WORDS:
        "The words a statement begins with that the compiler also reads as a value.",
};

function scannerWordsHeader(grammar) {
    const lists = scannerWords(grammar);
    const lines = [
        "// Generated by scripts/scanner-words.js from src/grammar.json. Do not edit.",
        "// Each list is lowercase and sorted.",
        "",
        "#ifndef TREE_SITTER_NETLINX_SCANNER_WORDS_H_",
        "#define TREE_SITTER_NETLINX_SCANNER_WORDS_H_",
        "",
    ];
    for (const [name, words] of Object.entries(lists)) {
        lines.push(`// ${COMMENTS[name]}`);
        lines.push(`static const char *const ${name}[] = {`);
        for (const word of words) {
            lines.push(`    "${word}",`);
        }
        lines.push("};");
        lines.push("");
    }
    lines.push("#endif // TREE_SITTER_NETLINX_SCANNER_WORDS_H_");
    return `${lines.join("\n")}\n`;
}

function readGrammar() {
    return JSON.parse(
        fs.readFileSync(path.join(root, "src", "grammar.json"), "utf8"),
    );
}

module.exports = { scannerWords, scannerWordsHeader, readGrammar, headerPath };

if (require.main === module) {
    fs.writeFileSync(headerPath, scannerWordsHeader(readGrammar()));
}
