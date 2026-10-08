/*
 * The app's side of the core's table: each call the app makes is to a fixed
 * slot below the boundary, whatever core is in there.
 */
#include "layout.h"

#define X(n, f) .global f $ .set f, CORE_TABLE + 2 * (n) $
    CORE_CALLS(X)
