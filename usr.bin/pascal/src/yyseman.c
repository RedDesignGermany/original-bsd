/*-
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "@(#)yyseman.c	8.1 (Berkeley) 06/06/93";
#endif /* not lint */

#include "whoami.h"
#include "0.h"
#include "tree_ty.h"	/* must be included for yy.h */
#include "yy.h"

/*
 * Assign semantics to a generated token
 *
 * Most terminals have a semantic value the current
 * input line.  If they are generated they are flagged
 * by having this number negated.
 *
 * The terminals which have true semantics such
 * as identifiers and strings are instead given
 * semantic value NIL here - we do not attempt
 * to do repair, e.g. by giving generated integers
 * the value 1, etc.
 */
nullsem(ch)
	int ch;
{

	switch (ch) {
		case YID:
		case YINT:
		case YNUMB:
		case YBINT:
		case YSTRING:
			return (NIL);
		default:
			return (-yyeline);
	}
}
