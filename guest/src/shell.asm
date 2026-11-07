; VCOS guest shell: console menu, game menu, launch and power off.

.include "common.inc"

.equ MODE,       $18
.equ SYS_SEL,    $11
.equ SYS_COUNT,  $16
.equ GAME_SEL,   $19
.equ GAME_COUNT, $1B

.org $8000
reset:
    SEI
    CLD
    LDX #$FF
    TXS

    BIT PPUSTATUS
vwait1:
    BIT PPUSTATUS
    BPL vwait1
vwait2:
    BIT PPUSTATUS
    BPL vwait2

    LDA #0
    STA PPUCTRL
    STA PPUMASK

    LDX #0
    LDA #0
clear_ram:
    STA $0000,X
    STA $0100,X
    STA $0200,X
    STA $0300,X
    STA $0400,X
    STA $0500,X
    STA $0600,X
    STA $0700,X
    INX
    BNE clear_ram

    LDA #$3F
    STA PPUADDR
    LDA #$00
    STA PPUADDR
    LDX #0
palette_loop:
    LDA palette,X
    STA PPUDATA
    INX
    CPX #32
    BNE palette_loop

    ; Bump and persist the 16-bit boot counter.
    LDA #SVC_LOAD
    STA SVC_CMD
    LDA SVC_DATA
    CLC
    ADC #1
    STA SVC_DATA
    BNE boot_store
    INC SVC_DATA+1
boot_store:
    LDA #SVC_SAVE
    STA SVC_CMD

    JSR load_systems
    LDA #0
    STA MODE
    STA SYS_SEL
    STA GAME_SEL
    JSR draw_ui

main_loop:
    JSR read_pad
    LDA $10
    EOR $13
    AND $10
    STA $15
    LDA $10
    STA $13

    LDA MODE
    BNE game_input

    LDA $15
    AND #BTN_DOWN
    BEQ no_sys_down
    JMP sys_down
no_sys_down:
    LDA $15
    AND #BTN_UP
    BEQ no_sys_up
    JMP sys_up
no_sys_up:
    LDA $15
    AND #(BTN_A | BTN_START)
    BEQ no_open_system
    JMP open_system
no_open_system:
    LDA $15
    AND #BTN_SELECT
    BEQ no_power_sys
    JMP power_off
no_power_sys:
    JMP main_loop

game_input:
    LDA $15
    AND #BTN_DOWN
    BEQ no_game_down
    JMP game_down
no_game_down:
    LDA $15
    AND #BTN_UP
    BEQ no_game_up
    JMP game_up
no_game_up:
    LDA $15
    AND #BTN_B
    BEQ no_close_system
    JMP close_system
no_close_system:
    LDA $15
    AND #(BTN_A | BTN_START)
    BEQ no_launch_game
    JMP launch_game
no_launch_game:
    LDA $15
    AND #BTN_SELECT
    BEQ no_power_game
    JMP power_off
no_power_game:
    JMP main_loop

sys_down:
    LDA SYS_SEL
    CLC
    ADC #1
    CMP SYS_COUNT
    BCC sys_down_ok
    JMP main_loop
sys_down_ok:
    STA SYS_SEL
    JSR draw_ui
    JMP main_loop

sys_up:
    LDA SYS_SEL
    BNE sys_up_ok
    JMP main_loop
sys_up_ok:
    SEC
    SBC #1
    STA SYS_SEL
    JSR draw_ui
    JMP main_loop

game_down:
    LDA GAME_SEL
    CLC
    ADC #1
    CMP GAME_COUNT
    BCC game_down_ok
    JMP main_loop
game_down_ok:
    STA GAME_SEL
    JSR draw_ui
    JMP main_loop

game_up:
    LDA GAME_SEL
    BNE game_up_ok
    JMP main_loop
game_up_ok:
    SEC
    SBC #1
    STA GAME_SEL
    JSR draw_ui
    JMP main_loop

open_system:
    LDA SYS_COUNT
    BNE open_system_ok
    JMP main_loop
open_system_ok:
    LDA SYS_SEL
    STA SVC_ARG0
    LDA #SVC_SYS_GAME_COUNT
    STA SVC_CMD
    LDA SVC_DATA
    CMP #6
    BCC game_count_ok
    LDA #6
game_count_ok:
    STA GAME_COUNT
    LDA #0
    STA GAME_SEL
    LDA #1
    STA MODE
    JSR draw_ui
    JMP main_loop

close_system:
    LDA #0
    STA MODE
    STA GAME_SEL
    JSR draw_ui
    JMP main_loop

launch_game:
    LDA GAME_COUNT
    BNE launch_game_ok
    JMP main_loop
launch_game_ok:
    LDA SYS_SEL
    STA SVC_ARG0
    LDA GAME_SEL
    STA SVC_ARG1
    LDA #SVC_SYS_LAUNCH
    STA SVC_CMD
    JMP main_loop

power_off:
    LDA #SVC_SHUTDOWN
    STA SVC_CMD
    JMP main_loop

load_systems:
    LDA #SVC_SYS_COUNT
    STA SVC_CMD
    LDA SVC_DATA
    CMP #6
    BCC load_systems_ok
    LDA #6
load_systems_ok:
    STA SYS_COUNT
    RTS

draw_ui:
    BIT PPUSTATUS
wait_draw:
    BIT PPUSTATUS
    BPL wait_draw
    LDA #0
    STA PPUMASK

    LDA #$20
    STA PPUADDR
    LDA #$00
    STA PPUADDR
    LDA #0
    LDX #0
    LDY #4
clear_nt:
    STA PPUDATA
    INX
    BNE clear_nt
    DEY
    BNE clear_nt

    LDA #$20
    STA PPUADDR
    LDA #$2A
    STA PPUADDR
    LDA #<msg_title
    STA $00
    LDA #>msg_title
    STA $01
    JSR print

    LDA #$20
    STA PPUADDR
    LDA #$62
    STA PPUADDR
    LDA #<msg_boot
    STA $00
    LDA #>msg_boot
    STA $01
    JSR print
    LDA SVC_DATA
    AND #$0F
    CLC
    ADC #$30
    STA PPUDATA

    LDA MODE
    BEQ draw_systems
    JMP draw_games

draw_systems:
    LDA #$20
    STA PPUADDR
    LDA #$A2
    STA PPUADDR
    LDA #<msg_consoles
    STA $00
    LDA #>msg_consoles
    STA $01
    JSR print

    LDA SYS_COUNT
    BNE system_list_start
    LDA #$21
    STA PPUADDR
    LDA #$02
    STA PPUADDR
    LDA #<msg_no_systems
    STA $00
    LDA #>msg_no_systems
    STA $01
    JSR print
    JMP footer

system_list_start:
    LDX #0
system_list_loop:
    STX $14
    TXA
    TAY
    STY $17

    LDA list_hi,Y
    STA PPUADDR
    LDA list_lo,Y
    STA PPUADDR

    LDA #' '
    CPX SYS_SEL
    BNE system_marker_done
    LDA #'>'
system_marker_done:
    STA PPUDATA

    LDA list_hi,Y
    STA PPUADDR
    LDA list_lo,Y
    CLC
    ADC #2
    STA PPUADDR

    LDA $14
    STA SVC_ARG0
    LDA #SVC_SYS_NAME
    STA SVC_CMD

    LDY #0
system_name_loop:
    LDA SVC_DATA,Y
    BEQ system_name_done
    STA PPUDATA
    INY
    CPY #15
    BNE system_name_loop
system_name_done:
    LDX $14
    INX
    CPX SYS_COUNT
    BCC system_list_loop
    JMP footer

draw_games:
    LDA #$20
    STA PPUADDR
    LDA #$A2
    STA PPUADDR
    LDA SYS_SEL
    STA SVC_ARG0
    LDA #SVC_SYS_NAME
    STA SVC_CMD
    LDY #0
game_header_loop:
    LDA SVC_DATA,Y
    BEQ game_header_done
    STA PPUDATA
    INY
    CPY #15
    BNE game_header_loop
game_header_done:

    LDA GAME_COUNT
    BNE game_list_start
    LDA #$21
    STA PPUADDR
    LDA #$02
    STA PPUADDR
    LDA #<msg_no_games
    STA $00
    LDA #>msg_no_games
    STA $01
    JSR print
    JMP footer

game_list_start:
    LDX #0
game_list_loop:
    STX $14
    TXA
    TAY
    STY $17

    LDA list_hi,Y
    STA PPUADDR
    LDA list_lo,Y
    STA PPUADDR

    LDA #' '
    CPX GAME_SEL
    BNE game_marker_done
    LDA #'>'
game_marker_done:
    STA PPUDATA

    LDA list_hi,Y
    STA PPUADDR
    LDA list_lo,Y
    CLC
    ADC #2
    STA PPUADDR

    LDA SYS_SEL
    STA SVC_ARG0
    LDA $14
    STA SVC_ARG1
    LDA #SVC_SYS_GAME_NAME
    STA SVC_CMD

    LDY #0
game_name_loop:
    LDA SVC_DATA,Y
    BEQ game_name_done
    STA PPUDATA
    INY
    CPY #15
    BNE game_name_loop
game_name_done:
    LDX $14
    INX
    CPX GAME_COUNT
    BCC game_list_loop

footer:
    LDA MODE
    BNE game_footer
    LDA #<msg_sys_help1
    STA $00
    LDA #>msg_sys_help1
    STA $01
    LDA #<msg_sys_help2
    STA $02
    LDA #>msg_sys_help2
    STA $03
    JMP draw_footer

game_footer:
    LDA #<msg_game_help1
    STA $00
    LDA #>msg_game_help1
    STA $01
    LDA #<msg_game_help2
    STA $02
    LDA #>msg_game_help2
    STA $03

draw_footer:
    LDA #$23
    STA PPUADDR
    LDA #$02
    STA PPUADDR
    JSR print

    LDA #$23
    STA PPUADDR
    LDA #$42
    STA PPUADDR
    LDA $02
    STA $00
    LDA $03
    STA $01
    JSR print

    LDA #0
    STA PPUSCROLL
    STA PPUSCROLL
    LDA #$0A
    STA PPUMASK
    RTS

print:
    LDY #0
print_loop:
    LDA ($00),Y
    BEQ print_done
    STA PPUDATA
    INY
    BNE print_loop
print_done:
    RTS

read_pad:
    LDA #1
    STA JOY1
    LDA #0
    STA JOY1
    LDA #0
    STA $10
    LDX #8
read_pad_loop:
    LDA JOY1
    LSR A
    ROL $10
    DEX
    BNE read_pad_loop
    RTS

nmi:
    RTI
irq:
    RTI

palette:
    .byte $0F, $30, $21, $00, $0F, $16, $27, $18
    .byte $0F, $1A, $2A, $3A, $0F, $12, $22, $32
    .byte $0F, $30, $21, $00, $0F, $16, $27, $18
    .byte $0F, $1A, $2A, $3A, $0F, $12, $22, $32

list_lo:
    .byte $02, $42, $82, $C2, $02, $42
list_hi:
    .byte $21, $21, $21, $21, $22, $22

msg_title:
    .byte "VCOS SHELL", 0
msg_boot:
    .byte "BOOT COUNT: ", 0
msg_consoles:
    .byte "CONSOLES", 0
msg_no_systems:
    .byte "NO CONSOLES", 0
msg_no_games:
    .byte "NO GAMES", 0
msg_sys_help1:
    .byte "UP/DOWN SELECT", 0
msg_sys_help2:
    .byte "A OPEN  SELECT OFF", 0
msg_game_help1:
    .byte "UP/DOWN SELECT  B BACK", 0
msg_game_help2:
    .byte "A LAUNCH  SELECT OFF", 0

.org $FFFA
    .word nmi
    .word reset
    .word irq
