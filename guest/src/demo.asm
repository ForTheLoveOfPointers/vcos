; VCOS demo cartridge: animates the backdrop and returns to the shell
; when Start is pressed.

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

    LDA #$21
    STA PPUADDR
    LDA #$48
    STA PPUADDR
    LDA #<msg_hello
    STA $00
    LDA #>msg_hello
    STA $01
    JSR print

    LDA #$21
    STA PPUADDR
    LDA #$C6
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

frame_loop:
wait_vblank:
    BIT PPUSTATUS
    BPL wait_vblank

    ; Animate the universal background colour once per frame.
    LDA #$3F
    STA PPUADDR
    LDA #$00
    STA PPUADDR
    LDA $12
    CLC
    ADC #1
    AND #$0F
    STA $12
    CLC
    ADC #$21
    STA PPUDATA
    LDA #0
    STA PPUSCROLL
    STA PPUSCROLL

    JSR read_pad
    LDA $10
    AND #BTN_START
    BNE return_to_shell
    JMP frame_loop

return_to_shell:
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

msg_hello:
    .byte "HELLO VCOS", 0
msg_exit:
    .byte "START: SHELL", 0

.org $FFFA
    .word nmi
    .word reset
    .word irq
