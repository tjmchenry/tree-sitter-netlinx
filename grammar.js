/**
 * @file NetLinx grammar for tree-sitter
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const keywords = require("./keyword-nodes");

const PREC = {
    PAREN_DECLARATOR: -10,
    // Statement-position `=` is ambiguous with the equality binary_expression and is
    // resolved by GLR (see conflicts) plus STATEMENT_ASSIGNMENT below, not by static
    // precedence — so this must TIE with the bare `expression` reduction that starts
    // the binary reading, and stay below every binary operator so the assignment's
    // whole right-hand side still binds into it.
    ASSIGNMENT: 0,
    CONDITIONAL: -1,
    DEFAULT: 0,
    // The ten binary/unary levels below are what NLRC's emitted code does, not what the
    // AMX reference table prints: that table contradicts the compiler on the shift, the
    // bitwise and the `!` rows, so it must never be used to "correct" this order. `<<`
    // and `>>` bind tighter than all arithmetic and sit on separate levels; `& | ^`
    // share one level above arithmetic; `!` binds looser than `& | ^`; unary `-` sits on
    // a level of its own, looser than `* / %` and tighter than binary `+ -`; `~` binds
    // tighter than everything.
    LOGICAL: 1,
    // NetLinx puts `<` `<=` `>` `>=` `=` `==` `<>` on ONE left-to-right level, so
    // `nX = 1 > 5` is `(nX == 1) > 5`. `=` and `==` are the same operator, and
    // rewriting one to the other must never reshape the tree.
    COMPARISON: 2,
    ADD: 3,
    NEGATE: 4,
    MULTIPLY: 5,
    NOT: 6,
    BITWISE: 7,
    LSHIFT: 8,
    RSHIFT: 9,
    UNARY: 10,
    CALL: 15,
    FIELD: 16,
    SUBSCRIPT: 17,
    DIRECTIVE: 20,
    SECTION_DEFINITION: 110,
    // Dynamic, not static: `nX = 7` in statement position is viable both as an
    // assignment and as an equality binary_expression, and only the runtime GLR
    // choice can prefer the assignment without giving `=` a static precedence that
    // would also bind its right operand too tightly.
    STATEMENT_ASSIGNMENT: 1,
};

module.exports = grammar({
    name: "netlinx",

    externals: ($) => [
        $._automatic_semicolon,
        // Scanner context only, never emitted. A type still needs its
        // declarator; RETURN may still take an optional value.
        $._declaration_continuation,
        $._value_continuation,
        $._field_continuation,
        // Never emitted: past a field's keyword type, a word on a later line
        // is its declarator, not a field of its own.
        $._keyword_type_continuation,
        // A reserved word where a field's name goes: a token no rule takes.
        $.reserved_name,
        // Never emitted: where a field begins, and where a field's name goes
        // after a comma.
        $._field_start,
        $._field_name,
        // Event-object tokens are emitted only in expression positions;
        // their spellings use the ordinary identifier token in names/types.
        $.data,
        $.button,
        $.channel,
        $.level,
        $.timeline,
        $.custom,
        // Scanner context only; distinguish user types from event operands.
        $._event_type_context,
        // Comments and strings, read by the compiler's rules. A comment or
        // string its file never closes is a token no rule takes.
        $.comment,
        $.unclosed_comment,
        $.string_part,
        $._string_continuation,
        $.unclosed_string,
        // A #DEFINE's value: the rest of its line up to a comment.
        $.preproc_arg,
        // Empty: a comment follows that no semicolon goes before.
        $._comment_ahead,
        // Never emitted: what error recovery inserts for an arm's missing
        // content.
        $.arm_content,
        // Empty: before an ACTIVE where no label may start. Only a label
        // start takes it.
        $._label_start,
        // Never emitted: an arm stays open past its statement, so error
        // recovery reads a second statement as the arm's and the first as
        // the error, where NLRC reports it.
        $._arm_continuation,
        // A lone `;`, lexed here so that error recovery, which inserts the
        // first token that fits, prefers an arm's missing content to it.
        $.empty_statement,
        // A directive's keyword, then the argument it reads, each a token of
        // its own: an extra, as a comment is.
        $.preproc_include_keyword,
        $.preproc_define_keyword,
        $.preproc_warn_keyword,
        $.preproc_disable_warning_keyword,
        $.preproc_if_defined_keyword,
        $.preproc_if_not_defined_keyword,
        $.preproc_else_keyword,
        $.preproc_end_if_keyword,
        $.preproc_name,
        $.preproc_string,
        $.preproc_unclosed_string,
        $.preproc_number,
        // Valid only while the parser recovers from an error.
        $._error_sentinel,
    ],

    conflicts: ($) => [
        [$.assignment_expression, $.expression],
        [$.type_specifier, $.expression],
        [$.string_expression],
        [$.type_specifier, $._top_level_expression_statement],
        [$.device_literal],
        [$.button_event_devchan_reference, $.devchan_expression],
        [$.channel_event_devchan_reference, $.devchan_expression],
        [$.push_event_devchan_reference, $.devchan_expression],
        [$.release_event_devchan_reference, $.devchan_expression],
        [$.wait_statement, $.expression],
        [$.wait_until_statement, $.expression],
        [$.timed_wait_until_statement, $.expression],
        [$.subscript_expression, $.devchan_expression],
        [$.argument_list, $.parenthesized_expression],
        [$.comma_expression, $.argument_list],
    ],

    extras: ($) => [
        /\s/,
        $.comment,
        $._comment_ahead,
        $.preproc_include_keyword,
        $.preproc_define_keyword,
        $.preproc_warn_keyword,
        $.preproc_disable_warning_keyword,
        $.preproc_if_defined_keyword,
        $.preproc_if_not_defined_keyword,
        $.preproc_else_keyword,
        $.preproc_end_if_keyword,
        $.preproc_name,
        $.preproc_arg,
        $.preproc_string,
        $.preproc_unclosed_string,
        $.preproc_number,
    ],

    inline: ($) => [
        $._type_identifier,
        $._field_identifier,
        $._statement_identifier,
        $._non_case_statement,
        $._wait_body,
        $._assignment_left_expression,
        $._expression_not_binary,
        $._expression_not_binary_or_number,
    ],

    supertypes: ($) => [
        $.expression,
        $.statement,
        $.type_specifier,
        $._declarator,
        $._field_declarator,
        $._type_declarator,
        $._abstract_declarator,
    ],

    word: ($) => $.identifier,

    rules: {
        source_file: ($) => repeat($._top_level_item),

        _top_level_item: ($) =>
            choice(
                // Sections (each wraps its keyword + body)
                $.section,

                // Headers
                $.program_name,
                $.module_name,

                // Items that may appear outside any section
                $._section_body_item,
            ),

        // Items that can appear inside a DEFINE_* section body.
        // Kept as a hidden rule so it doesn't create extra nodes.
        _section_body_item: ($) =>
            choice(
                // Declarations
                $.declaration,
                $.type_definition,
                $.event_definition,

                // Statements
                $._top_level_statement,
            ),

        _block_item: ($) =>
            choice(
                // Definitions
                $.define_function,
                $.type_definition,
                $.define_call,
                $.define_module,

                // Block-level declarations
                $.declaration,

                // Statements
                $.case_statement,
                $.statement,
            ),

        // Include all keyword nodes
        ...keywords,

        program_name: ($) =>
            prec.right(
                PREC.DIRECTIVE + 10,
                seq($.program_name_keyword, "=", $.string_literal),
            ),

        module_name: ($) =>
            prec.right(
                seq(
                    $.module_name_keyword,
                    "=",
                    $.string_literal,
                    optional(field("parameters", $.parameter_list)),
                ),
            ),

        // Main Grammar

        section: ($) =>
            choice(
                $.define_device_section,
                $.define_combine_section,
                $.define_connect_level_section,
                $.define_constant_section,
                $.define_type_section,
                $.define_mutually_exclusive_section,
                $.define_latching_section,
                $.define_toggling_section,
                $.define_variable_section,
                $.define_system_variable_section,
                $.define_start_section,
                $.define_event_section,
                $.define_program_section,
                $.define_function_section,
                $.define_call_section,
                $.define_module_section,
            ),

        define_device_section: ($) =>
            prec.right(
                seq($.define_device_keyword, repeat($._section_body_item)),
            ),
        define_combine_section: ($) =>
            prec.right(
                seq($.define_combine_keyword, repeat($._section_body_item)),
            ),
        define_connect_level_section: ($) =>
            prec.right(
                seq(
                    $.define_connect_level_keyword,
                    repeat($._section_body_item),
                ),
            ),
        define_constant_section: ($) =>
            prec.right(
                seq(
                    $.define_constant_keyword,
                    repeat(choice($._section_body_item, $.empty_statement)),
                ),
            ),
        define_type_section: ($) =>
            prec.right(
                seq($.define_type_keyword, repeat($._section_body_item)),
            ),
        define_mutually_exclusive_section: ($) =>
            prec.right(
                seq(
                    $.define_mutually_exclusive_keyword,
                    repeat($._section_body_item),
                ),
            ),
        define_latching_section: ($) =>
            prec.right(
                seq($.define_latching_keyword, repeat($._section_body_item)),
            ),
        define_toggling_section: ($) =>
            prec.right(
                seq($.define_toggling_keyword, repeat($._section_body_item)),
            ),
        define_variable_section: ($) =>
            prec.right(
                seq(
                    $.define_variable_keyword,
                    repeat(choice($._section_body_item, $.empty_statement)),
                ),
            ),
        define_system_variable_section: ($) =>
            prec.right(
                seq(
                    $.define_system_variable_keyword,
                    repeat($._section_body_item),
                ),
            ),
        define_event_section: ($) =>
            prec.right(
                seq($.define_event_keyword, repeat($._section_body_item)),
            ),
        define_start_section: ($) =>
            prec.right(
                seq(
                    $.define_start_keyword,
                    repeat(choice($._section_body_item, $.empty_statement)),
                ),
            ),
        define_program_section: ($) =>
            prec.right(
                seq(
                    $.define_program_keyword,
                    repeat(choice($._section_body_item, $.empty_statement)),
                ),
            ),

        define_function_section: ($) => prec.right($.define_function),

        define_call_section: ($) => prec.right($.define_call),

        define_module_section: ($) => prec.right($.define_module),

        define_function: ($) =>
            choice(
                seq($.define_function_keyword, $.function_definition),
                seq($.define_library_function_keyword, $.function_declaration),
            ),

        define_call: ($) => seq($.define_call_keyword, $.call_definition),
        define_module: ($) => seq($.define_module_keyword, $.module_definition),

        module_definition: ($) =>
            seq(
                field("module_name", $.string_literal),
                field("instance_name", $.identifier),
                field("parameters", $.argument_list),
                $._semicolon,
            ),

        event_definition: ($) =>
            choice(
                $.button_event_definition,
                $.channel_event_definition,
                $.level_event_definition,
                $.data_event_definition,
                $.timeline_event_definition,
                $.custom_event_definition,
                $.push_event_definition,
                $.release_event_definition,
            ),

        data_event_definition: ($) =>
            seq(
                repeat1($.data_event_declarator),
                field("body", $.data_event_block),
            ),

        data_event_declarator: ($) =>
            seq(
                $.data_event_keyword,
                field("device", $.data_event_device_reference),
            ),

        data_event_device_reference: ($) => seq("[", $.expression, "]"),

        data_event_block: ($) => seq("{", repeat($.data_event_handler), "}"),

        data_event_handler: ($) =>
            seq(
                field("type", $.data_event_type),
                ":",
                field("body", $.statement),
            ),

        data_event_type: ($) =>
            choice(
                $.command_keyword,
                $.string_keyword,
                $.online_keyword,
                $.offline_keyword,
                $.onerror_keyword,
                $.standby_keyword,
                $.awake_keyword,
            ),

        timeline_event_definition: ($) =>
            seq(
                repeat1($.timeline_event_declarator),
                field("body", $.statement),
            ),

        timeline_event_declarator: ($) =>
            seq(
                $.timeline_event_keyword,
                field("id", $.timeline_event_id_reference),
            ),

        timeline_event_id_reference: ($) => seq("[", $.expression, "]"),

        button_event_definition: ($) =>
            seq(
                repeat1($.button_event_declarator),
                field("body", $.button_event_block),
            ),

        button_event_declarator: ($) =>
            seq(
                $.button_event_keyword,
                field("devchan", $.button_event_devchan_reference),
            ),

        button_event_devchan_reference: ($) =>
            choice($.devchan_expression, seq("[", $.expression, "]")),

        button_event_block: ($) =>
            seq("{", repeat($.button_event_handler), "}"),

        button_event_handler: ($) =>
            seq(
                field("type", $.button_event_type),
                ":",
                field("body", $.statement),
            ),

        button_event_type: ($) =>
            choice(
                $.push_keyword,
                $.release_keyword,
                choice(
                    // HOLD[time[, repeat]]
                    seq(
                        $.hold_keyword,
                        seq(
                            "[",
                            field("time", $.expression),
                            optional(seq(",", $.button_event_hold_repeat)),
                            "]",
                        ),
                    ),
                ),
            ),

        button_event_hold_repeat: ($) => $.repeat_keyword,

        level_event_definition: ($) =>
            seq(repeat1($.level_event_declarator), field("body", $.statement)),

        level_event_declarator: ($) =>
            seq(
                $.level_event_keyword,
                field("devlev", $.level_event_devlev_reference),
            ),

        level_event_devlev_reference: ($) =>
            choice($.devlev_expression, seq("[", $.expression, "]")),

        channel_event_definition: ($) =>
            seq(
                repeat1($.channel_event_declarator),
                field("body", $.channel_event_block),
            ),

        channel_event_declarator: ($) =>
            seq(
                $.channel_event_keyword,
                field("devchan", $.channel_event_devchan_reference),
            ),

        channel_event_devchan_reference: ($) =>
            choice($.devchan_expression, seq("[", $.expression, "]")),

        channel_event_block: ($) =>
            seq("{", repeat($.channel_event_handler), "}"),

        channel_event_handler: ($) =>
            seq(
                field("type", $.channel_event_type),
                ":",
                field("body", $.statement),
            ),

        channel_event_type: ($) => choice($.on_keyword, $.off_keyword),

        custom_event_definition: ($) =>
            seq(repeat1($.custom_event_declarator), field("body", $.statement)),

        custom_event_declarator: ($) =>
            seq(
                $.custom_event_keyword,
                field("reference", $.custom_event_reference),
            ),

        custom_event_reference: ($) =>
            seq(
                "[",
                choice(
                    // Format 1: [DEVICE,ID,TYPE]
                    seq(
                        field("device", $.expression),
                        ",",
                        field("id", $.expression),
                        ",",
                        field("type", $.expression),
                    ),

                    // Format 2: [DEVCHAN,EVENTID]
                    seq(
                        field("devchan", $.expression),
                        ",",
                        field("eventid", $.expression),
                    ),
                ),
                "]",
            ),

        push_event_definition: ($) =>
            seq(
                repeat1($.push_event_declarator),
                field("body", $.compound_statement),
            ),

        push_event_declarator: ($) =>
            seq(
                $.push_keyword,
                field("devchan", $.push_event_devchan_reference),
            ),

        push_event_devchan_reference: ($) =>
            choice($.devchan_expression, seq("[", $.expression, "]")),

        release_event_definition: ($) =>
            seq(
                repeat1($.release_event_declarator),
                field("body", $.compound_statement),
            ),

        release_event_declarator: ($) =>
            seq(
                $.release_keyword,
                field("devchan", $.release_event_devchan_reference),
            ),

        release_event_devchan_reference: ($) =>
            choice($.devchan_expression, seq("[", $.expression, "]")),

        array_declarator: ($) =>
            prec(
                1,
                seq(
                    field("declarator", $._declarator),
                    "[",
                    field("size", optional($.expression)),
                    "]",
                ),
            ),

        array_field_declarator: ($) =>
            prec(
                1,
                seq(
                    field("declarator", $._field_declarator),
                    "[",
                    field("size", optional($.expression)),
                    "]",
                ),
            ),

        array_type_declarator: ($) =>
            prec(
                1,
                seq(
                    field("declarator", $._type_declarator),
                    "[",
                    field("size", optional($.expression)),
                    "]",
                ),
            ),

        abstract_array_declarator: ($) =>
            prec(
                1,
                seq(
                    field("declarator", optional($._abstract_declarator)),
                    "[",
                    field("size", optional($.expression)),
                    "]",
                ),
            ),

        init_declarator: ($) =>
            seq(
                field("declarator", $._declarator),
                "=",
                field("value", choice($.initializer_list, $.expression)),
            ),

        compound_statement: ($) => seq("{", repeat($._block_item), "}"),

        storage_class_specifier: ($) =>
            choice($.local_var_keyword, $.stack_var_keyword),

        type_qualifier: ($) =>
            choice(
                $.constant_keyword,
                $.volatile_keyword,
                $.non_volatile_keyword,
                $.persistent_keyword,
            ),

        type_specifier: ($) => choice($.intrinsic_type, $._type_identifier),

        struct_specifier: ($) =>
            prec.right(
                seq(
                    choice($.struct_keyword, $.structure_keyword),
                    choice(
                        seq(
                            field("name", $._type_identifier),
                            field("body", $.field_declaration_list),
                        ),
                    ),
                ),
            ),

        field_declaration_list: ($) =>
            seq(
                "{",
                repeat1(choice($.field_declaration, $.empty_statement)),
                "}",
            ),

        // A keyword type's field ends without a declarator only at a semicolon
        // the scanner places before a line that cannot continue it.
        field_declaration: ($) =>
            prec.right(
                choice(
                    seq(
                        repeat($._declaration_modifiers),
                        optional($._field_start),
                        field("type", $.intrinsic_type),
                        choice(
                            seq(
                                optional($._keyword_type_continuation),
                                $._field_declaration_declarator,
                                $._semicolon,
                            ),
                            $._automatic_semicolon,
                        ),
                    ),
                    seq(
                        repeat($._declaration_modifiers),
                        optional($._field_start),
                        optional($._event_type_context),
                        field("type", $._type_identifier),
                        optional($._field_continuation),
                        optional($._field_declaration_declarator),
                        $._semicolon,
                    ),
                ),
            ),

        _field_declaration_declarator: ($) =>
            seq(
                field("declarator", $._field_declarator),
                repeat(
                    seq(
                        ",",
                        optional($._field_name),
                        field("declarator", $._field_declarator),
                    ),
                ),
            ),

        intrinsic_type: ($) => choice($.primitive_type, $.structured_type),

        primitive_type: ($) =>
            choice(
                $.char_keyword,
                $.widechar_keyword,
                $.integer_keyword,
                $.sinteger_keyword,
                $.long_keyword,
                $.slong_keyword,
                $.float_keyword,
                $.double_keyword,
                $.variant_keyword,
                $.variantarray_keyword,
            ),

        structured_type: ($) =>
            choice($.dev_keyword, $.devlev_keyword, $.devchan_keyword),

        array_return_type: ($) =>
            seq(
                $.type_specifier,
                "[",
                optional(field("size", $.expression)),
                "]",
            ),

        function_definition: ($) =>
            seq(
                optional(
                    field(
                        "return_type",
                        choice($.type_specifier, $.array_return_type),
                    ),
                ),
                field("name", $.identifier),
                field("parameters", $.parameter_list),

                // Optional local declarations
                // Legacy/Weird syntax where local declarations
                // are defined before the body of the function
                field("local_declarations", repeat($.declaration)),

                // Function body { ... }
                field("body", $.compound_statement),
            ),

        function_declaration: ($) =>
            seq(
                optional(
                    field(
                        "return_type",
                        choice($.type_specifier, $.array_return_type),
                    ),
                ),
                field("name", $.identifier),
                field("parameters", $.parameter_list),
                optional(seq("=", $.expression)),
                $._semicolon,
            ),

        call_definition: ($) =>
            seq(
                field("name", $.string_literal),
                optional(field("parameters", $.parameter_list)),

                // Optional local declarations
                // Legacy/Weird syntax where local declarations
                // are defined before the body of the function
                field("local_declarations", repeat($.declaration)),

                // Call body { ... }
                field("body", $.compound_statement),
            ),

        declaration: ($) =>
            prec.right(
                seq(
                    choice(
                        // Regular declaration with custom type
                        prec.right(
                            10,
                            seq(
                                $._declaration_specifiers,
                                field("type", $._type_identifier),
                                optional($._declaration_continuation),
                                commaSep1(
                                    field(
                                        "declarator",
                                        choice(
                                            seq($._declaration_declarator),
                                            $.init_declarator,
                                        ),
                                    ),
                                ),
                            ),
                        ),

                        // Custom type with qualifier only
                        prec.right(
                            10,
                            seq(
                                $.type_qualifier,
                                field("type", $._type_identifier),
                                optional($._declaration_continuation),
                                commaSep1(
                                    field(
                                        "declarator",
                                        choice(
                                            seq($._declaration_declarator),
                                            $.init_declarator,
                                        ),
                                    ),
                                ),
                            ),
                        ),

                        // Custom type with storage class only
                        prec.right(
                            10,
                            seq(
                                $.storage_class_specifier,
                                field("type", $._type_identifier),
                                optional($._declaration_continuation),
                                commaSep1(
                                    field(
                                        "declarator",
                                        choice(
                                            seq($._declaration_declarator),
                                            $.init_declarator,
                                        ),
                                    ),
                                ),
                            ),
                        ),

                        // Regular declaration
                        prec.right(
                            1,
                            seq(
                                $._declaration_specifiers,
                                commaSep1(
                                    field(
                                        "declarator",
                                        choice(
                                            seq($._declaration_declarator),
                                            $.init_declarator,
                                        ),
                                    ),
                                ),
                            ),
                        ),

                        // Qualifier only
                        prec.right(
                            2,
                            seq(
                                $.type_qualifier,
                                commaSep1(
                                    field(
                                        "declarator",
                                        choice(
                                            seq($._declaration_declarator),
                                            $.init_declarator,
                                        ),
                                    ),
                                ),
                            ),
                        ),

                        // Storage class only
                        prec.right(
                            3,
                            seq(
                                $.storage_class_specifier,
                                commaSep1(
                                    field(
                                        "declarator",
                                        choice(
                                            seq($._declaration_declarator),
                                            $.init_declarator,
                                        ),
                                    ),
                                ),
                            ),
                        ),
                    ),
                    $._semicolon,
                ),
            ),

        type_definition: ($) =>
            prec.right(seq($.struct_specifier, $._semicolon)),

        _declaration_modifiers: ($) =>
            choice($.storage_class_specifier, $.type_qualifier),

        _declaration_specifiers: ($) =>
            prec.right(
                seq(
                    repeat($._declaration_modifiers),
                    // Never emitted: expose type/operand ambiguity to the scanner.
                    optional($._event_type_context),
                    field("type", $.type_specifier),
                    repeat($._declaration_modifiers),
                    optional($._declaration_continuation),
                ),
            ),

        _declarator: ($) =>
            choice(
                $.function_declarator,
                $.array_declarator,
                $.parenthesized_declarator,
                $.identifier,
            ),

        _declaration_declarator: ($) =>
            choice(
                alias(
                    $._function_declaration_declarator,
                    $.function_declarator,
                ),
                $.array_declarator,
                $.parenthesized_declarator,
                $.identifier,
            ),

        _field_declarator: ($) =>
            choice(
                alias($.function_field_declarator, $.function_declarator),
                alias($.array_field_declarator, $.array_declarator),
                alias(
                    $.parenthesized_field_declarator,
                    $.parenthesized_declarator,
                ),
                $._field_identifier,
            ),

        _type_declarator: ($) =>
            choice(
                alias($.function_type_declarator, $.function_declarator),
                alias($.array_type_declarator, $.array_declarator),
                alias(
                    $.parenthesized_type_declarator,
                    $.parenthesized_declarator,
                ),
                $._type_identifier,
                $.intrinsic_type,
            ),

        _abstract_declarator: ($) => choice($.abstract_array_declarator),

        function_declarator: ($) =>
            prec.right(
                1,
                seq(
                    field("declarator", $._declarator),
                    field("parameters", $.parameter_list),
                ),
            ),

        _function_declaration_declarator: ($) =>
            prec.right(
                1,
                seq(
                    field("declarator", $._declarator),
                    field("parameters", $.parameter_list),
                ),
            ),

        parenthesized_declarator: ($) =>
            prec.dynamic(PREC.PAREN_DECLARATOR, seq("(", $._declarator, ")")),

        parenthesized_field_declarator: ($) =>
            prec.dynamic(
                PREC.PAREN_DECLARATOR,
                seq("(", optional($._field_declarator), ")"),
            ),

        parenthesized_type_declarator: ($) =>
            prec.dynamic(
                PREC.PAREN_DECLARATOR,
                seq("(", optional($._type_declarator), ")"),
            ),

        function_field_declarator: ($) =>
            prec(
                1,
                seq(
                    field("declarator", $._field_declarator),
                    field("parameters", $.parameter_list),
                ),
            ),

        function_type_declarator: ($) =>
            prec(
                1,
                seq(
                    field("declarator", $._type_declarator),
                    field("parameters", $.parameter_list),
                ),
            ),

        parameter_list: ($) =>
            prec.left(
                PREC.CALL + 1,
                seq(
                    "(",
                    choice(
                        commaSep(choice($.parameter_declaration)),
                        $.compound_statement,
                    ),
                    ")",
                ),
            ),

        parameter_declaration: ($) =>
            seq(
                $._declaration_specifiers,
                optional(
                    field(
                        "declarator",
                        choice($._declarator, $._abstract_declarator),
                    ),
                ),
            ),

        /**
         * Statements
         */

        statement: ($) => $._non_case_statement,

        _non_case_statement: ($) =>
            choice(
                $.empty_statement,
                $.compound_statement,
                $.expression_statement,
                $.if_statement,
                $.switch_statement,
                $.select_statement,
                $.while_statement,
                $.for_statement,
                $.return_statement,
                $.break_statement,
                $.continue_statement,
                $.devchan_operation_statement,
                $.send_string_statement,
                $.send_command_statement,
                $.send_level_statement,
                $.create_buffer_statement,
                $.create_multi_buffer_statement,
                $.clear_buffer_statement,
                $.wait_statement,
                $.wait_until_statement,
                $.cancel_all_wait_statement,
                $.cancel_all_wait_until_statement,
                $.cancel_wait_statement,
                $.cancel_wait_until_statement,
                $.timed_wait_until_statement,
                $.pause_wait_statement,
                $.pause_all_wait_statement,
                $.restart_wait_statement,
                $.restart_all_wait_statement,
                $.create_level_statement,
                $.call_statement,
                $.system_call_statement,
            ),

        _top_level_statement: ($) =>
            choice(
                $.case_statement,
                $.compound_statement,
                $.expression_statement,
                $.if_statement,
                $.switch_statement,
                $.select_statement,
                $.while_statement,
                $.for_statement,
                $.return_statement,
                $.break_statement,
                $.continue_statement,
                $.devchan_operation_statement,
                $.send_string_statement,
                $.send_command_statement,
                $.send_level_statement,
                $.create_buffer_statement,
                $.create_multi_buffer_statement,
                $.clear_buffer_statement,
                $.wait_statement,
                $.wait_until_statement,
                $.cancel_all_wait_statement,
                $.cancel_all_wait_until_statement,
                $.cancel_wait_statement,
                $.cancel_wait_until_statement,
                $.timed_wait_until_statement,
                $.pause_wait_statement,
                $.pause_all_wait_statement,
                $.restart_wait_statement,
                $.restart_all_wait_statement,
                $.create_level_statement,
                $.call_statement,
                $.system_call_statement,
            ),

        _top_level_expression_statement: ($) =>
            seq($._expression_not_binary, $._semicolon),

        expression_statement: ($) =>
            choice(seq($._statement_expression, $._semicolon)),

        if_statement: ($) =>
            prec.right(
                seq(
                    $.if_keyword,
                    field("condition", $.parenthesized_expression),
                    field("consequence", $.statement),
                    optional(field("alternative", $.else_clause)),
                ),
            ),

        else_clause: ($) => seq($.else_keyword, $.statement),

        switch_statement: ($) =>
            seq(
                $.switch_keyword,
                field("condition", $.parenthesized_expression),
                field("body", $.compound_statement),
            ),

        case_statement: ($) =>
            prec.right(
                seq(
                    choice(
                        seq($.case_keyword, field("value", $.expression)),
                        $.default_keyword,
                    ),
                    ":",
                    repeat(choice($._non_case_statement, $.declaration)),
                ),
            ),

        while_statement: ($) =>
            seq(
                choice(
                    $.while_keyword,
                    $.long_while_keyword,
                    $.medium_while_keyword,
                ),
                field("condition", $.parenthesized_expression),
                field("body", $.statement),
            ),

        for_statement: ($) =>
            seq(
                $.for_keyword,
                "(",
                $._for_statement_body,
                ")",
                field("body", $.statement),
            ),

        _for_statement_body: ($) =>
            seq(
                choice(
                    seq(
                        field("initializer", optional($._statement_expression)),
                        ";",
                    ),
                ),
                field(
                    "condition",
                    optional(choice($.expression, $.comma_expression)),
                ),
                ";",
                field("update", optional($._statement_expression)),
            ),

        select_statement: ($) =>
            seq($.select_keyword, field("body", $.select_block)),

        select_block: ($) => seq("{", repeat($.active_statement), "}"),

        // An arm holds one statement or a run of declarations.
        active_statement: ($) =>
            seq(
                optional($._label_start),
                $.active_keyword,
                field("condition", $.parenthesized_expression),
                ":",
                choice(
                    seq(
                        field("body", $.statement),
                        optional($._arm_continuation),
                    ),
                    repeat1($.declaration),
                    $.arm_content,
                ),
            ),

        return_statement: ($) =>
            seq(
                $.return_keyword,
                optional($._value_continuation),
                optional(
                    choice(
                        $.expression,
                        $.comma_expression,
                        $.initializer_list,
                    ),
                ),
                $._semicolon,
            ),

        break_statement: ($) => seq($.break_keyword, $._semicolon),

        continue_statement: ($) => seq($.continue_keyword, $._semicolon),

        devchan_operation_statement: ($) =>
            prec.right(
                PREC.FIELD + 20,
                seq(
                    field("operation", $.devchan_operation),
                    field(
                        "target",
                        choice(
                            $.devchan_expression,
                            $.devchan_range_expression,
                        ),
                    ),
                    $._semicolon,
                ),
            ),

        devchan_operation: ($) =>
            choice(
                $.devchan_on_keyword,
                $.devchan_off_keyword,
                $.devchan_to_keyword,
                $.devchan_min_to_keyword,
                $.devchan_total_off_keyword,
                $.devchan_pulse_keyword,
            ),

        send_string_statement: ($) =>
            seq(
                $.send_string_keyword,
                field("device", $.expression),
                ",",
                field("value", $.expression),
                $._semicolon,
            ),

        send_command_statement: ($) =>
            seq(
                $.send_command_keyword,
                field("device", $.expression),
                ",",
                field("value", $.expression),
                $._semicolon,
            ),

        send_level_statement: ($) =>
            choice(
                seq(
                    $.send_level_keyword,
                    field("device", $.expression),
                    ",",
                    field("level", $.expression),
                    ",",
                    field("value", $.expression),
                    $._semicolon,
                ),
                seq(
                    $.send_level_keyword,
                    field("devlev", $.expression),
                    ",",
                    field("value", $.expression),
                    $._semicolon,
                ),
            ),

        create_buffer_statement: ($) =>
            seq($.create_buffer_keyword, $.comma_expression, $._semicolon),

        create_multi_buffer_statement: ($) =>
            seq(
                $.create_multi_buffer_keyword,
                $.comma_expression,
                $._semicolon,
            ),

        clear_buffer_statement: ($) =>
            seq($.clear_buffer_keyword, $.expression, $._semicolon),

        // Private rule used to build expression_statement-shaped nodes for WAIT bare bodies.
        // Aliasing this dedicated rule (rather than an inline seq) to expression_statement
        // keeps the inner call/assignment/update as a proper named child of the wrapper
        // and avoids the trailing `;` leaking up as a second body node.
        _wait_bare_expression_statement: ($) =>
            seq(
                choice(
                    $.call_expression,
                    $.assignment_expression,
                    $.update_expression,
                ),
                $._semicolon,
            ),

        // Allowlist of legal WAIT body forms.
        //
        // A leading string_literal is always the wait NAME (e.g. WAIT 10 'myname' ...),
        // so string_literal is deliberately excluded here.
        //
        // The bare-expression alternatives are intentionally narrow: only call_expression,
        // assignment_expression, and update_expression are legal as standalone WAIT bodies
        // in NLRC (confirmed by compilation probes). Broader expression kinds
        // (subscript_expression, devchan_expression, parenthesized_expression, identifier,
        // number_literal, unary_expression, field_expression) produce C10201 syntax errors
        // in NLRC and including them forces over-broad global conflict entries.
        _wait_body: ($) =>
            choice(
                $.empty_statement,
                $.compound_statement,
                $.if_statement,
                $.switch_statement,
                $.select_statement,
                $.while_statement,
                $.for_statement,
                $.return_statement,
                $.break_statement,
                $.continue_statement,
                $.devchan_operation_statement,
                $.send_string_statement,
                $.send_command_statement,
                $.send_level_statement,
                $.create_buffer_statement,
                $.create_multi_buffer_statement,
                $.clear_buffer_statement,
                $.wait_statement,
                $.wait_until_statement,
                $.cancel_all_wait_statement,
                $.cancel_all_wait_until_statement,
                $.cancel_wait_statement,
                $.cancel_wait_until_statement,
                $.timed_wait_until_statement,
                $.pause_wait_statement,
                $.pause_all_wait_statement,
                $.restart_wait_statement,
                $.restart_all_wait_statement,
                $.create_level_statement,
                $.call_statement,
                $.system_call_statement,
                // Bare-expression bodies: only the NLRC-legal subset (calls, assignments, updates).
                // Wrapped in expression_statement for a uniform AST (matches every other body form).
                // Using a dedicated private rule aliased to expression_statement so the
                // call/assignment/update node stays as a named child inside the wrapper.
                alias(
                    $._wait_bare_expression_statement,
                    $.expression_statement,
                ),
            ),

        wait_statement: ($) =>
            prec.right(
                seq(
                    $.wait_keyword,
                    field("time", $.expression),
                    optional(field("name", $.string_literal)),
                    optional(field("body", $._wait_body)),
                ),
            ),

        wait_until_statement: ($) =>
            prec.right(
                seq(
                    $.wait_until_keyword,
                    field("condition", $.expression),
                    optional(field("name", $.string_literal)),
                    optional(field("body", $._wait_body)),
                ),
            ),

        cancel_all_wait_statement: ($) =>
            seq($.cancel_all_wait_keyword, $._semicolon),
        cancel_all_wait_until_statement: ($) =>
            seq($.cancel_all_wait_until_keyword, $._semicolon),

        cancel_wait_statement: ($) =>
            seq(
                $.cancel_wait_keyword,
                field("name", $.string_literal),
                $._semicolon,
            ),

        cancel_wait_until_statement: ($) =>
            seq(
                $.cancel_wait_until_keyword,
                field("name", $.string_literal),
                $._semicolon,
            ),

        timed_wait_until_statement: ($) =>
            prec.right(
                seq(
                    $.timed_wait_until_keyword,
                    field("condition", $.parenthesized_expression),
                    field("timeout", $.expression),
                    optional(field("name", $.string_literal)),
                    optional(field("body", $._wait_body)),
                ),
            ),

        pause_wait_statement: ($) =>
            seq(
                $.pause_wait_keyword,
                field("name", $.string_literal),
                $._semicolon,
            ),

        pause_all_wait_statement: ($) =>
            seq($.pause_all_wait_keyword, $._semicolon),

        restart_wait_statement: ($) =>
            seq(
                $.restart_wait_keyword,
                field("name", $.string_literal),
                $._semicolon,
            ),

        restart_all_wait_statement: ($) =>
            seq($.restart_all_wait_keyword, $._semicolon),

        create_level_statement: ($) =>
            seq($.create_level_keyword, $.comma_expression, $._semicolon),

        // This is for invoking NetLinx's legacy DEFINE_CALL function.
        // These cannot be used in or as expressions.
        // Think of them like simple macros.
        // They do not support return values.
        call_statement: ($) =>
            seq(
                $.call_keyword,
                field("call", $.string_literal),
                optional(field("arguments", $.argument_list)),
                $._semicolon,
            ),

        system_call_statement: ($) =>
            seq(
                $.system_call_keyword,
                optional(seq("[", field("instance", $.expression), "]")),
                field("call", $.string_literal),
                optional(field("arguments", $.argument_list)),
                $._semicolon,
            ),

        /**
         * Expressions
         */

        expression: ($) =>
            choice($._expression_not_binary, $.binary_expression),

        _expression_not_binary: ($) =>
            choice($.number_literal, $._expression_not_binary_or_number),

        _expression_not_binary_or_number: ($) =>
            choice(
                $.unary_expression,
                $.update_expression,
                $.call_expression,
                $.field_expression,
                $.subscript_expression,
                $.string_expression,
                $.identifier,
                $.string_literal,
                $.compiler_variable,
                $.system_variable,
                $.device_literal,
                $.parenthesized_expression,
                $.devchan_range_expression,
                $.devchan_expression,
            ),

        compiler_variable: ($) =>
            choice(
                $.__file__,
                $.__date__,
                $.__ldate__,
                $.__time__,
                $.__line__,
                $.__name__,
            ),

        system_variable: ($) =>
            choice($.button, $.data, $.level, $.timeline, $.channel, $.custom),

        assignment_expression: ($) =>
            prec.dynamic(
                PREC.STATEMENT_ASSIGNMENT,
                prec.right(
                    PREC.ASSIGNMENT,
                    seq(
                        field("left", $._assignment_left_expression),
                        field("operator", "="),
                        field(
                            "right",
                            choice($.initializer_list, $.expression),
                        ),
                    ),
                ),
            ),

        _assignment_left_expression: ($) =>
            choice(
                $.identifier,
                $.call_expression,
                $.field_expression,
                $.subscript_expression,
                $.parenthesized_expression,
                $.devchan_expression,
                alias($.system_variable, $.identifier),
            ),

        // Three levels, not one: NLRC gives `~` the tightest binding of any operator,
        // `!` binds LOOSER than `& | ^` but tighter than `*`, and unary `-` binds looser
        // than `*` again — so `~nA & nB` is `(~nA) & nB`, `!nA & nB` is `!(nA & nB)` and
        // `-nA * nB` is `-(nA * nB)`. There is no unary `+` in the language: NLRC answers
        // C10201 to `nX = +nA`, so admitting it here would parse what cannot compile.
        unary_expression: ($) =>
            choice(
                prec.right(
                    PREC.UNARY,
                    seq(
                        field("operator", choice("~", $.bnot)),
                        field("argument", $.expression),
                    ),
                ),
                prec.right(
                    PREC.NOT,
                    seq(
                        field("operator", choice("!", $.not)),
                        field("argument", $.expression),
                    ),
                ),
                prec.right(
                    PREC.NEGATE,
                    seq(
                        field("operator", "-"),
                        field("argument", $.expression),
                    ),
                ),
            ),

        binary_expression: ($) => {
            const table = [
                ["+", PREC.ADD],
                ["-", PREC.ADD],
                ["*", PREC.MULTIPLY],
                ["/", PREC.MULTIPLY],
                ["%", PREC.MULTIPLY],
                [$.mod, PREC.MULTIPLY],
                ["||", PREC.LOGICAL],
                ["&&", PREC.LOGICAL],
                ["|", PREC.BITWISE],
                ["^", PREC.BITWISE],
                ["&", PREC.BITWISE],
                ["=", PREC.COMPARISON],
                ["==", PREC.COMPARISON],
                ["!=", PREC.COMPARISON],
                ["<>", PREC.COMPARISON],
                [">", PREC.COMPARISON],
                [">=", PREC.COMPARISON],
                ["<=", PREC.COMPARISON],
                ["<", PREC.COMPARISON],
                ["<<", PREC.LSHIFT],
                [">>", PREC.RSHIFT],
                [$.band, PREC.BITWISE],
                [$.bor, PREC.BITWISE],
                [$.bxor, PREC.BITWISE],
                [$.lshift, PREC.LSHIFT],
                [$.rshift, PREC.RSHIFT],
                [$.and, PREC.LOGICAL],
                [$.or, PREC.LOGICAL],
                [$.xor, PREC.LOGICAL],
                ["^^", PREC.LOGICAL],
            ];

            return choice(
                ...table.map(([operator, precedence]) => {
                    return prec.left(
                        // @ts-ignore
                        precedence,
                        seq(
                            field("left", $.expression),
                            // @ts-ignore
                            field("operator", operator),
                            field("right", $.expression),
                        ),
                    );
                }),
            );
        },

        update_expression: ($) => {
            const argument = field("argument", $.expression);
            const operator = field("operator", choice("--", "++"));
            return prec.right(
                PREC.UNARY,
                choice(
                    seq(argument, operator), // Post-increment/decrement (valid in NetLinx)
                    seq(operator, argument), // Pre-increment/decrement (parser support only)
                ),
            );
        },

        subscript_expression: ($) =>
            prec(
                PREC.SUBSCRIPT,
                seq(
                    field("argument", $.expression),
                    repeat1(
                        seq("[", field("index", optional($.expression)), "]"),
                    ),
                ),
            ),

        call_expression: ($) =>
            prec(
                PREC.CALL,
                seq(
                    field("function", $.expression),
                    field("arguments", $.argument_list),
                ),
            ),

        comma_expression: ($) =>
            prec.right(
                PREC.DEFAULT,
                seq(
                    field("left", $.expression),
                    ",",
                    field("right", choice($.expression, $.comma_expression)),
                ),
            ),

        // `=` is an assignment only where a statement is expected; wherever a value is
        // expected it is the equality operator, and `expression` cannot reach
        // assignment_expression at all. An expression-position rule (a condition, an
        // argument, a parenthesized expression) must never reach an assignment.
        // COUPLING: _wait_bare_expression_statement admits assignments without going
        // through here, deliberately — it is a narrower allowlist, not this rule.
        _statement_expression: ($) =>
            choice(
                $.expression,
                $.assignment_expression,
                alias($._statement_comma_expression, $.comma_expression),
            ),

        _statement_comma_expression: ($) =>
            prec.right(
                PREC.DEFAULT,
                seq(
                    field(
                        "left",
                        choice($.expression, $.assignment_expression),
                    ),
                    ",",
                    field(
                        "right",
                        choice(
                            $.expression,
                            $.assignment_expression,
                            alias(
                                $._statement_comma_expression,
                                $.comma_expression,
                            ),
                        ),
                    ),
                ),
            ),

        argument_list: ($) =>
            seq("(", commaSep(choice($.expression, $.compound_statement)), ")"),

        field_expression: ($) =>
            seq(
                prec(
                    PREC.FIELD,
                    seq(
                        // A numeric literal is not a member-access object in NetLinx, so
                        // `5.e3` is the C10201 of probe s141 rather than a field read on
                        // `5`. A bare binary_expression is excluded for the same reason and
                        // costs nothing: `.` binds tighter than every binary operator, so
                        // an unparenthesised binary can only become the object by absorbing
                        // the operator the literal was meant to end.
                        field("argument", $._expression_not_binary_or_number),
                        field("operator", "."),
                    ),
                ),
                field("field", $._field_identifier),
            ),

        parenthesized_expression: ($) =>
            seq(
                "(",
                choice($.expression, $.comma_expression, $.compound_statement),
                ")",
            ),

        devlev_expression: ($) =>
            prec.dynamic(
                PREC.FIELD + 12,
                seq(
                    token("["),
                    field("device", $.expression),
                    ",",
                    field("level", $.expression),
                    "]",
                ),
            ),

        devchan_expression: ($) =>
            prec.dynamic(
                PREC.FIELD + 12,
                seq(
                    token("["),
                    choice(
                        seq(
                            field("device", $.expression),
                            ",",
                            field("channel", $.expression),
                        ),
                        field("devchan", $.expression),
                    ),
                    "]",
                ),
            ),

        devchan_range_expression: ($) =>
            prec.dynamic(
                PREC.FIELD,
                seq(
                    field("start", $.devchan_expression),
                    $.range_operator,
                    field("end", $.devchan_expression),
                ),
            ),

        range_operator: (_) => token(".."),

        // String Expressions in NetLinx are like interpolated strings
        // or string template literals in other languages.
        string_expression: ($) => seq('"', commaSep($.expression), '"'),

        initializer_list: ($) =>
            seq(
                "{",
                commaSep(choice($.expression, $.initializer_list)),
                optional(","),
                "}",
            ),

        literal: ($) =>
            choice($.number_literal, $.string_literal, $.device_literal),

        device_literal: ($) =>
            prec.dynamic(
                PREC.FIELD + 30,
                seq(
                    field("device", $.expression),
                    ":",
                    field("port", $.expression),
                    ":",
                    field("system", $.expression),
                ),
            ),

        // Each part runs from its quote to the next quote, across lines. A
        // part written right after another's closing quote continues the
        // string with a quote character between them.
        string_literal: ($) =>
            seq(
                $.string_part,
                repeat(alias($._string_continuation, $.string_part)),
            ),

        number_literal: (_) => {
            return token(
                choice(
                    // Hexadecimal literals: no sign and no exponent (probes s126, s146,
                    // s147, s152).
                    /\$[0-9a-fA-F]+/,

                    // Decimal integer: no sign and no exponent either. `5e3` is not one
                    // token — NLRC lexes it as `5` followed by the symbol `E3`, which is a
                    // syntax error whether or not `E3` is defined (probes s138, s155, s156).
                    /\d+/,

                    // Pointed mantissa: the only form that takes a sign or an exponent, and
                    // only with digits on the right of the point — `5.` and `5.e3` are
                    // C10201 (probes s136, s141, s148, s153).
                    seq(
                        // A leading `-` is NEVER part of the literal: NLRC binds it as the
                        // unary-minus operator at PREC.NEGATE, so `-1 & nB` is `-(1 & nB)`
                        // and `-7 MOD 3` is `-(7 MOD 3)`. Absorbing it here would hide the
                        // operator from every consumer that reasons about operand shape.
                        // A leading `+` is different and does belong to the token: NLRC
                        // takes it only on a literal written with a decimal point — `+5.5`,
                        // `+.5` and `+5.5e3` compile while `+5` and `+$05` are C10201
                        // whatever the target type (probes s116, s117, s126-s133).
                        optional("+"),
                        choice(
                            // Format: digits.digits
                            seq(/\d+/, ".", /\d+/),
                            // Format: .digits
                            seq(".", /\d+/),
                        ),
                        // Optional scientific notation
                        optional(seq(/[eE]/, optional(/[-+]/), /\d+/)),
                    ),
                ),
            );
        },

        // U+212A: the `i` flag folds `k` to it, and a word is a keyword only
        // if identifier matches every spelling the word's pattern does.
        identifier: (_) => /[_a-zA-Z\u212A][_a-zA-Z0-9\u212A]*/,

        // Event spellings lex as identifiers in name and type positions.
        // The scanner emits event-object tokens only in expression positions.
        _type_identifier: ($) => alias($.identifier, $.type_identifier),
        _field_identifier: ($) => alias($.identifier, $.field_identifier),
        _statement_identifier: ($) =>
            alias($.identifier, $.statement_identifier),

        _semicolon: ($) => choice($._automatic_semicolon, ";"),
    },
});

/**
 * Creates a rule to optionally match one or more of the rules separated by a comma
 *
 * @param {Rule} rule
 *
 * @returns {ChoiceRule}
 */
function commaSep(rule) {
    return optional(commaSep1(rule));
}

/**
 * Creates a rule to match one or more of the rules separated by a comma
 *
 * @param {Rule} rule
 *
 * @returns {SeqRule}
 */
function commaSep1(rule) {
    return seq(rule, repeat(seq(",", rule)));
}

module.exports.PREC = PREC;
module.exports.commaSep = commaSep;
module.exports.commaSep1 = commaSep1;
