/*
 * msvcrt_compat.c - Compatibility shims for pre-compiled .a libraries
 * that were built against older MSVCRT (Visual C++ 6/2003 era).
 *
 * Modern MinGW (UCRT-based) no longer exports __iob_func or __mb_cur_max
 * as the old msvcrt.dll did. This file provides shims so the pre-compiled
 * bcrypt.a and libregex.a can link successfully.
 *
 * IMPORTANT: The old libraries were compiled with __declspec(dllimport),
 * so they reference __imp___iob_func and __imp___mb_cur_max (the PE
 * import thunk pointers), NOT the bare symbol names. We must provide
 * the __imp__ pointer variables that the linker actually needs.
 */

#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  __iob_func shim                                                    */
/* ------------------------------------------------------------------ */

/*
 * Old MSVCRT provided __iob_func() returning a pointer to a contiguous
 * array of FILE structs: [stdin, stdout, stderr].
 *
 * We use a helper with a different name to avoid conflicting with
 * MinGW's dllimport declaration of __iob_func in <stdio.h>.
 */
static FILE iob_arr[3];

static FILE* __cdecl my_iob_func(void)
{
    iob_arr[0] = *stdin;
    iob_arr[1] = *stdout;
    iob_arr[2] = *stderr;
    return iob_arr;
}

/*
 * The old library references __imp___iob_func (dllimport thunk).
 * In PE/COFF on x86_64, __imp_X is a pointer variable holding the
 * address of X. Provide this so the linker can resolve it.
 */
FILE* (__cdecl *__imp___iob_func)(void) = my_iob_func;


/* ------------------------------------------------------------------ */
/*  __mb_cur_max shim                                                  */
/* ------------------------------------------------------------------ */

/*
 * Old MSVCRT exported __mb_cur_max as a dllimport integer variable.
 * The pre-compiled regex library references __imp___mb_cur_max.
 *
 * Modern MinGW headers #define __mb_cur_max as a macro expanding to
 * ___mb_cur_max_func(), so we must #undef it first.
 */
#undef __mb_cur_max

static int mb_cur_max_val = 1;

/*
 * The old library references __imp___mb_cur_max (dllimport thunk).
 * Provide a pointer variable with that exact name.
 */
int *__imp___mb_cur_max = &mb_cur_max_val;
