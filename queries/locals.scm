;; Scopes
;; ======

;; Function and call definition scopes
(function_definition) @local.scope
(call_definition) @local.scope

;; Function body scope
(function_definition
  body: (compound_statement) @local.scope)

(call_definition
  body: (compound_statement) @local.scope)

;; Block scopes
(compound_statement) @local.scope

;; Loops and conditionals
(for_statement) @local.scope
(while_statement) @local.scope
(if_statement) @local.scope
(switch_statement) @local.scope
(select_statement) @local.scope
(wait_statement) @local.scope
(wait_until_statement) @local.scope

;; Event blocks
(button_event_type) @local.scope
(data_event_type) @local.scope
(channel_event_type) @local.scope

;; Definitions
;; ===========

;; Function name definition
(function_definition
  name: (identifier) @local.definition)

;; Function parameters
(function_definition
  parameters: (parameter_list
    (parameter_declaration
      declarator: (identifier) @local.definition)))

;; Function parameters (array)
(function_definition
  parameters: (parameter_list
    (parameter_declaration
      declarator: (array_declarator
        declarator: (identifier) @local.definition))))

;; Call parameters
(call_definition
  parameters: (parameter_list
    (parameter_declaration
      declarator: (identifier) @local.definition)))

;; Call parameters (array)
(call_definition
  parameters: (parameter_list
    (parameter_declaration
      declarator: (array_declarator
        declarator: (identifier) @local.definition))))

;; Local variables (with storage specifier)
(declaration
  (storage_class_specifier)
  declarator: (identifier) @local.definition)

;; Regular variable declarations
(declaration
  declarator: (identifier) @local.definition)

;; References
;; ==========

;; Any identifier that is not part of a declaration is a reference
(identifier) @local.reference
