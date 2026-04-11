;; ============================================================================
;; DEFINE_* SECTIONS
;; ============================================================================
(define_variable_section) @fold
(define_constant_section) @fold
(define_type_section) @fold
(define_start_section) @fold
(define_event_section) @fold
(define_program_section) @fold
(define_device_section) @fold
(define_mutually_exclusive_section) @fold
(define_latching_section) @fold
(define_toggling_section) @fold
(define_combine_section) @fold
(define_connect_level_section) @fold
(define_system_variable_section) @fold

;; ============================================================================
;; STRUCTURES
;; ============================================================================
(struct_specifier
  body: (field_declaration_list) @fold)

;; ============================================================================
;; FUNCTIONS AND CALLS
;; ============================================================================
(function_definition
  body: (compound_statement) @fold)

(call_definition
  body: (compound_statement) @fold)

;; ============================================================================
;; EVENT HANDLERS
;; ============================================================================
(button_event_block) @fold
(channel_event_block) @fold
(data_event_block) @fold

(custom_event_definition
  body: (compound_statement) @fold)
(timeline_event_definition
  body: (compound_statement) @fold)
(level_event_definition
  body: (compound_statement) @fold)

;; ============================================================================
;; CONTROL FLOW
;; ============================================================================
(if_statement
  consequence: (compound_statement) @fold
  alternative: (else_clause (compound_statement) @fold)?)
(if_statement
  alternative: (else_clause (if_statement)) @fold)
(switch_statement
  body: (compound_statement) @fold)
(select_statement) @fold
(while_statement
  body: (compound_statement) @fold)
(for_statement
  body: (compound_statement) @fold)

;; ============================================================================
;; PREPROCESSOR CONDITIONALS
;; ============================================================================
(preproc_if_defined) @fold
(preproc_if_not_defined) @fold

;; ============================================================================
;; MISCELLANEOUS
;; ============================================================================
(compound_statement) @fold
(initializer_list) @fold
(comment) @fold
