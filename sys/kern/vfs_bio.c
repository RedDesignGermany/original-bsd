/*
 * Copyright (c) 1982, 1986, 1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)vfs_bio.c	7.18 (Berkeley) 12/30/89
 */

#include "param.h"
#include "user.h"
#include "buf.h"
#include "vnode.h"
#include "mount.h"
#include "trace.h"
#include "ucred.h"

/*
 * Read in (if necessary) the block and return a buffer pointer.
 */
bread(vp, blkno, size, cred, bpp)
	struct vnode *vp;
	daddr_t blkno;
	int size;
	struct ucred *cred;
	struct buf **bpp;
{
	register struct buf *bp;

	if (size == 0)
		panic("bread: size 0");
	*bpp = bp = getblk(vp, blkno, size);
	if (bp->b_flags&(B_DONE|B_DELWRI)) {
		trace(TR_BREADHIT, pack(vp->v_mount->m_fsid[0], size), blkno);
		return (0);
	}
	bp->b_flags |= B_READ;
	if (bp->b_bcount > bp->b_bufsize)
		panic("bread");
	if (bp->b_rcred == NOCRED && cred != NOCRED) {
		crhold(cred);
		bp->b_rcred = cred;
	}
	VOP_STRATEGY(bp);
	trace(TR_BREADMISS, pack(vp->v_mount->m_fsid[0], size), blkno);
	u.u_ru.ru_inblock++;		/* pay for read */
	return (biowait(bp));
}

/*
 * Read in the block, like bread, but also start I/O on the
 * read-ahead block (which is not allocated to the caller)
 */
breada(vp, blkno, size, rablkno, rabsize, cred, bpp)
	struct vnode *vp;
	daddr_t blkno; int size;
	daddr_t rablkno; int rabsize;
	struct ucred *cred;
	struct buf **bpp;
{
	register struct buf *bp, *rabp;

	bp = NULL;
	/*
	 * If the block isn't in core, then allocate
	 * a buffer and initiate i/o (getblk checks
	 * for a cache hit).
	 */
	if (!incore(vp, blkno)) {
		*bpp = bp = getblk(vp, blkno, size);
		if ((bp->b_flags&(B_DONE|B_DELWRI)) == 0) {
			bp->b_flags |= B_READ;
			if (bp->b_bcount > bp->b_bufsize)
				panic("breada");
			if (bp->b_rcred == NOCRED && cred != NOCRED) {
				crhold(cred);
				bp->b_rcred = cred;
			}
			VOP_STRATEGY(bp);
			trace(TR_BREADMISS, pack(vp->v_mount->m_fsid[0], size),
			    blkno);
			u.u_ru.ru_inblock++;		/* pay for read */
		} else
			trace(TR_BREADHIT, pack(vp->v_mount->m_fsid[0], size),
			    blkno);
	}

	/*
	 * If there's a read-ahead block, start i/o
	 * on it also (as above).
	 */
	if (rablkno && !incore(vp, rablkno)) {
		rabp = getblk(vp, rablkno, rabsize);
		if (rabp->b_flags & (B_DONE|B_DELWRI)) {
			brelse(rabp);
			trace(TR_BREADHITRA,
			    pack(vp->v_mount->m_fsid[0], rabsize), rablkno);
		} else {
			rabp->b_flags |= B_READ|B_ASYNC;
			if (rabp->b_bcount > rabp->b_bufsize)
				panic("breadrabp");
			if (rabp->b_rcred == NOCRED && cred != NOCRED) {
				crhold(cred);
				rabp->b_rcred = cred;
			}
			VOP_STRATEGY(rabp);
			trace(TR_BREADMISSRA,
			    pack(vp->v_mount->m_fsid[0], rabsize), rablkno);
			u.u_ru.ru_inblock++;		/* pay in advance */
		}
	}

	/*
	 * If block was in core, let bread get it.
	 * If block wasn't in core, then the read was started
	 * above, and just wait for it.
	 */
	if (bp == NULL)
		return (bread(vp, blkno, size, cred, bpp));
	return (biowait(bp));
}

/*
 * Write the buffer, waiting for completion.
 * Then release the buffer.
 */
bwrite(bp)
	register struct buf *bp;
{
	register int flag;
	int error;

	flag = bp->b_flags;
	bp->b_flags &= ~(B_READ | B_DONE | B_ERROR | B_DELWRI);
	if ((flag&B_DELWRI) == 0)
		u.u_ru.ru_oublock++;		/* noone paid yet */
	trace(TR_BWRITE,
	    pack(bp->b_vp->v_mount->m_fsid[0], bp->b_bcount), bp->b_lblkno);
	if (bp->b_bcount > bp->b_bufsize)
		panic("bwrite");
	VOP_STRATEGY(bp);

	/*
	 * If the write was synchronous, then await i/o completion.
	 * If the write was "delayed", then we put the buffer on
	 * the q of blocks awaiting i/o completion status.
	 */
	if ((flag&B_ASYNC) == 0) {
		error = biowait(bp);
		brelse(bp);
	} else if (flag & B_DELWRI) {
		bp->b_flags |= B_AGE;
		error = 0;
	}
	return (error);
}

/*
 * Release the buffer, marking it so that if it is grabbed
 * for another purpose it will be written out before being
 * given up (e.g. when writing a partial block where it is
 * assumed that another write for the same block will soon follow).
 * This can't be done for magtape, since writes must be done
 * in the same order as requested.
 */
bdwrite(bp)
	register struct buf *bp;
{

	if ((bp->b_flags&B_DELWRI) == 0)
		u.u_ru.ru_oublock++;		/* noone paid yet */
	/*
	 * If this is a tape drive, the write must be initiated.
	 */
	if (VOP_IOCTL(bp->b_vp, 0, B_TAPE, 0, NOCRED) == 0) {
		bawrite(bp);
	} else {
		bp->b_flags |= B_DELWRI | B_DONE;
		brelse(bp);
	}
}

/*
 * Release the buffer, start I/O on it, but don't wait for completion.
 */
bawrite(bp)
	register struct buf *bp;
{

	bp->b_flags |= B_ASYNC;
	(void) bwrite(bp);
}

/*
 * Release the buffer, with no I/O implied.
 */
brelse(bp)
	register struct buf *bp;
{
	register struct buf *flist;
	register s;

	trace(TR_BRELSE,
	    pack(bp->b_vp->v_mount->m_fsid[0], bp->b_bufsize), bp->b_lblkno);
	/*
	 * If a process is waiting for the buffer, or
	 * is waiting for a free buffer, awaken it.
	 */
	if (bp->b_flags&B_WANTED)
		wakeup((caddr_t)bp);
	if (bfreelist[0].b_flags&B_WANTED) {
		bfreelist[0].b_flags &= ~B_WANTED;
		wakeup((caddr_t)bfreelist);
	}
	/*
	 * Retry I/O for locked buffers rather than invalidating them.
	 */
	if ((bp->b_flags & B_ERROR) && (bp->b_flags & B_LOCKED))
		bp->b_flags &= ~B_ERROR;

	/*
	 * Disassociate buffers that are no longer valid.
	 */
	if (bp->b_flags & (B_NOCACHE|B_ERROR))
		bp->b_flags |= B_INVAL;
	if ((bp->b_bufsize <= 0) || (bp->b_flags & (B_ERROR|B_INVAL))) {
		if (bp->b_vp)
			brelvp(bp);
		bp->b_flags &= ~B_DELWRI;
	}
	/*
	 * Stick the buffer back on a free list.
	 */
	s = splbio();
	if (bp->b_bufsize <= 0) {
		/* block has no buffer ... put at front of unused buffer list */
		flist = &bfreelist[BQ_EMPTY];
		binsheadfree(bp, flist);
	} else if (bp->b_flags & (B_ERROR|B_INVAL)) {
		/* block has no info ... put at front of most free list */
		flist = &bfreelist[BQ_AGE];
		binsheadfree(bp, flist);
	} else {
		if (bp->b_flags & B_LOCKED)
			flist = &bfreelist[BQ_LOCKED];
		else if (bp->b_flags & B_AGE)
			flist = &bfreelist[BQ_AGE];
		else
			flist = &bfreelist[BQ_LRU];
		binstailfree(bp, flist);
	}
	bp->b_flags &= ~(B_WANTED|B_BUSY|B_ASYNC|B_AGE|B_NOCACHE);
	splx(s);
}

/*
 * See if the block is associated with some buffer
 * (mainly to avoid getting hung up on a wait in breada)
 */
incore(vp, blkno)
	struct vnode *vp;
	daddr_t blkno;
{
	register struct buf *bp;
	register struct buf *dp;

	dp = BUFHASH(vp, blkno);
	for (bp = dp->b_forw; bp != dp; bp = bp->b_forw)
		if (bp->b_lblkno == blkno && bp->b_vp == vp &&
		    (bp->b_flags & B_INVAL) == 0)
			return (1);
	return (0);
}

/*
 * Return a block if it is in memory.
 */
baddr(vp, blkno, size, cred, bpp)
	struct vnode *vp;
	daddr_t blkno;
	int size;
	struct ucred *cred;
	struct buf **bpp;
{

	if (incore(vp, blkno))
		return (bread(vp, blkno, size, cred, bpp));
	*bpp = 0;
	return (0);
}

/*
 * Assign a buffer for the given block.  If the appropriate
 * block is already associated, return it; otherwise search
 * for the oldest non-busy buffer and reassign it.
 *
 * We use splx here because this routine may be called
 * on the interrupt stack during a dump, and we don't
 * want to lower the ipl back to 0.
 */
struct buf *
getblk(vp, blkno, size)
	register struct vnode *vp;
	daddr_t blkno;
	int size;
{
	register struct buf *bp, *dp;
	int s;

	if (size > MAXBSIZE)
		panic("getblk: size too big");
	/*
	 * To prevent overflow of 32-bit ints when converting block
	 * numbers to byte offsets, blknos > 2^32 / DEV_BSIZE are set
	 * to the maximum number that can be converted to a byte offset
	 * without overflow. This is historic code; what bug it fixed,
	 * or whether it is still a reasonable thing to do is open to
	 * dispute. mkm 9/85
	 *
	 * Make it a panic to see if it ever really happens. mkm 11/89
	 */
	if ((unsigned)blkno >= 1 << (sizeof(int)*NBBY-DEV_BSHIFT)) {
		panic("getblk: blkno too big");
		blkno = 1 << ((sizeof(int)*NBBY-DEV_BSHIFT) + 1);
	}
	/*
	 * Search the cache for the block.  If we hit, but
	 * the buffer is in use for i/o, then we wait until
	 * the i/o has completed.
	 */
	dp = BUFHASH(vp, blkno);
loop:
	for (bp = dp->b_forw; bp != dp; bp = bp->b_forw) {
		if (bp->b_lblkno != blkno || bp->b_vp != vp ||
		    bp->b_flags&B_INVAL)
			continue;
		s = splbio();
		if (bp->b_flags&B_BUSY) {
			bp->b_flags |= B_WANTED;
			sleep((caddr_t)bp, PRIBIO+1);
			splx(s);
			goto loop;
		}
		splx(s);
		notavail(bp);
		if (bp->b_bcount != size) {
			printf("getblk: stray size");
			bp->b_flags |= B_INVAL;
			bwrite(bp);
			goto loop;
		}
		bp->b_flags |= B_CACHE;
		return (bp);
	}
	bp = getnewbuf();
	bfree(bp);
	bremhash(bp);
	bgetvp(vp, bp);
	bp->b_lblkno = blkno;
	bp->b_blkno = blkno;
	bp->b_error = 0;
	bp->b_resid = 0;
	binshash(bp, dp);
	brealloc(bp, size);
	return (bp);
}

/*
 * get an empty block,
 * not assigned to any particular device
 */
struct buf *
geteblk(size)
	int size;
{
	register struct buf *bp, *flist;

	if (size > MAXBSIZE)
		panic("geteblk: size too big");
	bp = getnewbuf();
	bp->b_flags |= B_INVAL;
	bfree(bp);
	bremhash(bp);
	flist = &bfreelist[BQ_AGE];
	bp->b_error = 0;
	bp->b_resid = 0;
	binshash(bp, flist);
	brealloc(bp, size);
	return (bp);
}

/*
 * Allocate space associated with a buffer.
 */
brealloc(bp, size)
	register struct buf *bp;
	int size;
{
	daddr_t start, last;
	register struct buf *ep;
	struct buf *dp;
	int s;

	if (size == bp->b_bcount)
		return;
	allocbuf(bp, size);
}

/*
 * Find a buffer which is available for use.
 * Select something from a free list.
 * Preference is to AGE list, then LRU list.
 */
struct buf *
getnewbuf()
{
	register struct buf *bp, *dp;
	register struct ucred *cred;
	int s;

loop:
	s = splbio();
	for (dp = &bfreelist[BQ_AGE]; dp > bfreelist; dp--)
		if (dp->av_forw != dp)
			break;
	if (dp == bfreelist) {		/* no free blocks */
		dp->b_flags |= B_WANTED;
		sleep((caddr_t)dp, PRIBIO+1);
		splx(s);
		goto loop;
	}
	splx(s);
	bp = dp->av_forw;
	notavail(bp);
	if (bp->b_flags & B_DELWRI) {
		(void) bawrite(bp);
		goto loop;
	}
	trace(TR_BRELSE,
	    pack(bp->b_vp->v_mount->m_fsid[0], bp->b_bufsize), bp->b_lblkno);
	if (bp->b_vp)
		brelvp(bp);
	if (bp->b_rcred != NOCRED) {
		cred = bp->b_rcred;
		bp->b_rcred = NOCRED;
		crfree(cred);
	}
	if (bp->b_wcred != NOCRED) {
		cred = bp->b_wcred;
		bp->b_wcred = NOCRED;
		crfree(cred);
	}
	bp->b_flags = B_BUSY;
	return (bp);
}

/*
 * Wait for I/O completion on the buffer; return errors
 * to the user.
 */
biowait(bp)
	register struct buf *bp;
{
	int s;

	s = splbio();
	while ((bp->b_flags & B_DONE) == 0)
		sleep((caddr_t)bp, PRIBIO);
	splx(s);
	/*
	 * Pick up the device's error number and pass it to the user;
	 * if there is an error but the number is 0 set a generalized code.
	 */
	if ((bp->b_flags & B_ERROR) == 0)
		return (0);
	if (bp->b_error)
		return (bp->b_error);
	return (EIO);
}

/*
 * Mark I/O complete on a buffer.
 * If someone should be called, e.g. the pageout
 * daemon, do so.  Otherwise, wake up anyone
 * waiting for it.
 */
biodone(bp)
	register struct buf *bp;
{

	if (bp->b_flags & B_DONE)
		panic("dup biodone");
	bp->b_flags |= B_DONE;
	if ((bp->b_flags & B_READ) == 0)
		bp->b_dirtyoff = bp->b_dirtyend = 0;
	if (bp->b_flags & B_CALL) {
		bp->b_flags &= ~B_CALL;
		(*bp->b_iodone)(bp);
		return;
	}
	if (bp->b_flags&B_ASYNC)
		brelse(bp);
	else {
		bp->b_flags &= ~B_WANTED;
		wakeup((caddr_t)bp);
	}
}

/*
 * Make sure all write-behind blocks associated
 * with mount point are flushed out (from sync).
 */
mntflushbuf(mountp, flags)
	struct mount *mountp;
	int flags;
{
	register struct vnode *vp;
	struct vnode *nvp;

loop:
	for (vp = mountp->m_mounth; vp; vp = nvp) {
		nvp = vp->v_mountf;
		if (vget(vp))
			goto loop;
		vflushbuf(vp, flags);
		vput(vp);
	}
}

/*
 * Flush all dirty buffers associated with a vnode.
 */
vflushbuf(vp, flags)
	register struct vnode *vp;
	int flags;
{
	register struct buf *bp;
	struct buf *nbp;
	int s;

loop:
	s = splbio();
	for (bp = vp->v_blockh; bp; bp = nbp) {
		nbp = bp->b_blockf;
		if ((bp->b_flags & B_BUSY))
			continue;
		if ((bp->b_flags & B_DELWRI) == 0)
			continue;
		splx(s);
		notavail(bp);
		(void) bawrite(bp);
		goto loop;
	}
	splx(s);
	if ((flags & B_SYNC) == 0)
		return;
wloop:
	s = splbio();
	for (bp = vp->v_blockh; bp; bp = nbp) {
		nbp = bp->b_blockf;
		if (bp->b_flags & B_BUSY) {
			bp->b_flags |= B_WANTED;
			sleep((caddr_t)bp, PRIBIO+1);
			splx(s);
			goto wloop;
		}
		if ((bp->b_flags & B_DELWRI)) {
			splx(s);
			goto loop;
		}
	}
	splx(s);
}

/*
 * Invalidate in core blocks belonging to closed or umounted filesystem
 *
 * Go through the list of vnodes associated with the file system;
 * for each vnode invalidate any buffers that it holds. Normally
 * this routine is preceeded by a bflush call, so that on a quiescent
 * filesystem there will be no dirty buffers when we are done. Binval
 * returns the count of dirty buffers when it is finished.
 */
mntinvalbuf(mountp)
	struct mount *mountp;
{
	register struct vnode *vp;
	struct vnode *nvp;
	int dirty = 0;

loop:
	for (vp = mountp->m_mounth; vp; vp = nvp) {
		nvp = vp->v_mountf;
		if (vget(vp))
			goto loop;
		dirty += vinvalbuf(vp, 1);
		vput(vp);
	}
	return (dirty);
}

/*
 * Flush out and invalidate all buffers associated with a vnode.
 * Called with the underlying object locked.
 */
vinvalbuf(vp, save)
	register struct vnode *vp;
	int save;
{
	register struct buf *bp;
	struct buf *nbp;
	int s, dirty = 0;

loop:
	for (bp = vp->v_blockh; bp; bp = nbp) {
		nbp = bp->b_blockf;
		s = splbio();
		if (bp->b_flags & B_BUSY) {
			bp->b_flags |= B_WANTED;
			sleep((caddr_t)bp, PRIBIO+1);
			splx(s);
			goto loop;
		}
		splx(s);
		notavail(bp);
		if (save) {
			if (bp->b_flags & B_DELWRI) {
				dirty++;
				(void) bwrite(bp);
				goto loop;
			}
		}
		bp->b_flags |= B_INVAL;
		brelse(bp);
	}
	if (vp->v_blockh != 0)
		panic("vinvalbuf: flush failed");
	return (dirty);
}

/*
 * Associate a buffer with a vnode.
 */
bgetvp(vp, bp)
	register struct vnode *vp;
	register struct buf *bp;
{

	if (bp->b_vp)
		panic("bgetvp: not free");
	VHOLD(vp);
	bp->b_vp = vp;
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		bp->b_dev = vp->v_rdev;
	else
		bp->b_dev = NODEV;
	/*
	 * Insert onto list for new vnode.
	 */
	if (vp->v_blockh) {
		bp->b_blockf = vp->v_blockh;
		bp->b_blockb = &vp->v_blockh;
		vp->v_blockh->b_blockb = &bp->b_blockf;
		vp->v_blockh = bp;
	} else {
		vp->v_blockh = bp;
		bp->b_blockb = &vp->v_blockh;
		bp->b_blockf = NULL;
	}
}

/*
 * Disassociate a buffer from a vnode.
 */
brelvp(bp)
	register struct buf *bp;
{
	struct buf *bq;
	struct vnode *vp;

	if (bp->b_vp == (struct vnode *) 0)
		panic("brelvp: NULL");
	/*
	 * Delete from old vnode list, if on one.
	 */
	if (bp->b_blockb) {
		if (bq = bp->b_blockf)
			bq->b_blockb = bp->b_blockb;
		*bp->b_blockb = bq;
		bp->b_blockf = NULL;
		bp->b_blockb = NULL;
	}
	vp = bp->b_vp;
	bp->b_vp = (struct vnode *) 0;
	HOLDRELE(vp);
}

/*
 * Reassign a buffer from one vnode to another.
 * Used to assign file specific control information
 * (indirect blocks) to the vnode to which they belong.
 */
reassignbuf(bp, newvp)
	register struct buf *bp;
	register struct vnode *newvp;
{
	register struct buf *bq;

	/*
	 * Delete from old vnode list, if on one.
	 */
	if (bp->b_blockb) {
		if (bq = bp->b_blockf)
			bq->b_blockb = bp->b_blockb;
		*bp->b_blockb = bq;
	}
	/*
	 * Insert onto list for new vnode.
	 */
	if (newvp->v_blockh) {
		bp->b_blockf = newvp->v_blockh;
		bp->b_blockb = &newvp->v_blockh;
		newvp->v_blockh->b_blockb = &bp->b_blockf;
		newvp->v_blockh = bp;
	} else {
		newvp->v_blockh = bp;
		bp->b_blockb = &newvp->v_blockh;
		bp->b_blockf = NULL;
	}
}
