#include "tree_sitter/parser.h"

#include <stdio.h>
#include <string.h>
#include <wctype.h>

#define DEBUG_AUTOMATIC_SEMICOLON 0

enum TokenType {
    AUTOMATIC_SEMICOLON,
    DECLARATION_CONTINUATION,
    VALUE_CONTINUATION,
    PREPROC_CONTINUATION,
};

void *tree_sitter_netlinx_external_scanner_create() { return NULL; }

void tree_sitter_netlinx_external_scanner_destroy(void *p) {}

unsigned tree_sitter_netlinx_external_scanner_serialize(void *payload, char *buffer) { return 0; }

void tree_sitter_netlinx_external_scanner_deserialize(void *p, const char *b, unsigned n) {}

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static bool is_word_start(int32_t c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Look ahead without consuming the next token: mark_end remains at the
// statement boundary. Match whole words so e.g. ANDROID starts a statement.
typedef enum {
    WORD,
    WORD_OPERATOR,
    UNARY_WORD_OPERATOR,
    DECLARATION_MODIFIER,
    STATEMENT_KEYWORD,
} WordKind;

static WordKind scan_word(TSLexer *lexer) {
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

    // These cannot supply a declarator or RETURN operand. In particular,
    // don't reinterpret IF as a function name in `RETURN IF (a) ...`.
    static const char *statements[] = {
        "if", "else", "switch", "case", "default", "while", "long_while", "medium_while",
        "for", "break", "continue", "return", "select", "active",
        "on", "off", "to", "min_to", "total_off", "pulse",
        "send_level", "send_string", "send_command", "clear_buffer", "create_buffer",
        "create_multi_buffer", "create_level", "call", "system_call",
        "wait", "wait_until", "timed_wait_until", "cancel_wait", "cancel_wait_until",
        "cancel_all_wait", "cancel_all_wait_until", "pause_wait", "pause_all_wait",
        "restart_wait", "restart_all_wait",
        "program_name", "module_name", "define_device", "define_combine", "define_constant",
        "define_type", "define_variable", "define_system_variable", "define_connect_level",
        "define_latching", "define_mutually_exclusive", "define_toggling", "define_call",
        "define_function", "define_library_function", "define_start", "define_event",
        "define_program", "define_module",
        "struct", "structure", "button_event", "channel_event", "level_event", "data_event",
        "timeline_event", "custom_event", "push", "release",
        "char", "widechar", "integer", "sinteger", "long", "slong", "float", "double",
        "variant", "variantarray", "dev", "devlev", "devchan",
        "tdata", "tchannel", "tlevel", "tbutton", "ttimeline", "tcustom",
        "url_struct", "dns_struct", "ip_address_struct", "dev_info_struct",
        "clkmgr_timeserver_struct", "data_feed", "data_field", "data_record",
        "wc_data_feed", "wc_data_field", "wc_data_record", "last_login_info",
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
} WhitespaceResult;

/**
 * @param consume If false, only consume enough to check if comment indicates semicolon-legality
 */
static WhitespaceResult scan_whitespace_and_comments(TSLexer *lexer, bool *scanned_comment, bool consume) {
    bool saw_block_newline = false;

    for (;;) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }

        if (lexer->lookahead == '/') {
            skip(lexer);

            if (lexer->lookahead == '/') {
                skip(lexer);

                while (lexer->lookahead != 0 && lexer->lookahead != '\n' && lexer->lookahead != 0x2028 &&
                       lexer->lookahead != 0x2029) {
                    skip(lexer);
                }

                *scanned_comment = true;
            } else if (lexer->lookahead == '*') {
                skip(lexer);

                while (lexer->lookahead != 0) {
                    if (lexer->lookahead == '*') {
                        skip(lexer);

                        if (lexer->lookahead == '/') {
                            skip(lexer);
                            *scanned_comment = true;

                            if (lexer->lookahead != '/' && !consume) {
                                return saw_block_newline ? ACCEPT : NO_NEWLINE;
                            }

                            break;
                        }
                    } else if (lexer->lookahead == '\n' || lexer->lookahead == 0x2028 || lexer->lookahead == 0x2029) {
                        saw_block_newline = true;
                        skip(lexer);
                    } else {
                        skip(lexer);
                    }
                }
            } else {
                return REJECT;
            }
        } else {
            return ACCEPT;
        }
    }
}

static bool scan_automatic_semicolon(TSLexer *lexer, bool comment_condition, bool *scanned_comment,
                                     bool declaration_continuation, bool value_continuation,
                                     bool preproc_continuation) {
    lexer->result_symbol = AUTOMATIC_SEMICOLON;
    lexer->mark_end(lexer);

    for (;;) {
        if (lexer->lookahead == 0) {
            return true;    // EOF - insert a semicolon
        }

        if (lexer->lookahead == '/') {
            WhitespaceResult result = scan_whitespace_and_comments(lexer, scanned_comment, false);
            if (result == REJECT) {
                return false;
            }

            if (result == ACCEPT && comment_condition && lexer->lookahead != ',' && lexer->lookahead != '=') {
                return true;
            }
        }

        if (lexer->lookahead == '}') {
            return true; // Before closing brace - insert a semicolon
        }

        if (lexer->is_at_included_range_start(lexer)) {
            return true;
        }

        if (lexer->lookahead == '\n' || lexer->lookahead == 0x2028 || lexer->lookahead == 0x2029) {
            break;  // Found a newline - potentially insert a semicolon
        }

        if (!iswspace(lexer->lookahead)) {
            // NLRC also separates statements on one line. Parentheses,
            // brackets and symbolic operators still belong to the expression.
            // A macro argument consumes the rest of its line, even IF, etc.
            if (preproc_continuation) {
                return false;
            }
            if (lexer->lookahead == '{') {
                return !value_continuation;
            }
            if (is_word_start(lexer->lookahead)) {
                WordKind word = scan_word(lexer);
                return word == STATEMENT_KEYWORD ||
                       (word == UNARY_WORD_OPERATOR && !value_continuation) ||
                       (word == DECLARATION_MODIFIER && !declaration_continuation) ||
                       (word == WORD && !declaration_continuation && !value_continuation);
            }
            return false;
        }

        skip(lexer);
    }

    skip(lexer);    // Skip newline

    if (scan_whitespace_and_comments(lexer, scanned_comment, true) == REJECT) {
        return false;
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
            return false;

        // Insert a semicolon before opening parenthesis or bracket when preceded by a closing one
        case '(':
        case '[':
            return true;

        // Insert a semicolon before decimals literals but not otherwise.
        case '.':
            skip(lexer);
            return iswdigit(lexer->lookahead);

        // Insert a semicolon before `--` and `++`, but not before binary `+` or `-`.
        case '+':
            skip(lexer);
            return lexer->lookahead == '+';
        case '-':
            skip(lexer);
            return lexer->lookahead == '-';

        // Don't insert a semicolon before `!=`, but do insert one before a unary `!`.
        case '!':
            skip(lexer);
            return lexer->lookahead != '=';

        default:
            break;
    }

    // By default insert a semicolon
    return true;
}

bool tree_sitter_netlinx_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[AUTOMATIC_SEMICOLON]) {
        #if DEBUG_AUTOMATIC_SEMICOLON
        fprintf(stderr, "Scanner called: AUTOMATIC_SEMICOLON=%d, lookahead=%c\n",
            valid_symbols[AUTOMATIC_SEMICOLON], lexer->lookahead);
        #endif

        bool scanned_comment = false;
        bool ret = scan_automatic_semicolon(lexer, true, &scanned_comment,
                                            valid_symbols[DECLARATION_CONTINUATION],
                                            valid_symbols[VALUE_CONTINUATION],
                                            valid_symbols[PREPROC_CONTINUATION]);

        #if DEBUG_AUTOMATIC_SEMICOLON
        fprintf(stderr, "SCANNER: scan_automatic_semicolon returned %d\n", ret);
        #endif

        return ret;
    }

    return false;
}
