/* Flash programming routine for the AT32F415, loaded into SRAM at 0x20000000
 * by the web flasher (tools/web/at32.js) and run over SWD.
 *
 *   in:  r0 = destination in flash (word aligned), r1 = source in SRAM,
 *        r2 = number of 32-bit words
 *   out: r0 = 0 on success, otherwise FLASH_STS (PRGMERR 0x04 / EPPERR 0x10),
 *        r1 = address of the word that failed
 *   ends with BKPT, which halts the core for the debugger.
 *
 * Words that are 0xFFFFFFFF are skipped (the sector is already erased).
 * The WDT is reloaded after each word in case it is running.
 *
 * Rebuild: ./build_stub.sh (prints the bytes to paste into at32.js).
 */
    .syntax unified
    .cpu cortex-m4
    .thumb
    .global _start
_start:
    ldr   r3, =0x40022000       @ FLASH
    ldr   r6, =0x40003000       @ WDT_CMD
    movw  r7, #0xAAAA           @ WDT reload
    movs  r4, #0x34
    str   r4, [r3, #0x0C]       @ STS: clear ODF, EPPERR, PRGMERR
    movs  r4, #1
    str   r4, [r3, #0x10]       @ CTRL = FPRGM
loop:
    cbz   r2, done
    ldr   r4, [r1], #4
    adds  r5, r4, #1            @ 0xFFFFFFFF: nothing to program
    beq   next
    str   r4, [r0]
wait:
    ldr   r5, [r3, #0x0C]
    tst   r5, #1                @ OBF (busy)
    bne   wait
    tst   r5, #0x14             @ EPPERR | PRGMERR
    bne   fail
next:
    str   r7, [r6]
    adds  r0, #4
    subs  r2, #1
    b     loop
done:
    movs  r4, #0
    str   r4, [r3, #0x10]
    movs  r0, #0
    bkpt  #0
fail:
    movs  r4, #0
    str   r4, [r3, #0x10]
    mov   r1, r0
    mov   r0, r5
    bkpt  #0
    .ltorg
