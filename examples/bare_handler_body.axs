PROGRAM_NAME='bare_handler_body'

DEFINE_DEVICE
dvTP    = 10001:1:0
dvRELAY = 5001:1:0

DEFINE_VARIABLE
INTEGER nLvl

DEFINE_FUNCTION do_push(DEV dvDev, INTEGER nBtn)
{
    nLvl = nBtn
}

DEFINE_EVENT

BUTTON_EVENT[dvTP, 1] {
    PUSH: do_push(dvTP, 1)
    RELEASE: { do_push(dvTP, 1) }
}

CHANNEL_EVENT[dvRELAY, 1] {
    ON:  on[dvTP, 7]
    OFF: off[dvTP, 7]
}

DATA_EVENT[dvTP] {
    ONLINE: send_command dvTP, 'HELLO'
}

LEVEL_EVENT[dvTP, 1] nLvl = 1

CUSTOM_EVENT[dvTP, 1, 1] nLvl = 1

TIMELINE_EVENT[1] nLvl = 1

DEFINE_PROGRAM
