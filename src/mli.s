; ProDOS MLI wrappers. C fills the parameter blocks and reads mli_status.
        .section .bss
        .global mli_status
mli_status: .byte 0
        .global mli_zp_save
mli_zp_save: .byte 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

        .section .data
        .global mli_open_params
mli_open_params: .byte 3,0,0,0,0,0
        .global mli_close_params
mli_close_params: .byte 1,0
        .global mli_read_params
mli_read_params: .byte 4,0,0,0,0,0,0,0
        .global mli_write_params
mli_write_params: .byte 4,0,0,0,0,0,0,0
        .global mli_mark_params
mli_mark_params: .byte 2,0,0,0,0
        .global mli_eof_params
mli_eof_params: .byte 2,0,0,0,0
        .global mli_create_params
mli_create_params: .byte 7,0,0,0,0,0,0,1,0,0,0,0
        .global mli_online_params
mli_online_params: .byte 2,0,0,0,0
        .global mli_prefix_params
mli_prefix_params: .byte 1,0,0

        .section .text
        .macro CALL_MLI name, opcode, params
        .global \name
\name:
        LDX #15
.Lsave\@:
        LDA $40,X
        STA mli_zp_save,X
        DEX
        BPL .Lsave\@
        JSR $BF00
        .byte \opcode
        .word \params
        STA mli_status
        LDX #15
.Lrest\@:
        LDA mli_zp_save,X
        STA $40,X
        DEX
        BPL .Lrest\@
        RTS
        .endmacro

        CALL_MLI mlib_open,  $C8, mli_open_params
        CALL_MLI mlib_close, $CC, mli_close_params
        CALL_MLI mlib_read,  $CA, mli_read_params
        CALL_MLI mlib_write, $CB, mli_write_params
        CALL_MLI mlib_set_mark, $CE, mli_mark_params
        CALL_MLI mlib_set_eof, $D0, mli_eof_params
        CALL_MLI mlib_create, $C0, mli_create_params
        CALL_MLI mlib_on_line, $C5, mli_online_params
        CALL_MLI mlib_get_prefix, $C7, mli_prefix_params

        .section .data
        .global mlib_quit
quit_params:
        .byte 4,0,0,0,0,0,0
        .section .text
mlib_quit:
        JSR $BF00
        .byte $65
        .word quit_params
        RTS
