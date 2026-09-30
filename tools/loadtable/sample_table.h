/* sample_table.h -- a macro table written out by hand, in the packed format
 * DigiMacro.ino reads. Included by tools/loadtable, which writes it into a
 * board's EEPROM, and by test/table_test.cpp, which plays every code in it
 * against the real firmware. One original, so the table that gets flashed is
 * the table that was tested.
 *
 * The macros below are guarded, because the test includes the firmware too
 * and the firmware defines some of the same names -- deliberately, since the
 * format is what the two ends have to agree on.
 */
#ifndef SAMPLE_TABLE_H
#define SAMPLE_TABLE_H

#define HDR(n, c)   ((uint8_t)(((c) << 3) | (((n) - 1) & 0x07)))
#ifndef ESC_PAUSE
#define ESC_PAUSE   0x01
#define ESC_MOD     0x02
#define ESC_REPEAT  0x03
#endif

#define L(c)        ((uint8_t)((c) - 'a' + 0x04))   /* a lower-case letter */
#define U(c)        ((uint8_t)(L(c) | 0x80))        /* ...with Shift        */
#define D(n)        ((uint8_t)((n) ? (n) + 0x1D : 0x27))     /* a digit      */
#define K_ENTER     0x28
#define K_SPACE     0x2C
#define K_DOT       0x37
#define K_LEFT      0x50
#define MOD_CTRL    0x01

/* A code is the symbols as bits, a dash being 1, with the first symbol in the
   highest bit the length uses: ".-" is 0b01, "---" is 0b111. */
static const uint8_t PROGMEM table[] = {
    /* "."  -> DigiMacro 1.0 + Enter */
    HDR(1, 0b0),
        U('d'), L('i'), L('g'), L('i'), U('m'), L('a'), L('c'), L('r'), L('o'),
        K_SPACE, D(1), K_DOT, D(0), K_ENTER, 0,

    /* "-"  -> wait... one second ... ok */
    HDR(1, 0b1),
        L('w'), L('a'), L('i'), L('t'), K_DOT, ESC_REPEAT, 2,
        ESC_PAUSE, 10,                  /* ten tenths */
        L('o'), L('k'), 0,

    /* ".." -> abbbbb, the b written once */
    HDR(2, 0b00),
        L('a'), L('b'), ESC_REPEAT, 4, 0,

    /* ".-" -> Ctrl+Left, then z: a modifier mask on a key that is not a
              character at all, which is what ESC_MOD is for */
    HDR(2, 0b01),
        ESC_MOD, MOD_CTRL, K_LEFT, L('z'), 0,

    /* "---" -> longer than any entry DigiMacroCDC could hold */
    HDR(3, 0b111),
        L('t'), L('h'), L('i'), L('s'), K_SPACE,
        L('e'), L('n'), L('t'), L('r'), L('y'), K_SPACE,
        L('i'), L('s'), K_SPACE,
        L('l'), L('o'), L('n'), L('g'), L('e'), L('r'), K_SPACE,
        L('t'), L('h'), L('a'), L('n'), K_SPACE,
        L('t'), L('h'), L('i'), L('r'), L('t'), L('y'), K_SPACE,
        L('k'), L('e'), L('y'), L('s'), 0,

    0xFF                                /* end of table */
};

#endif /* SAMPLE_TABLE_H */
