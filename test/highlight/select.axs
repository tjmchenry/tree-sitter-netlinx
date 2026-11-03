DEFINE_FUNCTION fn(INTEGER p) {
    STACK_VAR MyType blockLocal
    // ^ type.storage
    //         ^ type
    //                 ^ variable

    SELECT {
    // ^ keyword
        ACTIVE (p = 1):
        // ^ keyword
            STACK_VAR MyType armLocal
            // ^ type.storage
            //         ^ type
            //                 ^ variable
            LOCAL_VAR MyType armStatic
            // ^ type.storage
            //         ^ type
            //                 ^ variable
#IF_DEFINED ENABLED
        ACTIVE (p = 2):
#END_IF
            STACK_VAR MyType afterConditional
            // ^ type.storage
            //         ^ type
            //                 ^ variable
        ACTIVE (p = 3):
            p = 0
    }
}
