; VCOS music cartridge: plays a short two-channel tune and returns to
; the shell on Start.

.include "common.inc"

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
    LDA #$8A
    STA PPUADDR
    LDA #<msg_title
    STA $00
    LDA #>msg_title
    STA $01
    JSR print

    LDA #$21
    STA PPUADDR
    LDA #$06
    STA PPUADDR
    LDA #<msg_exit
    STA $00
    LDA #>msg_exit
    STA $01
    JSR print

    LDA #$0A
    STA PPUMASK
    LDA #0
    STA PPUSCROLL
    STA PPUSCROLL

    LDA #$0F
    STA $4015
    LDA #$BF
    STA $4000
    STA $4004
    LDA #0
    STA $4001
    STA $4005

    LDA #0
    STA $12
    STA $13

frame_loop:
    BIT PPUSTATUS
wait_vblank:
    BIT PPUSTATUS
    BPL wait_vblank

    LDA #$3F
    STA PPUADDR
    LDA #$00
    STA PPUADDR
    LDA $12
    CLC
    ADC #$21
    STA PPUDATA
    LDA #0
    STA PPUSCROLL
    STA PPUSCROLL

    DEC $13
    BNE frame_play
    LDA #24
    STA $13
    JSR next_note

frame_play:
    JSR read_pad
    LDA $10
    AND #BTN_START
    BNE return_to_shell
    JMP frame_loop

next_note:
    LDX $12
    LDA notes_lo,X
    STA $4002
    LDA notes_hi,X
    ORA #$08
    STA $4003
    LDA harm_lo,X
    STA $4006
    LDA harm_hi,X
    ORA #$08
    STA $4007
    INX
    CPX #8
    BNE note_store
    LDX #0
note_store:
    STX $12
    RTS

return_to_shell:
    LDA #$0F
    STA $4015
    LDA #SVC_SHELL
    STA SVC_CMD
    JMP frame_loop

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

notes_lo:
    .byte $A6, $7C, $52, $3F, $1C, $FD, $E1, $D4
notes_hi:
    .byte $01, $01, $01, $01, $01, $00, $00, $00
harm_lo:
    .byte $1C, $FD, $E1, $D4, $A6, $7C, $52, $3F
harm_hi:
    .byte $01, $00, $00, $00, $01, $01, $01, $01

msg_title:
    .byte "VCOS MUSIC", 0
msg_exit:
    .byte "START: SHELL", 0

.org $FFFA
    .word nmi
    .word reset
    .word irq
