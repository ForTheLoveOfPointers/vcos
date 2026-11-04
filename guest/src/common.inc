; Shared hardware and VCOS service definitions.

.equ PPUCTRL,   $2000
.equ PPUMASK,   $2001
.equ PPUSTATUS, $2002
.equ OAMADDR,   $2003
.equ OAMDATA,   $2004
.equ PPUSCROLL, $2005
.equ PPUADDR,   $2006
.equ PPUDATA,   $2007

.equ JOY1,      $4016
.equ JOY2,      $4017

.equ SVC_CMD,   $5000
.equ SVC_ARG0,  $5001
.equ SVC_ARG1,  $5002
.equ SVC_STATUS, $5003
.equ SVC_DATA,  $5008

.equ SVC_LAUNCH,  1
.equ SVC_SHELL,   2
.equ SVC_SHUTDOWN, 3
.equ SVC_SAVE,    5
.equ SVC_LOAD,    6
.equ SVC_VERSION, 7
.equ SVC_LIB_COUNT, 8
.equ SVC_LIB_NAME,  9
.equ SVC_LIB_LAUNCH, 10
.equ SVC_REBOOT, 11

.equ BTN_A,      $80
.equ BTN_B,      $40
.equ BTN_SELECT, $20
.equ BTN_START,  $10
.equ BTN_UP,     $08
.equ BTN_DOWN,   $04
.equ BTN_LEFT,   $02
.equ BTN_RIGHT,  $01
