/*-
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)kvm.h	5.2 (Berkeley) 02/12/91
 */

#include <sys/cdefs.h>

/* Default version symbol. */
#define	VRS_SYM		"_version"
#define	VRS_KEY		"VERSION"

__BEGIN_DECLS
char		*kvm_getargs __P((const struct proc *, const struct user *));
struct eproc	*kvm_geteproc __P((const struct proc *));
char		*kvm_geterr __P((void));
struct user	*kvm_getu __P((const struct proc *));
struct proc	*kvm_nextproc __P((void));
__END_DECLS
