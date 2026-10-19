#include "tree_sitter/parser.h"

#include <stdlib.h>
#include <string.h>
#include <wctype.h>

enum TokenType {
    AUTOMATIC_SEMICOLON,
    DECLARATION_CONTINUATION,
    VALUE_CONTINUATION,
    PREPROC_CONTINUATION,
    FIELD_CONTINUATION,
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
    ERROR_SENTINEL,
};

// A COMMENT_AHEAD token was the last one returned: the comment it announced
// is the next token.
typedef struct {
    bool comment_ahead;
} Scanner;

void *tree_sitter_netlinx_external_scanner_create() { return calloc(1, sizeof(Scanner)); }

void tree_sitter_netlinx_external_scanner_destroy(void *payload) { free(payload); }

unsigned tree_sitter_netlinx_external_scanner_serialize(void *payload, char *buffer) {
    buffer[0] = (char)((Scanner *)payload)->comment_ahead;
    return 1;
}

void tree_sitter_netlinx_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    ((Scanner *)payload)->comment_ahead = length > 0 && buffer[0];
}

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline void move(TSLexer *lexer, bool skipped) { lexer->advance(lexer, skipped); }

static bool is_word_start(int32_t c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

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
static CommentKind scan_comment(TSLexer *lexer, bool skipped, bool *newline) {
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
    switch (scan_comment(lexer, false, NULL)) {
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
// the one after it when `started`.
static bool lex_define_value(TSLexer *lexer, bool started) {
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
            if (scan_comment(lexer, false, NULL) == NOT_A_COMMENT) {
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
            scan_comment(lexer, false, NULL);
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
            if (scan_comment(lexer, false, NULL) != BLOCK_COMMENT) {
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
    char word[9];
    unsigned length = 0;
    while (is_word_start(lexer->lookahead) || (lexer->lookahead >= '0' && lexer->lookahead <= '9')) {
        if (length == sizeof(word) - 1) {
            return false;
        }
        word[length++] = (char)towlower(lexer->lookahead);
        advance(lexer);
    }
    word[length] = '\0';
    return finish_event_object(lexer, word, valid_symbols);
}

static WordKind scan_word(TSLexer *lexer, bool value_continuation, const bool *event_symbols) {
    char word[32];
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

    static const char *operators[] = {
        "and", "or", "xor", "band", "bor", "bxor", "lshift", "rshift", "mod",
    };
    for (unsigned i = 0; i < sizeof(operators) / sizeof(operators[0]); i++) {
        if (strcmp(word, operators[i]) == 0) {
            return WORD_OPERATOR;
        }
    }

    if (strcmp(word, "not") == 0 || strcmp(word, "bnot") == 0) {
        return UNARY_WORD_OPERATOR;
    }
    static const char *modifiers[] = {
        "constant", "volatile", "non_volatile", "persistent", "local_var", "stack_var",
    };
    for (unsigned i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); i++) {
        if (strcmp(word, modifiers[i]) == 0) {
            return DECLARATION_MODIFIER;
        }
    }

    // Operation words can also be identifier values (notably ON and OFF).
    // At an optional RETURN operand they continue the value, just like WORD.
    static const char *value_statements[] = {
        "on", "off", "to", "min_to", "total_off", "pulse",
        "send_level", "send_string", "send_command", "clear_buffer", "create_buffer",
        "create_multi_buffer", "create_level", "call", "system_call",
        "break", "continue", "cancel_all_wait", "cancel_all_wait_until",
        "pause_all_wait", "restart_all_wait",
    };
    for (unsigned i = 0; i < sizeof(value_statements) / sizeof(value_statements[0]); i++) {
        if (strcmp(word, value_statements[i]) == 0) {
            return VALUE_STATEMENT_KEYWORD;
        }
    }

    // Prefer a still-incomplete declaration over starting a new one.
    static const char *types[] = {
        "char", "widechar", "integer", "sinteger", "long", "slong", "float", "double",
        "variant", "variantarray", "dev", "devlev", "devchan",
        "tdata", "tchannel", "tlevel", "tbutton", "ttimeline", "tcustom",
        "url_struct", "dns_struct", "ip_address_struct", "dev_info_struct",
        "clkmgr_timeserver_struct", "data_feed", "data_field", "data_record",
        "wc_data_feed", "wc_data_field", "wc_data_record", "last_login_info",
    };
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (strcmp(word, types[i]) == 0) {
            return TYPE_WORD;
        }
    }

    // Control and section keywords can begin a following statement. Operand
    // spellings above and incomplete field names take priority. In particular,
    // `RETURN IF (a) ...` can still end a bare RETURN before the IF statement.
    static const char *statements[] = {
        "if", "else", "switch", "case", "default", "while", "long_while", "medium_while",
        "for", "return", "select", "active",
        "wait", "wait_until", "timed_wait_until", "cancel_wait", "cancel_wait_until",
        "pause_wait", "restart_wait",
        "program_name", "module_name", "define_device", "define_combine", "define_constant",
        "define_type", "define_variable", "define_system_variable", "define_connect_level",
        "define_latching", "define_mutually_exclusive", "define_toggling", "define_call",
        "define_function", "define_library_function", "define_start", "define_event",
        "define_program", "define_module",
        "struct", "structure", "button_event", "channel_event", "level_event", "data_event",
        "timeline_event", "custom_event", "push", "release",
    };
    for (unsigned i = 0; i < sizeof(statements) / sizeof(statements[0]); i++) {
        if (strcmp(word, statements[i]) == 0) {
            return STATEMENT_KEYWORD;
        }
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
        CommentKind kind = scan_comment(lexer, true, &newline);
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
} SemicolonResult;

// What follows the statement boundary: a comment first, a quote first on the
// same line, or a #DEFINE's value first on the same line (its first character
// already read when `value_started`).
typedef struct {
    bool comment;
    bool quote;
    bool value;
    bool value_started;
} Ahead;

// Whether a semicolon belongs at the current position. Once a comment has
// been passed the scan only looks: mark_end stays at the boundary, so no
// other token can be returned over that comment.
static SemicolonResult scan_automatic_semicolon(TSLexer *lexer, const bool *valid_symbols, Ahead *ahead) {
    bool declaration_continuation = valid_symbols[DECLARATION_CONTINUATION];
    bool value_continuation = valid_symbols[VALUE_CONTINUATION];
    bool preproc_continuation = valid_symbols[PREPROC_CONTINUATION];
    bool field_continuation = valid_symbols[FIELD_CONTINUATION];
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
                ahead->value = ahead->value_started = first && preproc_continuation;
                return SEMICOLON_NO;
            }
            first = false;

            if (result == ACCEPT && lexer->lookahead != ',' && lexer->lookahead != '=') {
                return SEMICOLON_YES;
            }
        }

        if (lexer->lookahead == '}') {
            return SEMICOLON_YES; // Before closing brace - insert a semicolon
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
            // A macro argument consumes the rest of its line, even IF, etc.
            if (preproc_continuation) {
                ahead->value = first;
                return SEMICOLON_NO;
            }
            if (lexer->lookahead == '{') {
                return value_continuation ? SEMICOLON_NO : SEMICOLON_YES;
            }
            const bool *event_symbols =
                (declaration_continuation || value_continuation) && !ahead->comment ? valid_symbols : NULL;
            if (saw_whitespace && is_word_start(lexer->lookahead)) {
                if (field_continuation) {
                    return SEMICOLON_NO;
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
    bool error_mode = valid_symbols[ERROR_SENTINEL];

    if (comment_ahead) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }
        return lex_comment(lexer);
    }

    if (!error_mode && valid_symbols[STRING_CONTINUATION] && lexer->lookahead == '\'') {
        return lex_string(lexer, STRING_CONTINUATION);
    }

    if (valid_symbols[AUTOMATIC_SEMICOLON] && !error_mode) {
        Ahead ahead = {false, false, false, false};
        switch (scan_automatic_semicolon(lexer, valid_symbols, &ahead)) {
            case SEMICOLON_YES:
                return true;
            case SEMICOLON_EVENT_TOKEN:
                return true;
            case SEMICOLON_NO:
                break;
        }
        if (ahead.comment) {
            scanner->comment_ahead = true;
            lexer->result_symbol = COMMENT_AHEAD;
            return true;
        }
        if (ahead.value && valid_symbols[PREPROC_ARG]) {
            return lex_define_value(lexer, ahead.value_started);
        }
        return ahead.quote && valid_symbols[STRING_PART] && lex_string(lexer, STRING_PART);
    }

    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }
    if (lexer->lookahead == '(' || lexer->lookahead == '/') {
        return lex_comment(lexer);
    }
    if (lexer->lookahead == '\'') {
        return (valid_symbols[STRING_PART] || error_mode) && lex_string(lexer, STRING_PART);
    }
    return scan_event_object(lexer, valid_symbols);
}
