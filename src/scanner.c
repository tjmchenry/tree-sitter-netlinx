#include "tree_sitter/parser.h"
#include "scanner_words.h"

#include <stdlib.h>
#include <string.h>
#include <wctype.h>

enum TokenType {
    AUTOMATIC_SEMICOLON,
    DECLARATION_CONTINUATION,
    VALUE_CONTINUATION,
    FIELD_CONTINUATION,
    KEYWORD_TYPE_CONTINUATION,
    RESERVED_NAME,
    FIELD_START,
    FIELD_NAME,
    EVENT_DATA,
    EVENT_BUTTON,
    EVENT_CHANNEL,
    EVENT_LEVEL,
    EVENT_TIMELINE,
    EVENT_CUSTOM,
    EVENT_TYPE_CONTEXT,
    COMMENT,
    UNCLOSED_COMMENT,
    STRING_PART,
    STRING_CONTINUATION,
    UNCLOSED_STRING,
    PREPROC_ARG,
    COMMENT_AHEAD,
    ARM_CONTENT,
    LABEL_START,
    ARM_CONTINUATION,
    EMPTY_STATEMENT,
    PREPROC_INCLUDE_KEYWORD,
    PREPROC_DEFINE_KEYWORD,
    PREPROC_WARN_KEYWORD,
    PREPROC_DISABLE_WARNING_KEYWORD,
    PREPROC_IF_DEFINED_KEYWORD,
    PREPROC_IF_NOT_DEFINED_KEYWORD,
    PREPROC_ELSE_KEYWORD,
    PREPROC_END_IF_KEYWORD,
    PREPROC_NAME,
    PREPROC_STRING,
    PREPROC_UNCLOSED_STRING,
    PREPROC_NUMBER,
    ERROR_SENTINEL,
};

// The argument a directive's keyword leaves owed, read as the next tokens.
typedef enum {
    ARGUMENT_NONE,
    // A word: the flag a conditional tests.
    ARGUMENT_NAME,
    // A word, the name a #DEFINE defines, then its value on that word's line.
    ARGUMENT_DEFINED,
    // A #DEFINE's value.
    ARGUMENT_VALUE,
    ARGUMENT_STRING,
    ARGUMENT_NUMBER,
    // The comment a #DEFINE's value stops at.
    ARGUMENT_COMMENT,
} Argument;

// A COMMENT_AHEAD token was the last one returned: the comment it announced
// is the next token. A directive's keyword or a comment inside its argument
// was: `argument` is what the directive still reads. A RESERVED_NAME that
// ends its field, past its type or a comma, was: `after_reserved_name`.
typedef struct {
    bool comment_ahead;
    uint8_t argument;
    bool after_reserved_name;
} Scanner;

void *tree_sitter_netlinx_external_scanner_create() { return calloc(1, sizeof(Scanner)); }

void tree_sitter_netlinx_external_scanner_destroy(void *payload) { free(payload); }

unsigned tree_sitter_netlinx_external_scanner_serialize(void *payload, char *buffer) {
    Scanner *scanner = payload;
    buffer[0] = (char)scanner->comment_ahead;
    buffer[1] = (char)scanner->argument;
    buffer[2] = (char)scanner->after_reserved_name;
    return 3;
}

void tree_sitter_netlinx_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *scanner = payload;
    scanner->comment_ahead = length > 0 && buffer[0];
    scanner->argument = length > 1 ? (uint8_t)buffer[1] : ARGUMENT_NONE;
    scanner->after_reserved_name = length > 2 && buffer[2];
}

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline void move(TSLexer *lexer, bool skipped) { lexer->advance(lexer, skipped); }

static bool is_word_start(int32_t c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Room for the longest word the scanner looks for, and its terminator.
#define WORD_SIZE 32

static bool is_line_end(int32_t c) {
    return c == 0 || c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029;
}

static int compare_words(const void *word, const void *listed) {
    return strcmp(word, *(const char *const *)listed);
}

// Whether `word` is one of `list`, which is sorted.
#define IS_LISTED(word, list) \
    (bsearch((word), (list), sizeof(list) / sizeof((list)[0]), sizeof((list)[0]), compare_words) != NULL)

typedef enum {
    NOT_A_COMMENT,
    LINE_COMMENT,
    BLOCK_COMMENT,
    OPEN_COMMENT,
} CommentKind;

// The one reading of NetLinx comments, by the compiler's rules: `//` runs to
// the end of its line, and a final `\` continues nothing; `(*` and `/*` open
// block comments that nest across both kinds, either closer ends either
// opener, and a closer ends the comment it balances. The lookahead is '(' or
// '/'; when it opens no comment, only that character has been consumed, and
// it stays in the token. A newline inside a block comment sets `newline`.
// With `line_bounded`, as on a #DEFINE's line, a block comment its line leaves
// open ends with the line.
static CommentKind scan_comment(TSLexer *lexer, bool skipped, bool *newline, bool line_bounded) {
    int32_t opener = lexer->lookahead;
    advance(lexer);
    if (opener == '/' && lexer->lookahead == '/') {
        while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
            move(lexer, skipped);
        }
        return LINE_COMMENT;
    }
    if (lexer->lookahead != '*') {
        return NOT_A_COMMENT;
    }
    move(lexer, skipped);
    unsigned depth = 1;
    while (lexer->lookahead != 0) {
        int32_t c = lexer->lookahead;
        if (line_bounded && is_line_end(c)) {
            return BLOCK_COMMENT;
        }
        if (c == '\n' && newline) {
            *newline = true;
        }
        move(lexer, skipped);
        if ((c == '(' || c == '/') && lexer->lookahead == '*') {
            move(lexer, skipped);
            depth++;
        } else if (c == '*' && (lexer->lookahead == ')' || lexer->lookahead == '/')) {
            move(lexer, skipped);
            if (--depth == 0) {
                return BLOCK_COMMENT;
            }
        }
    }
    return OPEN_COMMENT;
}

// The one reading of NetLinx strings, by the compiler's rules: a string runs
// from its quote to the next quote, across lines; with no quote left in its
// file it ends with its line. The lookahead is the opening quote. With
// `mark`, the token ends where the string does. Returns whether a quote
// closed it.
static bool scan_quoted(TSLexer *lexer, bool skipped, bool mark) {
    move(lexer, skipped);
    bool line_ended = false;
    while (lexer->lookahead != 0) {
        if (lexer->lookahead == '\'') {
            move(lexer, skipped);
            if (mark) {
                lexer->mark_end(lexer);
            }
            return true;
        }
        if (!line_ended && (lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
            if (mark) {
                lexer->mark_end(lexer);
            }
            line_ended = true;
        }
        move(lexer, skipped);
    }
    if (mark && !line_ended) {
        lexer->mark_end(lexer);
    }
    return false;
}

static bool lex_comment(TSLexer *lexer) {
    switch (scan_comment(lexer, false, NULL, false)) {
        case LINE_COMMENT:
        case BLOCK_COMMENT:
            lexer->mark_end(lexer);
            lexer->result_symbol = COMMENT;
            return true;
        case OPEN_COMMENT:
            lexer->mark_end(lexer);
            lexer->result_symbol = UNCLOSED_COMMENT;
            return true;
        case NOT_A_COMMENT:
            break;
    }
    return false;
}

static bool lex_string(TSLexer *lexer, enum TokenType closed) {
    lexer->result_symbol = scan_quoted(lexer, false, true) ? closed : UNCLOSED_STRING;
    return true;
}

// A #DEFINE's value: the rest of its line up to a comment, and the next line
// too after a final `\`. The lookahead is the value's first character, or
// the one after it when `started`. Stopping at a comment sets `at_comment`.
static bool lex_define_value(TSLexer *lexer, bool started, bool *at_comment) {
    if (started) {
        lexer->mark_end(lexer);
    }
    for (;;) {
        int32_t c = lexer->lookahead;
        if (c == 0 || c == '\n' || c == '\r') {
            break;
        }
        advance(lexer);
        if ((c == '/' || c == '(') &&
            (lexer->lookahead == '*' || (c == '/' && lexer->lookahead == '/'))) {
            *at_comment = true;
            break;
        }
        if (c == '\\') {
            lexer->mark_end(lexer);
            if (lexer->lookahead == '\r') {
                advance(lexer);
            }
            if (lexer->lookahead == '\n') {
                advance(lexer);
            }
            continue;
        }
        if (!iswspace(c)) {
            lexer->mark_end(lexer);
        }
    }
    lexer->result_symbol = PREPROC_ARG;
    return true;
}

// Look ahead without consuming the next token: mark_end remains at the
// statement boundary. Match whole words so e.g. ANDROID starts a statement.
typedef enum {
    WORD,
    WORD_OPERATOR,
    UNARY_WORD_OPERATOR,
    DECLARATION_MODIFIER,
    STATEMENT_KEYWORD,
    VALUE_STATEMENT_KEYWORD,
    TYPE_WORD,
    EVENT_WORD,
} WordKind;

// Look past a possible call/condition without consuming it. A closing
// parenthesis followed by an expression suffix is a value; a following body
// can instead start a control statement after a bare RETURN.
static bool scan_parentheses(TSLexer *lexer) {
    unsigned depth = 0;
    while (lexer->lookahead != 0) {
        if (lexer->lookahead == '(') {
            if (scan_comment(lexer, false, NULL, false) == NOT_A_COMMENT) {
                depth++;
            }
            continue;
        }
        if (depth == 0) {
            while (iswspace(lexer->lookahead) && lexer->lookahead != '\n' &&
                   lexer->lookahead != 0x2028 && lexer->lookahead != 0x2029) {
                advance(lexer);
            }
            if (lexer->lookahead == '(') {
                continue;
            }
            return false; // Only trivia was found, not a parenthesized value.
        }
        if (lexer->lookahead == '\'') {
            scan_quoted(lexer, false, false);
            continue;
        }
        if (lexer->lookahead == '/') {
            scan_comment(lexer, false, NULL, false);
            continue;
        }
        if (lexer->lookahead == ')') {
            advance(lexer);
            if (--depth == 0) {
                return true;
            }
            continue;
        }
        advance(lexer);
    }
    return false;
}

static WordKind scan_word(TSLexer *lexer, bool value_continuation, const bool *event_symbols);

// Block comments can separate a user type from its declarator. A line
// comment instead ends the expression, as in the semicolon scanner.
static bool event_object_is_type(TSLexer *lexer) {
    for (;;) {
        while (iswspace(lexer->lookahead)) {
            advance(lexer);
        }
        if (lexer->lookahead == '/' || lexer->lookahead == '(') {
            if (scan_comment(lexer, false, NULL, false) != BLOCK_COMMENT) {
                return false;
            }
            continue;
        }
        return is_word_start(lexer->lookahead) &&
               scan_word(lexer, false, NULL) != WORD_OPERATOR;
    }
}

// Preserve the token end while looking for a declarator. Only states which
// can start a declaration need this lookahead; ordinary expression operands
// retain their event tokens even before another statement on the same line.
static bool finish_event_object(TSLexer *lexer, const char *word, const bool *valid_symbols) {
    static const char *names[] = {
        "data", "button", "channel", "level", "timeline", "custom",
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (valid_symbols[EVENT_DATA + i] && strcmp(word, names[i]) == 0) {
            lexer->mark_end(lexer);
            if (valid_symbols[EVENT_TYPE_CONTEXT] && event_object_is_type(lexer)) {
                return false;
            }
            lexer->result_symbol = EVENT_DATA + i;
            return true;
        }
    }
    return false;
}

// Reads a word lowercased. A word too long for `word` is none of the words
// the callers look for.
static bool read_word(TSLexer *lexer, char word[WORD_SIZE]) {
    unsigned length = 0;
    while (is_word_start(lexer->lookahead) || (lexer->lookahead >= '0' && lexer->lookahead <= '9')) {
        if (length == WORD_SIZE - 1) {
            return false;
        }
        word[length++] = (char)towlower(lexer->lookahead);
        advance(lexer);
    }
    word[length] = '\0';
    return true;
}

// Whether `word` is refused where the parse stands: a reserved word where a
// field's name goes after a comma, or one no field begins with where a field
// begins.
static bool is_refused_field_name(const char *word, const bool *valid_symbols) {
    if (valid_symbols[ERROR_SENTINEL] || !IS_LISTED(word, RESERVED_WORDS)) {
        return false;
    }
    return valid_symbols[FIELD_NAME] || (valid_symbols[FIELD_START] && !IS_LISTED(word, FIELD_START_WORDS));
}

// Whether a closing brace, a semicolon, a comment or the line's end follows
// the reserved name just read: its field ends there, so the error stays the
// name's alone.
static bool reserved_name_ends_its_field(TSLexer *lexer) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        skip(lexer);
    }
    if (lexer->lookahead == '/') {
        skip(lexer);
        return lexer->lookahead == '/' || lexer->lookahead == '*';
    }
    if (lexer->lookahead == '(') {
        skip(lexer);
        return lexer->lookahead == '*';
    }
    return lexer->lookahead == '}' || lexer->lookahead == ';' || is_line_end(lexer->lookahead);
}

// In name positions the event tokens are invalid and identifier lexing wins.
// In expressions retain the named event token, including bare operands and
// assignment targets, so system_variable and aliased identifier trees survive.
static bool scan_event_object(TSLexer *lexer, const bool *valid_symbols) {
    bool valid = false;
    for (unsigned i = EVENT_DATA; i <= EVENT_CUSTOM; i++) {
        valid |= valid_symbols[i];
    }
    if (!valid) {
        return false;
    }
    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }
    char word[WORD_SIZE];
    return read_word(lexer, word) && finish_event_object(lexer, word, valid_symbols);
}

static bool is_word_char(int32_t c) {
    return is_word_start(c) || (c >= '0' && c <= '9');
}

// Whether `argument` follows, past whitespace and comments: a token of its
// kind is next.
static bool argument_follows(TSLexer *lexer, Argument argument) {
    for (;;) {
        while (iswspace(lexer->lookahead)) {
            advance(lexer);
        }
        if (lexer->lookahead != '/' && lexer->lookahead != '(') {
            break;
        }
        CommentKind kind = scan_comment(lexer, false, NULL, false);
        if (kind != LINE_COMMENT && kind != BLOCK_COMMENT) {
            return false;
        }
    }
    switch (argument) {
        case ARGUMENT_NAME:
        case ARGUMENT_DEFINED:
            return is_word_start(lexer->lookahead);
        case ARGUMENT_STRING:
            return lexer->lookahead == '\'';
        case ARGUMENT_NUMBER:
            return iswdigit(lexer->lookahead);
        default:
            return false;
    }
}

// Whether a #DEFINE's value, or a block comment before it, follows on the
// line.
static bool value_follows(TSLexer *lexer) {
    while (iswspace(lexer->lookahead) && !is_line_end(lexer->lookahead)) {
        advance(lexer);
    }
    if (is_line_end(lexer->lookahead)) {
        return false;
    }
    if (lexer->lookahead == '/') {
        advance(lexer);
        return lexer->lookahead != '/';
    }
    return true;
}

// A directive's keyword and the argument it owes.
static const struct {
    const char *word;
    enum TokenType token;
    Argument argument;
} DIRECTIVES[] = {
    {"if_defined", PREPROC_IF_DEFINED_KEYWORD, ARGUMENT_NAME},
    {"if_not_defined", PREPROC_IF_NOT_DEFINED_KEYWORD, ARGUMENT_NAME},
    {"else", PREPROC_ELSE_KEYWORD, ARGUMENT_NONE},
    {"end_if", PREPROC_END_IF_KEYWORD, ARGUMENT_NONE},
    {"define", PREPROC_DEFINE_KEYWORD, ARGUMENT_DEFINED},
    {"include", PREPROC_INCLUDE_KEYWORD, ARGUMENT_STRING},
    {"warn", PREPROC_WARN_KEYWORD, ARGUMENT_STRING},
    {"disable_warning", PREPROC_DISABLE_WARNING_KEYWORD, ARGUMENT_NUMBER},
};

// The directive keyword `word` names, lowercase, ends here: its token, with
// the argument it owes when that argument follows.
static bool finish_directive_keyword(Scanner *scanner, TSLexer *lexer, const char *word) {
    for (unsigned i = 0; i < sizeof(DIRECTIVES) / sizeof(DIRECTIVES[0]); i++) {
        if (strcmp(word, DIRECTIVES[i].word) == 0) {
            lexer->mark_end(lexer);
            lexer->result_symbol = DIRECTIVES[i].token;
            Argument argument = DIRECTIVES[i].argument;
            scanner->argument =
                argument != ARGUMENT_NONE && argument_follows(lexer, argument) ? argument : ARGUMENT_NONE;
            return true;
        }
    }
    return false;
}

// A directive's keyword: the lookahead is `#`.
static bool lex_directive_keyword(Scanner *scanner, TSLexer *lexer) {
    advance(lexer);
    char word[16];
    unsigned length = 0;
    while (is_word_char(lexer->lookahead)) {
        if (length == sizeof(word) - 1) {
            return false;
        }
        word[length++] = (char)towlower(lexer->lookahead);
        advance(lexer);
    }
    word[length] = '\0';
    return finish_directive_keyword(scanner, lexer, word);
}

// What a directive's keyword owes: its argument, or a comment before it.
static bool scan_argument(Scanner *scanner, TSLexer *lexer) {
    Argument argument = scanner->argument;
    scanner->argument = ARGUMENT_NONE;
    bool on_define_line = argument == ARGUMENT_VALUE || argument == ARGUMENT_COMMENT;
    bool at_comment = false;
    if (on_define_line) {
        while (iswspace(lexer->lookahead) && !is_line_end(lexer->lookahead)) {
            skip(lexer);
        }
    } else {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
    }
    if (lexer->lookahead == '/' || lexer->lookahead == '(') {
        bool newline = false;
        switch (scan_comment(lexer, false, &newline, on_define_line)) {
            case LINE_COMMENT:
            case BLOCK_COMMENT:
                lexer->mark_end(lexer);
                lexer->result_symbol = COMMENT;
                if (argument == ARGUMENT_VALUE ? !newline && value_follows(lexer) : argument != ARGUMENT_COMMENT) {
                    scanner->argument = argument;
                }
                return true;
            case OPEN_COMMENT:
                lexer->mark_end(lexer);
                lexer->result_symbol = UNCLOSED_COMMENT;
                return true;
            case NOT_A_COMMENT:
                if (argument != ARGUMENT_VALUE) {
                    return false;
                }
                lex_define_value(lexer, true, &at_comment);
                scanner->argument = at_comment ? ARGUMENT_COMMENT : ARGUMENT_NONE;
                return true;
        }
    }
    switch (argument) {
        case ARGUMENT_NAME:
        case ARGUMENT_DEFINED:
            while (is_word_char(lexer->lookahead)) {
                advance(lexer);
            }
            lexer->mark_end(lexer);
            lexer->result_symbol = PREPROC_NAME;
            if (argument == ARGUMENT_DEFINED && value_follows(lexer)) {
                scanner->argument = ARGUMENT_VALUE;
            }
            return true;
        case ARGUMENT_VALUE:
            if (is_line_end(lexer->lookahead)) {
                return false;
            }
            lex_define_value(lexer, false, &at_comment);
            scanner->argument = at_comment ? ARGUMENT_COMMENT : ARGUMENT_NONE;
            return true;
        case ARGUMENT_STRING:
            lexer->result_symbol = scan_quoted(lexer, false, true) ? PREPROC_STRING : PREPROC_UNCLOSED_STRING;
            return true;
        case ARGUMENT_NUMBER:
            while (iswdigit(lexer->lookahead)) {
                advance(lexer);
            }
            lexer->mark_end(lexer);
            lexer->result_symbol = PREPROC_NUMBER;
            return true;
        default:
            return false;
    }
}

// The whole word INCLUDE starts a directive wherever it stands. ACTIVE is
// reserved, but where no label may start it would lex as an identifier and
// carry the label into an expression. Where an operand is owed it ends the
// statement there, short of that operand; anywhere else the empty token
// before it, which only a label start takes, ends the parse at the misplaced
// label. A reserved word where a field's name goes is RESERVED_NAME, with the
// semicolon after it where a field begins.
static bool scan_leading_word(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool label_may_start) {
    lexer->mark_end(lexer);
    char word[WORD_SIZE];
    if (!read_word(lexer, word)) {
        return false;
    }
    if (strcmp(word, "include") == 0) {
        return finish_directive_keyword(scanner, lexer, word);
    }
    if (!label_may_start && IS_LISTED(word, ARM_WORDS)) {
        bool operand = false;
        for (unsigned i = EVENT_DATA; i <= EVENT_CUSTOM; i++) {
            operand |= valid_symbols[i];
        }
        lexer->result_symbol =
            operand && !valid_symbols[EVENT_TYPE_CONTEXT] ? AUTOMATIC_SEMICOLON : LABEL_START;
        return true;
    }
    if (is_refused_field_name(word, valid_symbols)) {
        lexer->mark_end(lexer);
        lexer->result_symbol = RESERVED_NAME;
        if (valid_symbols[FIELD_START]) {
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                advance(lexer);
            }
            if (lexer->lookahead == ';') {
                advance(lexer);
                lexer->mark_end(lexer);
            }
        } else {
            scanner->after_reserved_name = reserved_name_ends_its_field(lexer);
        }
        return true;
    }
    return finish_event_object(lexer, word, valid_symbols);
}

static WordKind scan_word(TSLexer *lexer, bool value_continuation, const bool *event_symbols) {
    char word[WORD_SIZE];
    unsigned length = 0;
    while (is_word_start(lexer->lookahead) || (lexer->lookahead >= '0' && lexer->lookahead <= '9')) {
        if (length == sizeof(word) - 1) {
            return WORD;
        }
        word[length++] = (char)towlower(lexer->lookahead);
        advance(lexer);
    }
    word[length] = '\0';

    if (event_symbols && finish_event_object(lexer, word, event_symbols)) {
        return EVENT_WORD;
    }

    // A keyword spelling followed by an expression boundary or operator is
    // still an identifier operand, not the beginning of another statement.
    // This applies to every word, including reserved-looking names like WAIT.
    if (value_continuation) {
        while (iswspace(lexer->lookahead) && lexer->lookahead != '\n' &&
               lexer->lookahead != 0x2028 && lexer->lookahead != 0x2029) {
            advance(lexer);
        }
        if (lexer->lookahead == '(') {
            if (!scan_parentheses(lexer)) {
                return WORD;
            }
            while (iswspace(lexer->lookahead) && lexer->lookahead != '\n' &&
                   lexer->lookahead != 0x2028 && lexer->lookahead != 0x2029) {
                advance(lexer);
            }
        }
        switch (lexer->lookahead) {
            case 0: case '}': case ';': case '\n': case 0x2028: case 0x2029:
            case '(': case '[': case ')': case ']': case ',': case '.': case ':':
            case '+': case '-': case '*': case '/': case '%': case '=':
            case '<': case '>': case '&': case '|': case '^': case '!':
                return WORD;
            default:
                break;
        }
        // A binary word operator also proves that this word is an operand.
        // The nested scan only looks ahead; the token still ends at the
        // original mark_end, and value_continuation=false bounds recursion.
        if (is_word_start(lexer->lookahead) && scan_word(lexer, false, NULL) == WORD_OPERATOR) {
            return WORD;
        }
    }

    if (IS_LISTED(word, WORD_OPERATORS)) {
        return WORD_OPERATOR;
    }
    if (IS_LISTED(word, UNARY_WORD_OPERATORS)) {
        return UNARY_WORD_OPERATOR;
    }
    if (IS_LISTED(word, DECLARATION_MODIFIERS)) {
        return DECLARATION_MODIFIER;
    }

    // Operation words can also be identifier values (notably ON and OFF).
    // At an optional RETURN operand they continue the value, just like WORD.
    if (IS_LISTED(word, VALUE_STATEMENT_WORDS)) {
        return VALUE_STATEMENT_KEYWORD;
    }

    // Prefer a still-incomplete declaration over starting a new one.
    if (IS_LISTED(word, KEYWORD_TYPES) || IS_LISTED(word, SYSTEM_TYPES)) {
        return TYPE_WORD;
    }

    // Control and section keywords can begin a following statement. Operand
    // spellings above and incomplete field names take priority. In particular,
    // `RETURN IF (a) ...` can still end a bare RETURN before the IF statement.
    if (IS_LISTED(word, STATEMENT_WORDS)) {
        return STATEMENT_KEYWORD;
    }
    return WORD;
}

typedef enum {
    REJECT,     // Semicolon is illegal, ie a syntax error occurred
    NO_NEWLINE, // Unclear if semicolon will be legal, continue
    ACCEPT,     // Semicolon is legal, assuming a comment was encountered
    PAREN,      // Stopped at a '(' that opens no comment
} WhitespaceResult;

/**
 * Skip whitespace and comments. `first_comment`, when given, is set if the
 * first thing after the whitespace is a comment.
 *
 * @param consume If false, only consume enough to check if comment indicates semicolon-legality
 */
static WhitespaceResult scan_whitespace_and_comments(TSLexer *lexer, bool consume, bool *first_comment) {
    bool saw_block_newline = false;

    for (;;) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }

        if (lexer->lookahead != '/' && lexer->lookahead != '(') {
            return ACCEPT;
        }
        bool paren = lexer->lookahead == '(';
        bool newline = false;
        CommentKind kind = scan_comment(lexer, true, &newline, false);
        if (kind == NOT_A_COMMENT) {
            return paren ? PAREN : REJECT;
        }
        if (first_comment) {
            *first_comment = true;
            first_comment = NULL;
        }
        if (kind == BLOCK_COMMENT) {
            saw_block_newline |= newline;
            if (!consume && lexer->lookahead != '/' && lexer->lookahead != '(') {
                return saw_block_newline ? ACCEPT : NO_NEWLINE;
            }
        }
    }
}

typedef enum {
    SEMICOLON_NO,
    SEMICOLON_YES,
    SEMICOLON_EVENT_TOKEN, // result_symbol holds the event token found instead
    SEMICOLON_RESERVED_NAME, // result_symbol holds RESERVED_NAME
} SemicolonResult;

// What follows the statement boundary: a comment first, or a quote first on
// the same line.
typedef struct {
    bool comment;
    bool quote;
} Ahead;

// Whether the word at the lookahead can be a keyword type's declarator: a
// subscript, a parameter list, a comma, a semicolon, a comment, a closing
// brace, another word or its line's end follows it. Anything else ends the
// keyword type's field, so a parse error stays on its own line.
static bool word_can_be_declarator(TSLexer *lexer) {
    while (is_word_char(lexer->lookahead)) {
        skip(lexer);
    }
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        skip(lexer);
    }
    switch (lexer->lookahead) {
        case '[':
        case '(':
        case ',':
        case ';':
        case '/':
        case '}':
            return true;
        default:
            return is_word_start(lexer->lookahead) || is_line_end(lexer->lookahead);
    }
}

// Reads the word at the lookahead as RESERVED_NAME when it is a reserved word,
// which no field's name may be.
static bool lex_reserved_name(TSLexer *lexer) {
    char word[WORD_SIZE];
    if (!read_word(lexer, word) || !IS_LISTED(word, RESERVED_WORDS)) {
        return false;
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = RESERVED_NAME;
    return true;
}

// What a field's keyword type takes from the word or closing brace at the
// lookahead. A keyword type is never a field alone: before a closing brace
// no semicolon ends it. `may_lex` is false once a comment has been passed.
static SemicolonResult after_keyword_type(TSLexer *lexer, bool may_lex) {
    if (lexer->lookahead == '}') {
        return SEMICOLON_NO;
    }
    if (may_lex && lex_reserved_name(lexer)) {
        return SEMICOLON_RESERVED_NAME;
    }
    return word_can_be_declarator(lexer) ? SEMICOLON_NO : SEMICOLON_YES;
}

// Whether a semicolon belongs at the current position. Once a comment has
// been passed the scan only looks: mark_end stays at the boundary, so no
// other token can be returned over that comment.
static SemicolonResult scan_automatic_semicolon(TSLexer *lexer, const bool *valid_symbols, Ahead *ahead) {
    bool declaration_continuation = valid_symbols[DECLARATION_CONTINUATION];
    bool value_continuation = valid_symbols[VALUE_CONTINUATION];
    bool field_continuation = valid_symbols[FIELD_CONTINUATION];
    bool keyword_type = valid_symbols[KEYWORD_TYPE_CONTINUATION];
    lexer->result_symbol = AUTOMATIC_SEMICOLON;
    lexer->mark_end(lexer);
    bool saw_whitespace = false;
    bool first = true;

    for (;;) {
        if (lexer->lookahead == 0) {
            return SEMICOLON_YES;    // EOF - insert a semicolon
        }

        if (lexer->lookahead == '/' || lexer->lookahead == '(') {
            WhitespaceResult result = scan_whitespace_and_comments(lexer, false, first ? &ahead->comment : NULL);
            if (result == REJECT || result == PAREN) {
                return SEMICOLON_NO;
            }
            first = false;

            if (result == ACCEPT && lexer->lookahead != ',' && lexer->lookahead != '=') {
                if (keyword_type && (lexer->lookahead == '}' || is_word_start(lexer->lookahead))) {
                    return after_keyword_type(lexer, false);
                }
                return SEMICOLON_YES;
            }
        }

        if (lexer->lookahead == '}') {
            return keyword_type ? SEMICOLON_NO : SEMICOLON_YES;
        }

        if (lexer->is_at_included_range_start(lexer)) {
            return SEMICOLON_YES;
        }

        if (lexer->lookahead == '\n' || lexer->lookahead == 0x2028 || lexer->lookahead == 0x2029) {
            break;  // Found a newline - potentially insert a semicolon
        }

        if (!iswspace(lexer->lookahead)) {
            ahead->quote = first && lexer->lookahead == '\'';
            // NLRC also separates statements on one line. Parentheses,
            // brackets and symbolic operators still belong to the expression.
            if (lexer->lookahead == '{') {
                return value_continuation ? SEMICOLON_NO : SEMICOLON_YES;
            }
            const bool *event_symbols =
                (declaration_continuation || value_continuation) && !ahead->comment ? valid_symbols : NULL;
            if (saw_whitespace && is_word_start(lexer->lookahead)) {
                if (keyword_type) {
                    return after_keyword_type(lexer, !ahead->comment) == SEMICOLON_RESERVED_NAME
                        ? SEMICOLON_RESERVED_NAME
                        : SEMICOLON_NO;
                }
                if (field_continuation) {
                    return !ahead->comment && lex_reserved_name(lexer) ? SEMICOLON_RESERVED_NAME : SEMICOLON_NO;
                }
                WordKind word = scan_word(lexer, value_continuation, event_symbols);
                if (word == EVENT_WORD) {
                    return SEMICOLON_EVENT_TOKEN;
                }
                return word == STATEMENT_KEYWORD ||
                       (word == VALUE_STATEMENT_KEYWORD && !value_continuation && !declaration_continuation) ||
                       (word == TYPE_WORD && !declaration_continuation && !value_continuation) ||
                       (word == UNARY_WORD_OPERATOR && !declaration_continuation && !value_continuation) ||
                       (word == DECLARATION_MODIFIER && !declaration_continuation) ||
                       (word == WORD && !declaration_continuation && !value_continuation)
                    ? SEMICOLON_YES
                    : SEMICOLON_NO;
            }
            if (ahead->comment || ahead->quote) {
                return SEMICOLON_NO;
            }
            return scan_event_object(lexer, valid_symbols) ? SEMICOLON_EVENT_TOKEN : SEMICOLON_NO;
        }

        saw_whitespace = true;
        skip(lexer);
    }

    skip(lexer);    // Skip newline

    WhitespaceResult result = scan_whitespace_and_comments(lexer, true, first ? &ahead->comment : NULL);
    if (result == REJECT) {
        return SEMICOLON_NO;
    }
    if (result == PAREN) {
        return SEMICOLON_YES;
    }
    if (keyword_type && (lexer->lookahead == '}' || is_word_start(lexer->lookahead))) {
        return after_keyword_type(lexer, !ahead->comment);
    }

    // Don't insert a semicolon before these characters
    switch (lexer->lookahead) {
        case ',':
        case ':':
        case ';':
        case '*':
        case '%':
        case '>':
        case '<':
        case '=':
        case '^':
        case '|':
        case '&':
        case '/':
            return SEMICOLON_NO;

        // Insert a semicolon before opening parenthesis or bracket when preceded by a closing one
        case '(':
        case '[':
            return SEMICOLON_YES;

        // Insert a semicolon before decimals literals but not otherwise.
        case '.':
            skip(lexer);
            return iswdigit(lexer->lookahead) ? SEMICOLON_YES : SEMICOLON_NO;

        // Insert a semicolon before `--` and `++`, but not before binary `+` or `-`.
        case '+':
            skip(lexer);
            return lexer->lookahead == '+' ? SEMICOLON_YES : SEMICOLON_NO;
        case '-':
            skip(lexer);
            return lexer->lookahead == '-' ? SEMICOLON_YES : SEMICOLON_NO;

        // Don't insert a semicolon before `!=`, but do insert one before a unary `!`.
        case '!':
            skip(lexer);
            return lexer->lookahead != '=' ? SEMICOLON_YES : SEMICOLON_NO;

        default:
            break;
    }

    // By default insert a semicolon
    return SEMICOLON_YES;
}

bool tree_sitter_netlinx_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *scanner = payload;
    bool comment_ahead = scanner->comment_ahead;
    scanner->comment_ahead = false;
    bool after_reserved_name = scanner->after_reserved_name;
    scanner->after_reserved_name = false;
    bool error_mode = valid_symbols[ERROR_SENTINEL];

    if (comment_ahead) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
        return lex_comment(lexer);
    }

    if (scanner->argument != ARGUMENT_NONE) {
        return scan_argument(scanner, lexer);
    }

    if (!error_mode && valid_symbols[STRING_CONTINUATION] && lexer->lookahead == '\'') {
        return lex_string(lexer, STRING_CONTINUATION);
    }

    if (after_reserved_name && valid_symbols[AUTOMATIC_SEMICOLON]) {
        lexer->result_symbol = AUTOMATIC_SEMICOLON;
        lexer->mark_end(lexer);
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            skip(lexer);
        }
        if (lexer->lookahead == ';') {
            advance(lexer);
            lexer->mark_end(lexer);
        }
        return true;
    }

    if (valid_symbols[AUTOMATIC_SEMICOLON] && !error_mode) {
        Ahead ahead = {false, false};
        switch (scan_automatic_semicolon(lexer, valid_symbols, &ahead)) {
            case SEMICOLON_YES:
                return true;
            case SEMICOLON_EVENT_TOKEN:
                return true;
            case SEMICOLON_RESERVED_NAME:
                scanner->after_reserved_name = reserved_name_ends_its_field(lexer);
                return true;
            case SEMICOLON_NO:
                break;
        }
        if (ahead.comment) {
            scanner->comment_ahead = true;
            lexer->result_symbol = COMMENT_AHEAD;
            return true;
        }
        if (lexer->lookahead == '#') {
            return lex_directive_keyword(scanner, lexer);
        }
        return ahead.quote && valid_symbols[STRING_PART] && lex_string(lexer, STRING_PART);
    }

    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }
    if (lexer->lookahead == ';') {
        if (!valid_symbols[EMPTY_STATEMENT] || error_mode) {
            return false;
        }
        advance(lexer);
        lexer->result_symbol = EMPTY_STATEMENT;
        return true;
    }
    if (lexer->lookahead == '(' || lexer->lookahead == '/') {
        return lex_comment(lexer);
    }
    if (lexer->lookahead == '\'') {
        return (valid_symbols[STRING_PART] || error_mode) && lex_string(lexer, STRING_PART);
    }
    if (lexer->lookahead == '#') {
        return lex_directive_keyword(scanner, lexer);
    }
    return scan_leading_word(scanner, lexer, valid_symbols, valid_symbols[LABEL_START] || error_mode);
}
