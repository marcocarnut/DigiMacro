/* XTEA, encrypt only, 32 cycles (counter mode never decrypts):
 *
 *      void xtea(uint8_t v[8], const uint8_t key[16]);
 *
 * v is v0 then v1, key k[0..3], all little-endian 32-bit words, as
 * web/table.js has them. v0 r20-23, v1 r24-27, sum r16-19, the key at
 * r10:11, v at r14:15. Both half-cycles are the same subroutine, v0 +=
 * F(v1), by swapping v0 and v1 after each: an even number of swaps a cycle
 * leaves them where they started.
 *
 * Checked against the C reference in build/cipher-spike (simavr), and the
 * page's against the same vectors (test/webtable.js).
 */
    .text
    .global xtea
xtea:
    /* r2-r17 are the caller's: saved through the register file, which the
       ATtiny maps at data addresses 0-31 -- a loop, not sixteen pushes. */
    ldi r30, 2
    clr r31
1:  ld r0, Z+
    push r0
    cpi r30, 18
    brne 1b
    movw r10, r22       /* the key */
    movw r14, r24       /* v, for the end */
    movw r30, r24
    ld r20, Z+          /* v0 */
    ld r21, Z+
    ld r22, Z+
    ld r23, Z+
    ld r24, Z+          /* v1 */
    ld r25, Z+
    ld r26, Z+
    ld r27, Z
    clr r16             /* sum */
    clr r17
    movw r18, r16
    ldi r30, 32
    mov r12, r30        /* cycles */
.Lcycle:
    mov r13, r16        /* k[sum & 3] */
    rcall half
    subi r16, 0x47      /* sum += 0x9E3779B9, as -0x61C88647 */
    sbci r17, 0x86
    sbci r18, 0xC8
    sbci r19, 0x61
    mov r13, r17        /* k[(sum >> 11) & 3] */
    lsr r13
    lsr r13
    lsr r13
    rcall half
    dec r12
    brne .Lcycle
    movw r30, r14
    st Z+, r20
    st Z+, r21
    st Z+, r22
    st Z+, r23
    st Z+, r24
    st Z+, r25
    st Z+, r26
    st Z, r27
    ldi r30, 18
    clr r31
2:  pop r0
    st -Z, r0
    cpi r30, 2
    brne 2b
    ret

/* v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[r13 & 3]), then swap */
half:
    movw r2, r24        /* T = v1 << 4 */
    movw r4, r26
    ldi r31, 4
1:  lsl r2
    rol r3
    rol r4
    rol r5
    dec r31
    brne 1b
    movw r6, r24        /* U = v1 >> 5 */
    movw r8, r26
    ldi r31, 5
2:  lsr r9
    ror r8
    ror r7
    ror r6
    dec r31
    brne 2b
    eor r2, r6
    eor r3, r7
    eor r4, r8
    eor r5, r9
    add r2, r24         /* + v1 */
    adc r3, r25
    adc r4, r26
    adc r5, r27
    mov r30, r13        /* U = sum + k[i] */
    andi r30, 3
    lsl r30
    lsl r30
    add r30, r10
    mov r31, r11
    adc r31, r1
    ld r6, Z+
    ld r7, Z+
    ld r8, Z+
    ld r9, Z
    add r6, r16
    adc r7, r17
    adc r8, r18
    adc r9, r19
    eor r2, r6
    eor r3, r7
    eor r4, r8
    eor r5, r9
    add r20, r2         /* v0 += */
    adc r21, r3
    adc r22, r4
    adc r23, r5
    movw r6, r20        /* swap v0 and v1 */
    movw r8, r22
    movw r20, r24
    movw r22, r26
    movw r24, r6
    movw r26, r8
    ret
