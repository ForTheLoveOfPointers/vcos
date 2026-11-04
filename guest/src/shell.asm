; VCOS guest shell: library menu, boot counter, launch and power off.

.include "common.inc"

.equ ROW_LIST0, $0102
.equ ROW_STEP,  $0040

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

    LDA #SVC_LIB_COUNT
    STA SVC_CMD
    LDA SVC_DATA
    STA $12
    CMP #6
    BCC count_ok
    LDA #6
count_ok:
    STA $16
    LDA #0
    STA $11
    STA $13

    JSR draw_ui

main_loop:
    JSR read_pad
    LDA $10
    EOR $13
    AND $10
    STA $15
    LDA $10
    STA $13

    LDA $15
    AND #BTN_DOWN
    BNE move_down
    LDA $15
    AND #BTN_UP
    BNE move_up
    LDA $15
    AND #(BTN_A | BTN_START)
    BNE launch_selected
    LDA $15
    AND #BTN_SELECT
    BNE power_off
    JMP main_loop

move_down:
    LDA $11
    CLC
    ADC #1
    CMP $16
    BCS main_loop
    STA $11
    JSR draw_ui
    JMP main_loop

move_up:
    LDA $11
    BEQ main_loop
    SEC
    SBC #1
    STA $11
    JSR draw_ui
    JMP main_loop

launch_selected:
    LDA $16
    BEQ main_loop
    LDA $11
    STA SVC_ARG0
    LDA #SVC_LIB_LAUNCH
    STA SVC_CMD
    JMP main_loop

power_off:
    LDA #SVC_SHUTDOWN
    STA SVC_CMD
    JMP main_loop

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
    LDA #$6A
    STA PPUADDR
    LDA #<msg_title
    STA $00
    LDA #>msg_title
    STA $01
    JSR print

    LDA #$20
    STA PPUADDR
    LDA #$AA
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

    LDA $16
    BNE list_start
    LDA #$21
    STA PPUADDR
    LDA #$02
    STA PPUADDR
    LDA #<msg_empty
    STA $00
    LDA #>msg_empty
    STA $01
    JSR print
    JMP footer

list_start:
    LDX #0
list_loop:
    STX $14
    TXA
    TAY
    STY $17

    LDA list_hi,Y
    STA PPUADDR
    LDA list_lo,Y
    STA PPUADDR

    LDA #' '
    CPX $11
    BNE marker_done
    LDA #'>'
marker_done:
    STA PPUDATA

    LDA list_hi,Y
    STA PPUADDR
    LDA list_lo,Y
    CLC
    ADC #2
    STA PPUADDR

    LDA $14
    STA SVC_ARG0
    LDA #SVC_LIB_NAME
    STA SVC_CMD

    LDY #0
name_loop:
    LDA SVC_DATA,Y
    BEQ name_done
    STA PPUDATA
    INY
    CPY #15
    BNE name_loop
name_done:
    LDX $14
    INX
    CPX $16
    BCC list_loop

footer:
    LDA #$22
    STA PPUADDR
    LDA #$42
    STA PPUADDR
    LDA #<msg_help1
    STA $00
    LDA #>msg_help1
    STA $01
    JSR print

    LDA #$22
    STA PPUADDR
    LDA #$82
    STA PPUADDR
    LDA #<msg_help2
    STA $00
    LDA #>msg_help2
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
msg_empty:
    .byte "NO CARTRIDGES", 0
msg_help1:
    .byte "UP/DOWN SELECT", 0
msg_help2:
    .byte "A LAUNCH  SELECT OFF", 0

.org $FFFA
    .word nmi
    .word reset
    .word irq
