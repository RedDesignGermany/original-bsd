/*
 * Copyright (c) 1994 Jan-Simon Pendry
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)union_subr.c	1.1 (Berkeley) 01/28/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include "union.h" /*<miscfs/union/union.h>*/

static struct union_node *unhead;
static int unvplock;

int
union_init()
{

	unhead = 0;
	unvplock = 0;
}

/*
 * allocate a union_node/vnode pair.  the vnode is
 * referenced and locked.
 *
 * all union_nodes are maintained on a singly-linked
 * list.  new nodes are only allocated when they cannot
 * be found on this list.  entries on the list are
 * removed when the vfs reclaim entry is called.
 *
 * a single lock is kept for the entire list.  this is
 * needed because the getnewvnode() function can block
 * waiting for a vnode to become free, in which case there
 * may be more than one process trying to get the same
 * vnode.  this lock is only taken if we are going to
 * call getnewvnode, since the kernel itself is single-threaded.
 *
 * if an entry is found on the list, then call vget() to
 * take a reference.  this is done because there may be
 * zero references to it and so it needs to removed from
 * the vnode free list.
 */
int
union_allocvp(vpp, mp, dvp, cnp, uppervp, lowervp)
	struct vnode **vpp;
	struct mount *mp;
	struct vnode *dvp;		/* may be null */
	struct componentname *cnp;	/* may be null */
	struct vnode *uppervp;		/* may be null */
	struct vnode *lowervp;		/* may be null */
{
	int error;
	struct union_node *un;
	struct union_node **pp;

loop:
	for (un = unhead; un != 0; un = un->un_next) {
		if ((un->un_lowervp == lowervp ||
		     un->un_lowervp == 0) &&
		    (un->un_uppervp == uppervp ||
		     un->un_uppervp == 0) &&
		    (UNIONTOV(un)->v_mount == mp)) {
			if (vget(un->un_vnode, 1))
				goto loop;
			un->un_lowervp = lowervp;
			un->un_uppervp = uppervp;
			*vpp = un->un_vnode;
			return (0);
		}
	}

	/*
	 * otherwise lock the vp list while we call getnewvnode
	 * since that can block.
	 */ 
	if (unvplock & UN_LOCKED) {
		unvplock |= UN_WANT;
		sleep((caddr_t) &unvplock, PINOD);
		goto loop;
	}
	unvplock |= UN_LOCKED;

	error = getnewvnode(VT_UNION, mp, union_vnodeop_p, vpp);
	if (error)
		goto out;

	MALLOC((*vpp)->v_data, void *, sizeof(struct union_node),
		M_TEMP, M_WAITOK);

	un = VTOUNION(*vpp);
	un->un_next = 0;
	un->un_dirvp = dvp;
	un->un_uppervp = uppervp;
	un->un_lowervp = lowervp;
	un->un_vnode = *vpp;
	un->un_flags = 0;
	if (cnp) {
		un->un_path = malloc(cnp->cn_namelen+1, M_TEMP, M_WAITOK);
		bcopy(cnp->cn_nameptr, un->un_path, cnp->cn_namelen);
		un->un_path[cnp->cn_namelen] = '\0';
	} else {
		un->un_path = 0;
	}

#ifdef DIAGNOSTIC
	un->un_pid = 0;
#endif

	/* add to union vnode list */
	for (pp = &unhead; *pp; pp = &(*pp)->un_next)
		continue;
	*pp = un;

out:
	unvplock &= ~UN_LOCKED;

	if (unvplock & UN_WANT) {
		unvplock &= ~UN_WANT;
		wakeup((caddr_t) &unvplock);
	}

	if (un)
		VOP_LOCK(UNIONTOV(un));

	return (error);
}

int
union_freevp(vp)
	struct vnode *vp;
{
	struct union_node **unpp;
	struct union_node *un = VTOUNION(vp);

	for (unpp = &unhead; *unpp != 0; unpp = &(*unpp)->un_next) {
		if (*unpp == un) {
			*unpp = un->un_next;
			break;
		}
	}

	if (un->un_path)
		FREE(un->un_path, M_TEMP);

	FREE(vp->v_data, M_TEMP);
	vp->v_data = 0;
	return (0);
}
