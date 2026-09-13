/* Apple IIe Time Pilot IIvera — custom linker script.
 *
 * Loads at $1400 instead of $2000, giving up to 33,280 bytes ($1400-$95FF).
 * $1000-$12FF is used by ProDOS QUIT code and must not be overwritten.
 * $1300-$13FF is a BASIC.SYSTEM scratch area; $1400 is the first genuinely
 * free address under ProDOS 2.4.3 + BASIC.SYSTEM.
 *
 * ProDOS BRUN transfers control to the load address ($1400), so entry point
 * must be at $1400.
 */

/* Provide imaginary (zero page) registers.  Apple II zero page $00-$01 is
 * reserved; we start imaginary registers at $02. */
__rc0 = 0x02;
INCLUDE imag-regs.ld   /* __rc31 = 0x20 */

MEMORY {
    /* Zero page variables: $21-$BF.  ($C0-$FF left for system/monitor.) */
    zp : ORIGIN = __rc31 + 1, LENGTH = 0xBF - (__rc31 + 1)
    /* Main program RAM: $1400-$9BFF (34,816 bytes, leaves 8.5KB below $BE00 stack). */
    ram (rw) : ORIGIN = 0x1400, LENGTH = 0x8800
}

REGION_ALIAS("c_readonly", ram)
REGION_ALIAS("c_writeable", ram)

/* Entry point at $1400 (ProDOS BRUN jumps here). */
SECTIONS {
    . = 0x1400;
    .text : { INCLUDE text-sections.ld } > c_readonly
    INCLUDE zp.ld
    INCLUDE rodata.ld
    INCLUDE data.ld
    INCLUDE bss.ld
    INCLUDE noinit.ld
}

/* Soft stack grows down from $BE00 (same as stock apple2e). */
__stack = 0xBE00;

OUTPUT_FORMAT {
    /* ProDOS BIN header: 2-byte load address ($1400) + 2-byte length. */
    SHORT(0x1400)
    SHORT(__data_end - 0x1400)
    TRIM(ram)
}
