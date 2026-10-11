#pragma once
extern int ints;                        /* SREG's I bit */
#define cli() (ints = 0)
