/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)r_dim.c	5.2	07/08/85
 */

float r_dim(a,b)
float *a, *b;
{
return( *a > *b ? *a - *b : 0);
}
