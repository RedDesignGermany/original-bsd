/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)muldi3.c	5.3 (Berkeley) 05/12/92";
#endif /* LIBC_SCCS and not lint */

#include "longlong.h"

static void bmul ();

long long 
__muldi3 (u, v)
     long long u, v;
{
  long a[2], b[2], c[2][2];
  long_long w;
  long_long uu, vv;

  uu.ll = u;
  vv.ll = v;

  a[HIGH] = uu.s.high;
  a[LOW] = uu.s.low;
  b[HIGH] = vv.s.high;
  b[LOW] = vv.s.low;

  bmul (a, b, c, sizeof a, sizeof b);

  w.s.high = c[LOW][HIGH];
  w.s.low = c[LOW][LOW];
  return w.ll;
}

static void 
bmul (a, b, c, m, n)
    unsigned short *a, *b, *c;
    size_t m, n;
{
  int i, j;
  unsigned short *d;
  unsigned long acc;

  m /= sizeof *a;
  n /= sizeof *b;

  for (i = m + n, d = c; i-- > 0; *d++ = 0)
    ;

  for (j = little_end (n); is_not_msd (j, n); j = next_msd (j))
    {
      unsigned short *c1 = c + j + little_end (2);
      acc = 0;
      for (i = little_end (m); is_not_msd (i, m); i = next_msd (i))
	{
	  /* Widen before arithmetic to avoid loss of high bits.  */
	  acc += (unsigned long) a[i] * b[j] + c1[i];
	  c1[i] = acc & low16;
	  acc = acc >> 16;
	}
      c1[i] = acc;
    }
}
