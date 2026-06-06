PROGRAM_NAME='wait_bare_body'

DEFINE_DEVICE
dvDevice = 5001:1:0

DEFINE_VARIABLE
INTEGER nReady
INTEGER nTimeout

DEFINE_FUNCTION doAction()
{
    nReady = 1
}

DEFINE_FUNCTION doDelayed()
{
    nReady = 2
}

DEFINE_FUNCTION doNamed()
{
    nReady = 3
}

DEFINE_START
{
    // Bare single-statement wait body (no braces)
    WAIT 10 doAction()

    // Named wait with bare body
    WAIT 20 'DelayedAction' doDelayed()

    // Braced wait body (standard form)
    WAIT 30 'NamedBraced'
    {
        doNamed()
    }

    // Wait until with bare body
    WAIT_UNTIL (nReady) doAction()
}

DEFINE_PROGRAM
{
}
