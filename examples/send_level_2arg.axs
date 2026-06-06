PROGRAM_NAME='send_level_2arg'

DEFINE_DEVICE
dvTP = 10001:1:0

DEFINE_CONSTANT
LVL_FD_NUM = 4

DEFINE_VARIABLE
volatile DEVLEV DLS_FDIN[LVL_FD_NUM]
volatile DEVLEV dvLev

DEFINE_FUNCTION SetLevels(INTEGER nIndex) {
    send_level dvLev, 174
    send_level DLS_FDIN[nIndex], 174
    send_level dvTP, 1, 174
}

DEFINE_PROGRAM
