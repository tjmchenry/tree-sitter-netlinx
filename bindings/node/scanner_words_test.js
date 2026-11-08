/// <reference types="node" />

const assert = require("node:assert");
const fs = require("node:fs");
const { test } = require("node:test");

const {
    scannerWords,
    scannerWordsHeader,
    readGrammar,
    headerPath,
} = require("../../scripts/scanner-words.js");

test("the scanner's word lists are the grammar's", () => {
    assert.strictEqual(
        fs.readFileSync(headerPath, "utf8"),
        scannerWordsHeader(readGrammar()),
        "src/scanner_words.h is stale: run `pnpm generate`",
    );
});

test("each of the scanner's word lists is reserved and not empty", () => {
    const lists = scannerWords(readGrammar());
    const reserved = new Set(lists.RESERVED_WORDS);
    for (const [name, words] of Object.entries(lists)) {
        assert.ok(words.length > 0, `${name} is empty`);
        for (const word of words) {
            assert.ok(reserved.has(word), `${name} holds ${word}, which no rule spells`);
        }
    }
});
