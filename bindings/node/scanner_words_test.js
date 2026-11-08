/// <reference types="node" />

const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const { test } = require("node:test");

const {
    scannerWords,
    scannerWordsHeader,
    scannerWordsBinding,
    readGrammar,
    headerPath,
    bindingPath,
} = require("../../scripts/scanner-words.js");

test("the scanner's word lists are the grammar's", () => {
    assert.strictEqual(
        fs.readFileSync(headerPath, "utf8"),
        scannerWordsHeader(readGrammar()),
        "src/scanner_words.h is stale: run `pnpm generate`",
    );
    assert.strictEqual(
        fs.readFileSync(bindingPath, "utf8"),
        scannerWordsBinding(readGrammar()),
        "bindings/rust/scanner_words.rs is stale: run `pnpm generate`",
    );
});

test("each of the scanner's word lists is not empty and fits its buffer, a keyword list reserved and the system types not", () => {
    const { SYSTEM_TYPES, ...keywords } = scannerWords(readGrammar());
    const reserved = new Set(keywords.RESERVED_WORDS);
    const scanner = fs.readFileSync(path.join(__dirname, "..", "..", "src", "scanner.c"), "utf8");
    const wordSize = Number(/#define WORD_SIZE (\d+)/.exec(scanner)[1]);
    for (const [name, words] of Object.entries({ ...keywords, SYSTEM_TYPES })) {
        assert.ok(words.length > 0, `${name} is empty`);
        for (const word of words) {
            const isKeyword = name !== "SYSTEM_TYPES";
            assert.strictEqual(reserved.has(word), isKeyword, `${name} holds ${word}`);
            assert.ok(word.length < wordSize, `${word} does not fit WORD_SIZE`);
        }
    }
});

test("the scanner spells no reserved word or system type of its own", () => {
    const reserved = new Set([
        ...scannerWords(readGrammar()).RESERVED_WORDS,
        ...Object.keys(require("../../netlinx.js").types),
    ]);
    const scanner = fs.readFileSync(path.join(__dirname, "..", "..", "src", "scanner.c"), "utf8");
    const directives = new Set(
        [...scanner.matchAll(/\{"(\w+)", PREPROC_/g)].map((match) => match[1]),
    );
    const spelled = [...scanner.matchAll(/"([A-Za-z_][A-Za-z0-9_]*)"/g)]
        .map((match) => match[1].toLowerCase())
        .filter((word) => reserved.has(word) && !directives.has(word));
    assert.deepStrictEqual(spelled, [], "take these from src/scanner_words.h");
});
