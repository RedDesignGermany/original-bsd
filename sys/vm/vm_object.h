/* 
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)vm_object.h	7.5 (Berkeley) 02/19/92
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 *	Virtual memory object module definitions.
 */

#ifndef	_VM_OBJECT_
#define	_VM_OBJECT_

#include <vm/vm_pager.h>

/*
 *	Types defined:
 *
 *	vm_object_t		Virtual memory object.
 */

struct vm_object {
	queue_chain_t		memq;		/* Resident memory */
	queue_chain_t		object_list;	/* list of all objects */
	u_short			flags;		/* see below */
	u_short			paging_in_progress; /* Paging (in or out) so
						    don't collapse or destroy */
	simple_lock_data_t	Lock;		/* Synchronization */
	int			LockHolder;
	int			ref_count;	/* How many refs?? */
	vm_size_t		size;		/* Object size */
	int			resident_page_count;
						/* number of resident pages */
	struct vm_object	*copy;		/* Object that holds copies of
						   my changed pages */
	vm_pager_t		pager;		/* Where to get data */
	vm_offset_t		paging_offset;	/* Offset into paging space */
	struct vm_object	*shadow;	/* My shadow */
	vm_offset_t		shadow_offset;	/* Offset in shadow */
	queue_chain_t		cached_list;	/* for persistence */
};
/*
 * Flags
 */
#define OBJ_CANPERSIST	0x0001	/* allow to persist */
#define OBJ_INTERNAL	0x0002	/* internally created object */
#define OBJ_ACTIVE	0x0004	/* used to mark active objects */

typedef struct vm_object	*vm_object_t;

struct vm_object_hash_entry {
	queue_chain_t		hash_links;	/* hash chain links */
	vm_object_t		object;		/* object we represent */
};

typedef struct vm_object_hash_entry	*vm_object_hash_entry_t;

#ifdef	KERNEL
queue_head_t	vm_object_cached_list;	/* list of objects persisting */
int		vm_object_cached;	/* size of cached list */
simple_lock_data_t	vm_cache_lock;	/* lock for object cache */

queue_head_t	vm_object_list;		/* list of allocated objects */
long		vm_object_count;	/* count of all objects */
simple_lock_data_t	vm_object_list_lock;
					/* lock for object list and count */

vm_object_t	kernel_object;		/* the single kernel object */
vm_object_t	kmem_object;

#define	vm_object_cache_lock()		simple_lock(&vm_cache_lock)
#define	vm_object_cache_unlock()	simple_unlock(&vm_cache_lock)
#endif	KERNEL

/*
 *	Declare procedures that operate on VM objects.
 */

void	vm_object_init __P((void));
void	vm_object_terminate __P((vm_object_t));
vm_object_t	vm_object_allocate __P((vm_size_t));
void	vm_object_reference __P((vm_object_t));
void	vm_object_deallocate __P((vm_object_t));
void	vm_object_pmap_copy __P((vm_object_t, vm_offset_t, vm_offset_t));
void	vm_object_pmap_remove __P((vm_object_t, vm_offset_t, vm_offset_t));
void	vm_object_page_remove __P((vm_object_t, vm_offset_t, vm_offset_t));
void	vm_object_shadow __P((vm_object_t *, vm_offset_t *, vm_size_t));
void	vm_object_copy __P((vm_object_t, vm_offset_t, vm_size_t,
			    vm_object_t *, vm_offset_t *, boolean_t *));
void	vm_object_collapse __P((vm_object_t));
vm_object_t	vm_object_lookup __P((vm_pager_t));
void	vm_object_enter __P((vm_object_t, vm_pager_t));
void	vm_object_setpager __P((vm_object_t, vm_pager_t,
				vm_offset_t, boolean_t));
#define	vm_object_cache(pager)	 pager_cache(vm_object_lookup(pager),TRUE)
#define	vm_object_uncache(pager) pager_cache(vm_object_lookup(pager),FALSE)

void	vm_object_cache_clear __P((void));
void	vm_object_print __P((vm_object_t, boolean_t));

void	thread_sleep __P((int, simple_lock_t, boolean_t));
void	thread_wakeup __P((int));

#if	VM_OBJECT_DEBUG
#define	vm_object_lock_init(object)	{ simple_lock_init(&(object)->Lock); (object)->LockHolder = 0; }
#define	vm_object_lock(object)		{ simple_lock(&(object)->Lock); (object)->LockHolder = (int) current_thread(); }
#define	vm_object_unlock(object)	{ (object)->LockHolder = 0; simple_unlock(&(object)->Lock); }
#define	vm_object_lock_try(object)	(simple_lock_try(&(object)->Lock) ? ( ((object)->LockHolder = (int) current_thread()) , TRUE) : FALSE)
#define	vm_object_sleep(event, object, interruptible) \
					{ (object)->LockHolder = 0; thread_sleep((event), &(object)->Lock, (interruptible)); }
#else	VM_OBJECT_DEBUG
#define	vm_object_lock_init(object)	simple_lock_init(&(object)->Lock)
#define	vm_object_lock(object)		simple_lock(&(object)->Lock)
#define	vm_object_unlock(object)	simple_unlock(&(object)->Lock)
#define	vm_object_lock_try(object)	simple_lock_try(&(object)->Lock)
#define	vm_object_sleep(event, object, interruptible) \
					thread_sleep((event), &(object)->Lock, (interruptible))
#endif	VM_OBJECT_DEBUG

#endif	_VM_OBJECT_
