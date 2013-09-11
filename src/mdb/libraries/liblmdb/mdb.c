/** @file mdb.c
 *	@brief memory-mapped database library
 *
 *	A Btree-based database management library modeled loosely on the
 *	BerkeleyDB API, but much simplified.
 */
/*
 * Copyright 2011-2013 Howard Chu, Symas Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 *
 * This code is derived from btree.c written by Martin Hedenfalk.
 *
 * Copyright (c) 2009, 2010 Martin Hedenfalk <martin@bzero.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/uio.h>
#include <sys/mman.h>
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#include <fcntl.h>
#endif

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if !(defined(BYTE_ORDER) || defined(__BYTE_ORDER))
#include <netinet/in.h>
#include <resolv.h>	/* defines BYTE_ORDER on HPUX and Solaris */
#endif

#if defined(__APPLE__) || defined (BSD)
# define MDB_USE_POSIX_SEM	1
# define MDB_FDATASYNC		fsync
#elif defined(ANDROID)
# define MDB_FDATASYNC		fsync
#endif

#ifndef _WIN32
#include <pthread.h>
#ifdef MDB_USE_POSIX_SEM
#include <semaphore.h>
#endif
#endif

#ifdef USE_VALGRIND
#include <valgrind/memcheck.h>
#define VGMEMP_CREATE(h,r,z)    VALGRIND_CREATE_MEMPOOL(h,r,z)
#define VGMEMP_ALLOC(h,a,s) VALGRIND_MEMPOOL_ALLOC(h,a,s)
#define VGMEMP_FREE(h,a) VALGRIND_MEMPOOL_FREE(h,a)
#define VGMEMP_DESTROY(h)	VALGRIND_DESTROY_MEMPOOL(h)
#define VGMEMP_DEFINED(a,s)	VALGRIND_MAKE_MEM_DEFINED(a,s)
#else
#define VGMEMP_CREATE(h,r,z)
#define VGMEMP_ALLOC(h,a,s)
#define VGMEMP_FREE(h,a)
#define VGMEMP_DESTROY(h)
#define VGMEMP_DEFINED(a,s)
#endif

#ifndef BYTE_ORDER
# if (defined(_LITTLE_ENDIAN) || defined(_BIG_ENDIAN)) && !(defined(_LITTLE_ENDIAN) && defined(_BIG_ENDIAN))
/* Solaris just defines one or the other */
#  define LITTLE_ENDIAN	1234
#  define BIG_ENDIAN	4321
#  ifdef _LITTLE_ENDIAN
#   define BYTE_ORDER  LITTLE_ENDIAN
#  else
#   define BYTE_ORDER  BIG_ENDIAN
#  endif
# else
#  define BYTE_ORDER   __BYTE_ORDER
# endif
#endif

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN	__LITTLE_ENDIAN
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN	__BIG_ENDIAN
#endif

#if defined(__i386) || defined(__x86_64) || defined(_M_IX86)
#define MISALIGNED_OK	1
#endif

#include "lmdb.h"
#include "midl.h"

#if (BYTE_ORDER == LITTLE_ENDIAN) == (BYTE_ORDER == BIG_ENDIAN)
# error "Unknown or unsupported endianness (BYTE_ORDER)"
#elif (-6 & 5) || CHAR_BIT != 8 || UINT_MAX < 0xffffffff || ULONG_MAX % 0xFFFF
# error "Two's complement, reasonably sized integer types, please"
#endif

/** @defgroup internal	MDB Internals
 *	@{
 */
/** @defgroup compat	Windows Compatibility Macros
 *	A bunch of macros to minimize the amount of platform-specific ifdefs
 *	needed throughout the rest of the code. When the features this library
 *	needs are similar enough to POSIX to be hidden in a one-or-two line
 *	replacement, this macro approach is used.
 *	@{
 */
#ifdef _WIN32
#define pthread_t	DWORD
#define pthread_mutex_t	HANDLE
#define pthread_key_t	DWORD
#define pthread_self()	GetCurrentThreadId()
#define pthread_key_create(x,y)	\
	((*(x) = TlsAlloc()) == TLS_OUT_OF_INDEXES ? ErrCode() : 0)
#define pthread_key_delete(x)	TlsFree(x)
#define pthread_getspecific(x)	TlsGetValue(x)
#define pthread_setspecific(x,y)	(TlsSetValue(x,y) ? 0 : ErrCode())
#define pthread_mutex_unlock(x)	ReleaseMutex(x)
#define pthread_mutex_lock(x)	WaitForSingleObject(x, INFINITE)
#define LOCK_MUTEX_R(env)	pthread_mutex_lock((env)->me_rmutex)
#define UNLOCK_MUTEX_R(env)	pthread_mutex_unlock((env)->me_rmutex)
#define LOCK_MUTEX_W(env)	pthread_mutex_lock((env)->me_wmutex)
#define UNLOCK_MUTEX_W(env)	pthread_mutex_unlock((env)->me_wmutex)
#define getpid()	GetCurrentProcessId()
#define	MDB_FDATASYNC(fd)	(!FlushFileBuffers(fd))
#define	MDB_MSYNC(addr,len,flags)	(!FlushViewOfFile(addr,len))
#define	ErrCode()	GetLastError()
#define GET_PAGESIZE(x) {SYSTEM_INFO si; GetSystemInfo(&si); (x) = si.dwPageSize;}
#define	close(fd)	(CloseHandle(fd) ? 0 : -1)
#define	munmap(ptr,len)	UnmapViewOfFile(ptr)
#else

#ifdef MDB_USE_POSIX_SEM

#define LOCK_MUTEX_R(env)	mdb_sem_wait((env)->me_rmutex)
#define UNLOCK_MUTEX_R(env)	sem_post((env)->me_rmutex)
#define LOCK_MUTEX_W(env)	mdb_sem_wait((env)->me_wmutex)
#define UNLOCK_MUTEX_W(env)	sem_post((env)->me_wmutex)

static int
mdb_sem_wait(sem_t *sem)
{
   int rc;
   while ((rc = sem_wait(sem)) && (rc = errno) == EINTR) ;
   return rc;
}

#else
	/** Lock the reader mutex.
	 */
#define LOCK_MUTEX_R(env)	pthread_mutex_lock(&(env)->me_txns->mti_mutex)
	/** Unlock the reader mutex.
	 */
#define UNLOCK_MUTEX_R(env)	pthread_mutex_unlock(&(env)->me_txns->mti_mutex)

	/** Lock the writer mutex.
	 *	Only a single write transaction is allowed at a time. Other writers
	 *	will block waiting for this mutex.
	 */
#define LOCK_MUTEX_W(env)	pthread_mutex_lock(&(env)->me_txns->mti_wmutex)
	/** Unlock the writer mutex.
	 */
#define UNLOCK_MUTEX_W(env)	pthread_mutex_unlock(&(env)->me_txns->mti_wmutex)
#endif	/* MDB_USE_POSIX_SEM */

	/** Get the error code for the last failed system function.
	 */
#define	ErrCode()	errno

	/** An abstraction for a file handle.
	 *	On POSIX systems file handles are small integers. On Windows
	 *	they're opaque pointers.
	 */
#define	HANDLE	int

	/**	A value for an invalid file handle.
	 *	Mainly used to initialize file variables and signify that they are
	 *	unused.
	 */
#define INVALID_HANDLE_VALUE	(-1)

	/** Get the size of a memory page for the system.
	 *	This is the basic size that the platform's memory manager uses, and is
	 *	fundamental to the use of memory-mapped files.
	 */
#define	GET_PAGESIZE(x)	((x) = sysconf(_SC_PAGE_SIZE))
#endif

#if defined(_WIN32) || defined(MDB_USE_POSIX_SEM)
#define MNAME_LEN	32
#else
#define MNAME_LEN	(sizeof(pthread_mutex_t))
#endif

/** @} */

#ifndef _WIN32
/**	A flag for opening a file and requesting synchronous data writes.
 *	This is only used when writing a meta page. It's not strictly needed;
 *	we could just do a normal write and then immediately perform a flush.
 *	But if this flag is available it saves us an extra system call.
 *
 *	@note If O_DSYNC is undefined but exists in /usr/include,
 * preferably set some compiler flag to get the definition.
 * Otherwise compile with the less efficient -DMDB_DSYNC=O_SYNC.
 */
#ifndef MDB_DSYNC
# define MDB_DSYNC	O_DSYNC
#endif
#endif

/** Function for flushing the data of a file. Define this to fsync
 *	if fdatasync() is not supported.
 */
#ifndef MDB_FDATASYNC
# define MDB_FDATASYNC	fdatasync
#endif

#ifndef MDB_MSYNC
# define MDB_MSYNC(addr,len,flags)	msync(addr,len,flags)
#endif

#ifndef MS_SYNC
#define	MS_SYNC	1
#endif

#ifndef MS_ASYNC
#define	MS_ASYNC	0
#endif

	/** A page number in the database.
	 *	Note that 64 bit page numbers are overkill, since pages themselves
	 *	already represent 12-13 bits of addressable memory, and the OS will
	 *	always limit applications to a maximum of 63 bits of address space.
	 *
	 *	@note In the #MDB_node structure, we only store 48 bits of this value,
	 *	which thus limits us to only 60 bits of addressable data.
	 */
typedef MDB_ID	pgno_t;

	/** A transaction ID.
	 *	See struct MDB_txn.mt_txnid for details.
	 */
typedef MDB_ID	txnid_t;

/** @defgroup debug	Debug Macros
 *	@{
 */
#ifndef MDB_DEBUG
	/**	Enable debug output.
	 *	Set this to 1 for copious tracing. Set to 2 to add dumps of all IDLs
	 *	read from and written to the database (used for free space management).
	 */
#define MDB_DEBUG 0
#endif

#if !(__STDC_VERSION__ >= 199901L || defined(__GNUC__))
# undef  MDB_DEBUG
# define MDB_DEBUG	0
# define DPRINTF	(void)	/* Vararg macros may be unsupported */
#elif MDB_DEBUG
static int mdb_debug;
static txnid_t mdb_debug_start;

	/**	Print a debug message with printf formatting. */
# define DPRINTF(fmt, ...)	/**< Requires 2 or more args */ \
	((void) ((mdb_debug) && \
	 fprintf(stderr, "%s:%d " fmt "\n", __func__, __LINE__, __VA_ARGS__)))
#else
# define DPRINTF(fmt, ...)	((void) 0)
# define MDB_DEBUG_SKIP
#endif
	/**	Print a debug string.
	 *	The string is printed literally, with no format processing.
	 */
#define DPUTS(arg)	DPRINTF("%s", arg)
/** @} */

	/** A default memory page size.
	 *	The actual size is platform-dependent, but we use this for
	 *	boot-strapping. We probably should not be using this any more.
	 *	The #GET_PAGESIZE() macro is used to get the actual size.
	 *
	 *	Note that we don't currently support Huge pages. On Linux,
	 *	regular data files cannot use Huge pages, and in general
	 *	Huge pages aren't actually pageable. We rely on the OS
	 *	demand-pager to read our data and page it out when memory
	 *	pressure from other processes is high. So until OSs have
	 *	actual paging support for Huge pages, they're not viable.
	 */
#define MDB_PAGESIZE	 4096

	/** The minimum number of keys required in a database page.
	 *	Setting this to a larger value will place a smaller bound on the
	 *	maximum size of a data item. Data items larger than this size will
	 *	be pushed into overflow pages instead of being stored directly in
	 *	the B-tree node. This value used to default to 4. With a page size
	 *	of 4096 bytes that meant that any item larger than 1024 bytes would
	 *	go into an overflow page. That also meant that on average 2-3KB of
	 *	each overflow page was wasted space. The value cannot be lower than
	 *	2 because then there would no longer be a tree structure. With this
	 *	value, items larger than 2KB will go into overflow pages, and on
	 *	average only 1KB will be wasted.
	 */
#define MDB_MINKEYS	 2

	/**	A stamp that identifies a file as an MDB file.
	 *	There's nothing special about this value other than that it is easily
	 *	recognizable, and it will reflect any byte order mismatches.
	 */
#define MDB_MAGIC	 0xBEEFC0DE

	/**	The version number for a database's datafile format. */
#define MDB_DATA_VERSION	 1
	/**	The version number for a database's lockfile format. */
#define MDB_LOCK_VERSION	 1

	/**	@brief The maximum size of a key in the database.
	 *
	 *	The library rejects bigger keys, and cannot deal with records
	 *	with bigger keys stored by a library with bigger max keysize.
	 *
	 *	We require that keys all fit onto a regular page. This limit
	 *	could be raised a bit further if needed; to something just
	 *	under #MDB_PAGESIZE / #MDB_MINKEYS.
	 *
	 *	Note that data items in an #MDB_DUPSORT database are actually keys
	 *	of a subDB, so they're also limited to this size.
	 */
#ifndef MDB_MAXKEYSIZE
#define MDB_MAXKEYSIZE	 511
#endif

	/**	@brief The maximum size of a data item.
	 *
	 *	We only store a 32 bit value for node sizes.
	 */
#define MAXDATASIZE	0xffffffffUL

#if MDB_DEBUG
	/**	A key buffer.
	 *	@ingroup debug
	 *	This is used for printing a hex dump of a key's contents.
	 */
#define DKBUF	char kbuf[(MDB_MAXKEYSIZE*2+1)]
	/**	Display a key in hex.
	 *	@ingroup debug
	 *	Invoke a function to display a key in hex.
	 */
#define	DKEY(x)	mdb_dkey(x, kbuf)
#else
#define	DKBUF	do { } while (0)	/* so we can put ';' after */
#define DKEY(x)	0
#endif

	/** An invalid page number.
	 *	Mainly used to denote an empty tree.
	 */
#define P_INVALID	 (~(pgno_t)0)

	/** Test if the flags \b f are set in a flag word \b w. */
#define F_ISSET(w, f)	 (((w) & (f)) == (f))

	/**	Used for offsets within a single page.
	 *	Since memory pages are typically 4 or 8KB in size, 12-13 bits,
	 *	this is plenty.
	 */
typedef uint16_t	 indx_t;

	/**	Default size of memory map.
	 *	This is certainly too small for any actual applications. Apps should always set
	 *	the size explicitly using #mdb_env_set_mapsize().
	 */
#define DEFAULT_MAPSIZE	1048576

/**	@defgroup readers	Reader Lock Table
 *	Readers don't acquire any locks for their data access. Instead, they
 *	simply record their transaction ID in the reader table. The reader
 *	mutex is needed just to find an empty slot in the reader table. The
 *	slot's address is saved in thread-specific data so that subsequent read
 *	transactions started by the same thread need no further locking to proceed.
 *
 *	If #MDB_NOTLS is set, the slot address is not saved in thread-specific data.
 *
 *	No reader table is used if the database is on a read-only filesystem.
 *
 *	Since the database uses multi-version concurrency control, readers don't
 *	actually need any locking. This table is used to keep track of which
 *	readers are using data from which old transactions, so that we'll know
 *	when a particular old transaction is no longer in use. Old transactions
 *	that have discarded any data pages can then have those pages reclaimed
 *	for use by a later write transaction.
 *
 *	The lock table is constructed such that reader slots are aligned with the
 *	processor's cache line size. Any slot is only ever used by one thread.
 *	This alignment guarantees that there will be no contention or cache
 *	thrashing as threads update their own slot info, and also eliminates
 *	any need for locking when accessing a slot.
 *
 *	A writer thread will scan every slot in the table to determine the oldest
 *	outstanding reader transaction. Any freed pages older than this will be
 *	reclaimed by the writer. The writer doesn't use any locks when scanning
 *	this table. This means that there's no guarantee that the writer will
 *	see the most up-to-date reader info, but that's not required for correct
 *	operation - all we need is to know the upper bound on the oldest reader,
 *	we don't care at all about the newest reader. So the only consequence of
 *	reading stale information here is that old pages might hang around a
 *	while longer before being reclaimed. That's actually good anyway, because
 *	the longer we delay reclaiming old pages, the more likely it is that a
 *	string of contiguous pages can be found after coalescing old pages from
 *	many old transactions together.
 *	@{
 */
	/**	Number of slots in the reader table.
	 *	This value was chosen somewhat arbitrarily. 126 readers plus a
	 *	couple mutexes fit exactly into 8KB on my development machine.
	 *	Applications should set the table size using #mdb_env_set_maxreaders().
	 */
#define DEFAULT_READERS	126

	/**	The size of a CPU cache line in bytes. We want our lock structures
	 *	aligned to this size to avoid false cache line sharing in the
	 *	lock table.
	 *	This value works for most CPUs. For Itanium this should be 128.
	 */
#ifndef CACHELINE
#define CACHELINE	64
#endif

	/**	The information we store in a single slot of the reader table.
	 *	In addition to a transaction ID, we also record the process and
	 *	thread ID that owns a slot, so that we can detect stale information,
	 *	e.g. threads or processes that went away without cleaning up.
	 *	@note We currently don't check for stale records. We simply re-init
	 *	the table when we know that we're the only process opening the
	 *	lock file.
	 */
typedef struct MDB_rxbody {
	/**	Current Transaction ID when this transaction began, or (txnid_t)-1.
	 *	Multiple readers that start at the same time will probably have the
	 *	same ID here. Again, it's not important to exclude them from
	 *	anything; all we need to know is which version of the DB they
	 *	started from so we can avoid overwriting any data used in that
	 *	particular version.
	 */
	txnid_t		mrb_txnid;
	/** The process ID of the process owning this reader txn. */
	pid_t		mrb_pid;
	/** The thread ID of the thread owning this txn. */
	pthread_t	mrb_tid;
} MDB_rxbody;

	/** The actual reader record, with cacheline padding. */
typedef struct MDB_reader {
	union {
		MDB_rxbody mrx;
		/** shorthand for mrb_txnid */
#define	mr_txnid	mru.mrx.mrb_txnid
#define	mr_pid	mru.mrx.mrb_pid
#define	mr_tid	mru.mrx.mrb_tid
		/** cache line alignment */
		char pad[(sizeof(MDB_rxbody)+CACHELINE-1) & ~(CACHELINE-1)];
	} mru;
} MDB_reader;

	/** The header for the reader table.
	 *	The table resides in a memory-mapped file. (This is a different file
	 *	than is used for the main database.)
	 *
	 *	For POSIX the actual mutexes reside in the shared memory of this
	 *	mapped file. On Windows, mutexes are named objects allocated by the
	 *	kernel; we store the mutex names in this mapped file so that other
	 *	processes can grab them. This same approach is also used on
	 *	MacOSX/Darwin (using named semaphores) since MacOSX doesn't support
	 *	process-shared POSIX mutexes. For these cases where a named object
	 *	is used, the object name is derived from a 64 bit FNV hash of the
	 *	environment pathname. As such, naming collisions are extremely
	 *	unlikely. If a collision occurs, the results are unpredictable.
	 */
typedef struct MDB_txbody {
		/** Stamp identifying this as an MDB file. It must be set
		 *	to #MDB_MAGIC. */
	uint32_t	mtb_magic;
		/** Version number of this lock file. Must be set to #MDB_LOCK_VERSION. */
	uint32_t	mtb_version;
#if defined(_WIN32) || defined(MDB_USE_POSIX_SEM)
	char	mtb_rmname[MNAME_LEN];
#else
		/** Mutex protecting access to this table.
		 *	This is the reader lock that #LOCK_MUTEX_R acquires.
		 */
	pthread_mutex_t	mtb_mutex;
#endif
		/**	The ID of the last transaction committed to the database.
		 *	This is recorded here only for convenience; the value can always
		 *	be determined by reading the main database meta pages.
		 */
	txnid_t		mtb_txnid;
		/** The number of slots that have been used in the reader table.
		 *	This always records the maximum count, it is not decremented
		 *	when readers release their slots.
		 */
	unsigned	mtb_numreaders;
} MDB_txbody;

	/** The actual reader table definition. */
typedef struct MDB_txninfo {
	union {
		MDB_txbody mtb;
#define mti_magic	mt1.mtb.mtb_magic
#define mti_version	mt1.mtb.mtb_version
#define mti_mutex	mt1.mtb.mtb_mutex
#define mti_rmname	mt1.mtb.mtb_rmname
#define mti_txnid	mt1.mtb.mtb_txnid
#define mti_numreaders	mt1.mtb.mtb_numreaders
		char pad[(sizeof(MDB_txbody)+CACHELINE-1) & ~(CACHELINE-1)];
	} mt1;
	union {
#if defined(_WIN32) || defined(MDB_USE_POSIX_SEM)
		char mt2_wmname[MNAME_LEN];
#define	mti_wmname	mt2.mt2_wmname
#else
		pthread_mutex_t	mt2_wmutex;
#define mti_wmutex	mt2.mt2_wmutex
#endif
		char pad[(MNAME_LEN+CACHELINE-1) & ~(CACHELINE-1)];
	} mt2;
	MDB_reader	mti_readers[1];
} MDB_txninfo;
/** @} */

/** Common header for all page types.
 * Overflow records occupy a number of contiguous pages with no
 * headers on any page after the first.
 */
typedef struct MDB_page {
#define	mp_pgno	mp_p.p_pgno
#define	mp_next	mp_p.p_next
	union {
		pgno_t		p_pgno;	/**< page number */
		void *		p_next;	/**< for in-memory list of freed structs */
	} mp_p;
	uint16_t	mp_pad;
/**	@defgroup mdb_page	Page Flags
 *	@ingroup internal
 *	Flags for the page headers.
 *	@{
 */
#define	P_BRANCH	 0x01		/**< branch page */
#define	P_LEAF		 0x02		/**< leaf page */
#define	P_OVERFLOW	 0x04		/**< overflow page */
#define	P_META		 0x08		/**< meta page */
#define	P_DIRTY		 0x10		/**< dirty page */
#define	P_LEAF2		 0x20		/**< for #MDB_DUPFIXED records */
#define	P_SUBP		 0x40		/**< for #MDB_DUPSORT sub-pages */
#define	P_KEEP		 0x8000		/**< leave this page alone during spill */
/** @} */
	uint16_t	mp_flags;		/**< @ref mdb_page */
#define mp_lower	mp_pb.pb.pb_lower
#define mp_upper	mp_pb.pb.pb_upper
#define mp_pages	mp_pb.pb_pages
	union {
		struct {
			indx_t		pb_lower;		/**< lower bound of free space */
			indx_t		pb_upper;		/**< upper bound of free space */
		} pb;
		uint32_t	pb_pages;	/**< number of overflow pages */
	} mp_pb;
	indx_t		mp_ptrs[1];		/**< dynamic size */
} MDB_page;

	/** Size of the page header, excluding dynamic data at the end */
#define PAGEHDRSZ	 ((unsigned) offsetof(MDB_page, mp_ptrs))

	/** Address of first usable data byte in a page, after the header */
#define METADATA(p)	 ((void *)((char *)(p) + PAGEHDRSZ))

	/** Number of nodes on a page */
#define NUMKEYS(p)	 (((p)->mp_lower - PAGEHDRSZ) >> 1)

	/** The amount of space remaining in the page */
#define SIZELEFT(p)	 (indx_t)((p)->mp_upper - (p)->mp_lower)

	/** The percentage of space used in the page, in tenths of a percent. */
#define PAGEFILL(env, p) (1000L * ((env)->me_psize - PAGEHDRSZ - SIZELEFT(p)) / \
				((env)->me_psize - PAGEHDRSZ))
	/** The minimum page fill factor, in tenths of a percent.
	 *	Pages emptier than this are candidates for merging.
	 */
#define FILL_THRESHOLD	 250

	/** Test if a page is a leaf page */
#define IS_LEAF(p)	 F_ISSET((p)->mp_flags, P_LEAF)
	/** Test if a page is a LEAF2 page */
#define IS_LEAF2(p)	 F_ISSET((p)->mp_flags, P_LEAF2)
	/** Test if a page is a branch page */
#define IS_BRANCH(p)	 F_ISSET((p)->mp_flags, P_BRANCH)
	/** Test if a page is an overflow page */
#define IS_OVERFLOW(p)	 F_ISSET((p)->mp_flags, P_OVERFLOW)
	/** Test if a page is a sub page */
#define IS_SUBP(p)	 F_ISSET((p)->mp_flags, P_SUBP)

	/** The number of overflow pages needed to store the given size. */
#define OVPAGES(size, psize)	((PAGEHDRSZ-1 + (size)) / (psize) + 1)

	/** Header for a single key/data pair within a page.
	 * We guarantee 2-byte alignment for nodes.
	 */
typedef struct MDB_node {
	/** lo and hi are used for data size on leaf nodes and for
	 * child pgno on branch nodes. On 64 bit platforms, flags
	 * is also used for pgno. (Branch nodes have no flags).
	 * They are in host byte order in case that lets some
	 * accesses be optimized into a 32-bit word access.
	 */
#define mn_lo mn_offset[BYTE_ORDER!=LITTLE_ENDIAN]
#define mn_hi mn_offset[BYTE_ORDER==LITTLE_ENDIAN] /**< part of dsize or pgno */
	unsigned short	mn_offset[2];	/**< storage for #mn_lo and #mn_hi */
/** @defgroup mdb_node Node Flags
 *	@ingroup internal
 *	Flags for node headers.
 *	@{
 */
#define F_BIGDATA	 0x01			/**< data put on overflow page */
#define F_SUBDATA	 0x02			/**< data is a sub-database */
#define F_DUPDATA	 0x04			/**< data has duplicates */

/** valid flags for #mdb_node_add() */
#define	NODE_ADD_FLAGS	(F_DUPDATA|F_SUBDATA|MDB_RESERVE|MDB_APPEND)

/** @} */
	unsigned short	mn_flags;		/**< @ref mdb_node */
	unsigned short	mn_ksize;		/**< key size */
	char		mn_data[1];			/**< key and data are appended here */
} MDB_node;

	/** Size of the node header, excluding dynamic data at the end */
#define NODESIZE	 offsetof(MDB_node, mn_data)

	/** Bit position of top word in page number, for shifting mn_flags */
#define PGNO_TOPWORD ((pgno_t)-1 > 0xffffffffu ? 32 : 0)

	/** Size of a node in a branch page with a given key.
	 *	This is just the node header plus the key, there is no data.
	 */
#define INDXSIZE(k)	 (NODESIZE + ((k) == NULL ? 0 : (k)->mv_size))

	/** Size of a node in a leaf page with a given key and data.
	 *	This is node header plus key plus data size.
	 */
#define LEAFSIZE(k, d)	 (NODESIZE + (k)->mv_size + (d)->mv_size)

	/** Address of node \b i in page \b p */
#define NODEPTR(p, i)	 ((MDB_node *)((char *)(p) + (p)->mp_ptrs[i]))

	/** Address of the key for the node */
#define NODEKEY(node)	 (void *)((node)->mn_data)

	/** Address of the data for a node */
#define NODEDATA(node)	 (void *)((char *)(node)->mn_data + (node)->mn_ksize)

	/** Get the page number pointed to by a branch node */
#define NODEPGNO(node) \
	((node)->mn_lo | ((pgno_t) (node)->mn_hi << 16) | \
	 (PGNO_TOPWORD ? ((pgno_t) (node)->mn_flags << PGNO_TOPWORD) : 0))
	/** Set the page number in a branch node */
#define SETPGNO(node,pgno)	do { \
	(node)->mn_lo = (pgno) & 0xffff; (node)->mn_hi = (pgno) >> 16; \
	if (PGNO_TOPWORD) (node)->mn_flags = (pgno) >> PGNO_TOPWORD; } while(0)

	/** Get the size of the data in a leaf node */
#define NODEDSZ(node)	 ((node)->mn_lo | ((unsigned)(node)->mn_hi << 16))
	/** Set the size of the data for a leaf node */
#define SETDSZ(node,size)	do { \
	(node)->mn_lo = (size) & 0xffff; (node)->mn_hi = (size) >> 16;} while(0)
	/** The size of a key in a node */
#define NODEKSZ(node)	 ((node)->mn_ksize)

	/** Copy a page number from src to dst */
#ifdef MISALIGNED_OK
#define COPY_PGNO(dst,src)	dst = src
#else
#if SIZE_MAX > 4294967295UL
#define COPY_PGNO(dst,src)	do { \
	unsigned short *s, *d;	\
	s = (unsigned short *)&(src);	\
	d = (unsigned short *)&(dst);	\
	*d++ = *s++;	\
	*d++ = *s++;	\
	*d++ = *s++;	\
	*d = *s;	\
} while (0)
#else
#define COPY_PGNO(dst,src)	do { \
	unsigned short *s, *d;	\
	s = (unsigned short *)&(src);	\
	d = (unsigned short *)&(dst);	\
	*d++ = *s++;	\
	*d = *s;	\
} while (0)
#endif
#endif
	/** The address of a key in a LEAF2 page.
	 *	LEAF2 pages are used for #MDB_DUPFIXED sorted-duplicate sub-DBs.
	 *	There are no node headers, keys are stored contiguously.
	 */
#define LEAF2KEY(p, i, ks)	((char *)(p) + PAGEHDRSZ + ((i)*(ks)))

	/** Set the \b node's key into \b key, if requested. */
#define MDB_GET_KEY(node, key)	{ if ((key) != NULL) { \
	(key)->mv_size = NODEKSZ(node); (key)->mv_data = NODEKEY(node); } }

	/** Information about a single database in the environment. */
typedef struct MDB_db {
	uint32_t	md_pad;		/**< also ksize for LEAF2 pages */
	uint16_t	md_flags;	/**< @ref mdb_dbi_open */
	uint16_t	md_depth;	/**< depth of this tree */
	pgno_t		md_branch_pages;	/**< number of internal pages */
	pgno_t		md_leaf_pages;		/**< number of leaf pages */
	pgno_t		md_overflow_pages;	/**< number of overflow pages */
	size_t		md_entries;		/**< number of data items */
	pgno_t		md_root;		/**< the root page of this tree */
} MDB_db;

	/** mdb_dbi_open flags */
#define MDB_VALID	0x8000		/**< DB handle is valid, for me_dbflags */
#define PERSISTENT_FLAGS	(0xffff & ~(MDB_VALID))
#define VALID_FLAGS	(MDB_REVERSEKEY|MDB_DUPSORT|MDB_INTEGERKEY|MDB_DUPFIXED|\
	MDB_INTEGERDUP|MDB_REVERSEDUP|MDB_CREATE)

	/** Handle for the DB used to track free pages. */
#define	FREE_DBI	0
	/** Handle for the default DB. */
#define	MAIN_DBI	1

	/** Meta page content. */
typedef struct MDB_meta {
		/** Stamp identifying this as an MDB file. It must be set
		 *	to #MDB_MAGIC. */
	uint32_t	mm_magic;
		/** Version number of this lock file. Must be set to #MDB_DATA_VERSION. */
	uint32_t	mm_version;
	void		*mm_address;		/**< address for fixed mapping */
	size_t		mm_mapsize;			/**< size of mmap region */
	MDB_db		mm_dbs[2];			/**< first is free space, 2nd is main db */
	/** The size of pages used in this DB */
#define	mm_psize	mm_dbs[0].md_pad
	/** Any persistent environment flags. @ref mdb_env */
#define	mm_flags	mm_dbs[0].md_flags
	pgno_t		mm_last_pg;			/**< last used page in file */
	txnid_t		mm_txnid;			/**< txnid that committed this page */
} MDB_meta;

	/** Buffer for a stack-allocated dirty page.
	 *	The members define size and alignment, and silence type
	 *	aliasing warnings.  They are not used directly; that could
	 *	mean incorrectly using several union members in parallel.
	 */
typedef union MDB_pagebuf {
	char		mb_raw[MDB_PAGESIZE];
	MDB_page	mb_page;
	struct {
		char		mm_pad[PAGEHDRSZ];
		MDB_meta	mm_meta;
	} mb_metabuf;
} MDB_pagebuf;

	/** Auxiliary DB info.
	 *	The information here is mostly static/read-only. There is
	 *	only a single copy of this record in the environment.
	 */
typedef struct MDB_dbx {
	MDB_val		md_name;		/**< name of the database */
	MDB_cmp_func	*md_cmp;	/**< function for comparing keys */
	MDB_cmp_func	*md_dcmp;	/**< function for comparing data items */
	MDB_rel_func	*md_rel;	/**< user relocate function */
	void		*md_relctx;		/**< user-provided context for md_rel */
} MDB_dbx;

	/** A database transaction.
	 *	Every operation requires a transaction handle.
	 */
struct MDB_txn {
	MDB_txn		*mt_parent;		/**< parent of a nested txn */
	MDB_txn		*mt_child;		/**< nested txn under this txn */
	pgno_t		mt_next_pgno;	/**< next unallocated page */
	/** The ID of this transaction. IDs are integers incrementing from 1.
	 *	Only committed write transactions increment the ID. If a transaction
	 *	aborts, the ID may be re-used by the next writer.
	 */
	txnid_t		mt_txnid;
	MDB_env		*mt_env;		/**< the DB environment */
	/** The list of pages that became unused during this transaction.
	 */
	MDB_IDL		mt_free_pgs;
	/** The list of dirty pages we temporarily wrote to disk
	 *	because the dirty list was full.
	 */
	MDB_IDL		mt_spill_pgs;
	union {
		MDB_ID2L	dirty_list;	/**< for write txns: modified pages */
		MDB_reader	*reader;	/**< this thread's reader table slot or NULL */
	} mt_u;
	/** Array of records for each DB known in the environment. */
	MDB_dbx		*mt_dbxs;
	/** Array of MDB_db records for each known DB */
	MDB_db		*mt_dbs;
/** @defgroup mt_dbflag	Transaction DB Flags
 *	@ingroup internal
 * @{
 */
#define DB_DIRTY	0x01		/**< DB was written in this txn */
#define DB_STALE	0x02		/**< DB record is older than txnID */
#define DB_NEW		0x04		/**< DB handle opened in this txn */
#define DB_VALID	0x08		/**< DB handle is valid, see also #MDB_VALID */
/** @} */
	/** In write txns, array of cursors for each DB */
	MDB_cursor	**mt_cursors;
	/** Array of flags for each DB */
	unsigned char	*mt_dbflags;
	/**	Number of DB records in use. This number only ever increments;
	 *	we don't decrement it when individual DB handles are closed.
	 */
	MDB_dbi		mt_numdbs;

/** @defgroup mdb_txn	Transaction Flags
 *	@ingroup internal
 *	@{
 */
#define MDB_TXN_RDONLY		0x01		/**< read-only transaction */
#define MDB_TXN_ERROR		0x02		/**< an error has occurred */
#define MDB_TXN_DIRTY		0x04		/**< must write, even if dirty list is empty */
#define MDB_TXN_SPILLS		0x08		/**< txn or a parent has spilled pages */
/** @} */
	unsigned int	mt_flags;		/**< @ref mdb_txn */
	/** dirty_list maxsize - # of allocated pages allowed, including in parent txns */
	unsigned int	mt_dirty_room;
	/** Tracks which of the two meta pages was used at the start
	 * 	of this transaction.
	 */
	unsigned int	mt_toggle;
};

/** Enough space for 2^32 nodes with minimum of 2 keys per node. I.e., plenty.
 * At 4 keys per node, enough for 2^64 nodes, so there's probably no need to
 * raise this on a 64 bit machine.
 */
#define CURSOR_STACK		 32

struct MDB_xcursor;

	/** Cursors are used for all DB operations */
struct MDB_cursor {
	/** Next cursor on this DB in this txn */
	MDB_cursor	*mc_next;
	/** Backup of the original cursor if this cursor is a shadow */
	MDB_cursor	*mc_backup;
	/** Context used for databases with #MDB_DUPSORT, otherwise NULL */
	struct MDB_xcursor	*mc_xcursor;
	/** The transaction that owns this cursor */
	MDB_txn		*mc_txn;
	/** The database handle this cursor operates on */
	MDB_dbi		mc_dbi;
	/** The database record for this cursor */
	MDB_db		*mc_db;
	/** The database auxiliary record for this cursor */
	MDB_dbx		*mc_dbx;
	/** The @ref mt_dbflag for this database */
	unsigned char	*mc_dbflag;
	unsigned short 	mc_snum;	/**< number of pushed pages */
	unsigned short	mc_top;		/**< index of top page, normally mc_snum-1 */
/** @defgroup mdb_cursor	Cursor Flags
 *	@ingroup internal
 *	Cursor state flags.
 *	@{
 */
#define C_INITIALIZED	0x01	/**< cursor has been initialized and is valid */
#define C_EOF	0x02			/**< No more data */
#define C_SUB	0x04			/**< Cursor is a sub-cursor */
#define C_SPLITTING	0x20		/**< Cursor is in page_split */
#define C_UNTRACK	0x40		/**< Un-track cursor when closing */
/** @} */
	unsigned int	mc_flags;	/**< @ref mdb_cursor */
	MDB_page	*mc_pg[CURSOR_STACK];	/**< stack of pushed pages */
	indx_t		mc_ki[CURSOR_STACK];	/**< stack of page indices */
};

	/** Context for sorted-dup records.
	 *	We could have gone to a fully recursive design, with arbitrarily
	 *	deep nesting of sub-databases. But for now we only handle these
	 *	levels - main DB, optional sub-DB, sorted-duplicate DB.
	 */
typedef struct MDB_xcursor {
	/** A sub-cursor for traversing the Dup DB */
	MDB_cursor mx_cursor;
	/** The database record for this Dup DB */
	MDB_db	mx_db;
	/**	The auxiliary DB record for this Dup DB */
	MDB_dbx	mx_dbx;
	/** The @ref mt_dbflag for this Dup DB */
	unsigned char mx_dbflag;
} MDB_xcursor;

	/** State of FreeDB old pages, stored in the MDB_env */
typedef struct MDB_pgstate {
	pgno_t		*mf_pghead;	/**< Reclaimed freeDB pages, or NULL before use */
	txnid_t		mf_pglast;	/**< ID of last used record, or 0 if !mf_pghead */
} MDB_pgstate;

	/** The database environment. */
struct MDB_env {
	HANDLE		me_fd;		/**< The main data file */
	HANDLE		me_lfd;		/**< The lock file */
	HANDLE		me_mfd;			/**< just for writing the meta pages */
	/** Failed to update the meta page. Probably an I/O error. */
#define	MDB_FATAL_ERROR	0x80000000U
	/** Some fields are initialized. */
#define	MDB_ENV_ACTIVE	0x20000000U
	/** me_txkey is set */
#define	MDB_ENV_TXKEY	0x10000000U
	/** Have liveness lock in reader table */
#define	MDB_LIVE_READER	0x08000000U
	uint32_t 	me_flags;		/**< @ref mdb_env */
	unsigned int	me_psize;	/**< size of a page, from #GET_PAGESIZE */
	unsigned int	me_maxreaders;	/**< size of the reader table */
	unsigned int	me_numreaders;	/**< max numreaders set by this env */
	MDB_dbi		me_numdbs;		/**< number of DBs opened */
	MDB_dbi		me_maxdbs;		/**< size of the DB table */
	pid_t		me_pid;		/**< process ID of this env */
	char		*me_path;		/**< path to the DB files */
	char		*me_map;		/**< the memory map of the data file */
	MDB_txninfo	*me_txns;		/**< the memory map of the lock file or NULL */
	MDB_meta	*me_metas[2];	/**< pointers to the two meta pages */
	MDB_txn		*me_txn;		/**< current write transaction */
	size_t		me_mapsize;		/**< size of the data memory map */
	off_t		me_size;		/**< current file size */
	pgno_t		me_maxpg;		/**< me_mapsize / me_psize */
	MDB_dbx		*me_dbxs;		/**< array of static DB info */
	uint16_t	*me_dbflags;	/**< array of flags from MDB_db.md_flags */
	pthread_key_t	me_txkey;	/**< thread-key for readers */
	MDB_pgstate	me_pgstate;		/**< state of old pages from freeDB */
#	define		me_pglast	me_pgstate.mf_pglast
#	define		me_pghead	me_pgstate.mf_pghead
	MDB_page	*me_dpages;		/**< list of malloc'd blocks for re-use */
	/** IDL of pages that became unused in a write txn */
	MDB_IDL		me_free_pgs;
	/** ID2L of pages written during a write txn. Length MDB_IDL_UM_SIZE. */
	MDB_ID2L	me_dirty_list;
	/** Max number of freelist items that can fit in a single overflow page */
	int			me_maxfree_1pg;
	/** Max size of a node on a page */
	unsigned int	me_nodemax;
#ifdef _WIN32
	int		me_pidquery;		/**< Used in OpenProcess */
	HANDLE		me_rmutex;		/* Windows mutexes don't reside in shared mem */
	HANDLE		me_wmutex;
#elif defined(MDB_USE_POSIX_SEM)
	sem_t		*me_rmutex;		/* Shared mutexes are not supported */
	sem_t		*me_wmutex;
#endif
};

	/** Nested transaction */
typedef struct MDB_ntxn {
	MDB_txn		mnt_txn;		/* the transaction */
	MDB_pgstate	mnt_pgstate;	/* parent transaction's saved freestate */
} MDB_ntxn;

	/** max number of pages to commit in one writev() call */
#define MDB_COMMIT_PAGES	 64
#if defined(IOV_MAX) && IOV_MAX < MDB_COMMIT_PAGES
#undef MDB_COMMIT_PAGES
#define MDB_COMMIT_PAGES	IOV_MAX
#endif

	/* max bytes to write in one call */
#define MAX_WRITE		(0x80000000U >> (sizeof(ssize_t) == 4))

static int  mdb_page_alloc(MDB_cursor *mc, int num, MDB_page **mp);
static int  mdb_page_new(MDB_cursor *mc, uint32_t flags, int num, MDB_page **mp);
static int  mdb_page_touch(MDB_cursor *mc);

static int  mdb_page_get(MDB_txn *txn, pgno_t pgno, MDB_page **mp, int *lvl);
static int  mdb_page_search_root(MDB_cursor *mc,
			    MDB_val *key, int modify);
#define MDB_PS_MODIFY	1
#define MDB_PS_ROOTONLY	2
static int  mdb_page_search(MDB_cursor *mc,
			    MDB_val *key, int flags);
static int	mdb_page_merge(MDB_cursor *csrc, MDB_cursor *cdst);

#define MDB_SPLIT_REPLACE	MDB_APPENDDUP	/**< newkey is not new */
static int	mdb_page_split(MDB_cursor *mc, MDB_val *newkey, MDB_val *newdata,
				pgno_t newpgno, unsigned int nflags);

static int  mdb_env_read_header(MDB_env *env, MDB_meta *meta);
static int  mdb_env_pick_meta(const MDB_env *env);
static int  mdb_env_write_meta(MDB_txn *txn);
#if !(defined(_WIN32) || defined(MDB_USE_POSIX_SEM)) /* Drop unused excl arg */
# define mdb_env_close0(env, excl) mdb_env_close1(env)
#endif
static void mdb_env_close0(MDB_env *env, int excl);

static MDB_node *mdb_node_search(MDB_cursor *mc, MDB_val *key, int *exactp);
static int  mdb_node_add(MDB_cursor *mc, indx_t indx,
			    MDB_val *key, MDB_val *data, pgno_t pgno, unsigned int flags);
static void mdb_node_del(MDB_page *mp, indx_t indx, int ksize);
static void mdb_node_shrink(MDB_page *mp, indx_t indx);
static int	mdb_node_move(MDB_cursor *csrc, MDB_cursor *cdst);
static int  mdb_node_read(MDB_txn *txn, MDB_node *leaf, MDB_val *data);
static size_t	mdb_leaf_size(MDB_env *env, MDB_val *key, MDB_val *data);
static size_t	mdb_branch_size(MDB_env *env, MDB_val *key);

static int	mdb_rebalance(MDB_cursor *mc);
static int	mdb_update_key(MDB_cursor *mc, MDB_val *key);

static void	mdb_cursor_pop(MDB_cursor *mc);
static int	mdb_cursor_push(MDB_cursor *mc, MDB_page *mp);

static int	mdb_cursor_del0(MDB_cursor *mc, MDB_node *leaf);
static int	mdb_cursor_sibling(MDB_cursor *mc, int move_right);
static int	mdb_cursor_next(MDB_cursor *mc, MDB_val *key, MDB_val *data, MDB_cursor_op op);
static int	mdb_cursor_prev(MDB_cursor *mc, MDB_val *key, MDB_val *data, MDB_cursor_op op);
static int	mdb_cursor_set(MDB_cursor *mc, MDB_val *key, MDB_val *data, MDB_cursor_op op,
				int *exactp);
static int	mdb_cursor_first(MDB_cursor *mc, MDB_val *key, MDB_val *data);
static int	mdb_cursor_last(MDB_cursor *mc, MDB_val *key, MDB_val *data);

static void	mdb_cursor_init(MDB_cursor *mc, MDB_txn *txn, MDB_dbi dbi, MDB_xcursor *mx);
static void	mdb_xcursor_init0(MDB_cursor *mc);
static void	mdb_xcursor_init1(MDB_cursor *mc, MDB_node *node);

static int	mdb_drop0(MDB_cursor *mc, int subs);
static void mdb_default_cmp(MDB_txn *txn, MDB_dbi dbi);

/** @cond */
static MDB_cmp_func	mdb_cmp_memn, mdb_cmp_memnr, mdb_cmp_int, mdb_cmp_cint, mdb_cmp_long;
/** @endcond */

#ifdef _WIN32
static SECURITY_DESCRIPTOR mdb_null_sd;
static SECURITY_ATTRIBUTES mdb_all_sa;
static int mdb_sec_inited;
#endif

/** Return the library version info. */
char *
mdb_version(int *major, int *minor, int *patch)
{
	if (major) *major = MDB_VERSION_MAJOR;
	if (minor) *minor = MDB_VERSION_MINOR;
	if (patch) *patch = MDB_VERSION_PATCH;
	return MDB_VERSION_STRING;
}

/** Table of descriptions for MDB @ref errors */
static char *const mdb_errstr[] = {
	"MDB_KEYEXIST: Key/data pair already exists",
	"MDB_NOTFOUND: No matching key/data pair found",
	"MDB_PAGE_NOTFOUND: Requested page not found",
	"MDB_CORRUPTED: Located page was wrong type",
	"MDB_PANIC: Update of meta page failed",
	"MDB_VERSION_MISMATCH: Database environment version mismatch",
	"MDB_INVALID: File is not an MDB file",
	"MDB_MAP_FULL: Environment mapsize limit reached",
	"MDB_DBS_FULL: Environment maxdbs limit reached",
	"MDB_READERS_FULL: Environment maxreaders limit reached",
	"MDB_TLS_FULL: Thread-local storage keys full - too many environments open",
	"MDB_TXN_FULL: Transaction has too many dirty pages - transaction too big",
	"MDB_CURSOR_FULL: Internal error - cursor stack limit reached",
	"MDB_PAGE_FULL: Internal error - page has no more space",
	"MDB_MAP_RESIZED: Database contents grew beyond environment mapsize",
	"MDB_INCOMPATIBLE: Database flags changed or would change",
	"MDB_BAD_RSLOT: Invalid reuse of reader locktable slot",
};

char *
mdb_strerror(int err)
{
	int i;
	if (!err)
		return ("Successful return: 0");

	if (err >= MDB_KEYEXIST && err <= MDB_LAST_ERRCODE) {
		i = err - MDB_KEYEXIST;
		return mdb_errstr[i];
	}

	return strerror(err);
}

#if MDB_DEBUG
/** Display a key in hexadecimal and return the address of the result.
 * @param[in] key the key to display
 * @param[in] buf the buffer to write into. Should always be #DKBUF.
 * @return The key in hexadecimal form.
 */
char *
mdb_dkey(MDB_val *key, char *buf)
{
	char *ptr = buf;
	unsigned char *c = key->mv_data;
	unsigned int i;

	if (!key)
		return "";

	if (key->mv_size > MDB_MAXKEYSIZE)
		return "MDB_MAXKEYSIZE";
	/* may want to make this a dynamic check: if the key is mostly
	 * printable characters, print it as-is instead of converting to hex.
	 */
#if 1
	buf[0] = '\0';
	for (i=0; i<key->mv_size; i++)
		ptr += sprintf(ptr, "%02x", *c++);
#else
	sprintf(buf, "%.*s", key->mv_size, key->mv_data);
#endif
	return buf;
}

/** Display all the keys in the page. */
void
mdb_page_list(MDB_page *mp)
{
	MDB_node *node;
	unsigned int i, nkeys, nsize;
	MDB_val key;
	DKBUF;

	nkeys = NUMKEYS(mp);
	fprintf(stderr, "Page %zu numkeys %d\n", mp->mp_pgno, nkeys);
	for (i=0; i<nkeys; i++) {
		node = NODEPTR(mp, i);
		key.mv_size = node->mn_ksize;
		key.mv_data = node->mn_data;
		nsize = NODESIZE + NODEKSZ(node) + sizeof(indx_t);
		if (IS_BRANCH(mp)) {
			fprintf(stderr, "key %d: page %zu, %s\n", i, NODEPGNO(node),
				DKEY(&key));
		} else {
			if (F_ISSET(node->mn_flags, F_BIGDATA))
				nsize += sizeof(pgno_t);
			else
				nsize += NODEDSZ(node);
			fprintf(stderr, "key %d: nsize %d, %s\n", i, nsize, DKEY(&key));
		}
	}
}

void
mdb_cursor_chk(MDB_cursor *mc)
{
	unsigned int i;
	MDB_node *node;
	MDB_page *mp;

	if (!mc->mc_snum && !(mc->mc_flags & C_INITIALIZED)) return;
	for (i=0; i<mc->mc_top; i++) {
		mp = mc->mc_pg[i];
		node = NODEPTR(mp, mc->mc_ki[i]);
		if (NODEPGNO(node) != mc->mc_pg[i+1]->mp_pgno)
			printf("oops!\n");
	}
	if (mc->mc_ki[i] >= NUMKEYS(mc->mc_pg[i]))
		printf("ack!\n");
}
#endif

#if MDB_DEBUG > 2
/** Count all the pages in each DB and in the freelist
 *  and make sure it matches the actual number of pages
 *  being used.
 */
static void mdb_audit(MDB_txn *txn)
{
	MDB_cursor mc;
	MDB_val key, data;
	MDB_ID freecount, count;
	MDB_dbi i;
	int rc;

	freecount = 0;
	mdb_cursor_init(&mc, txn, FREE_DBI, NULL);
	while ((rc = mdb_cursor_get(&mc, &key, &data, MDB_NEXT)) == 0)
		freecount += *(MDB_ID *)data.mv_data;

	count = 0;
	for (i = 0; i<txn->mt_numdbs; i++) {
		MDB_xcursor mx;
		mdb_cursor_init(&mc, txn, i, &mx);
		if (txn->mt_dbs[i].md_root == P_INVALID)
			continue;
		count += txn->mt_dbs[i].md_branch_pages +
			txn->mt_dbs[i].md_leaf_pages +
			txn->mt_dbs[i].md_overflow_pages;
		if (txn->mt_dbs[i].md_flags & MDB_DUPSORT) {
			mdb_page_search(&mc, NULL, 0);
			do {
				unsigned j;
				MDB_page *mp;
				mp = mc.mc_pg[mc.mc_top];
				for (j=0; j<NUMKEYS(mp); j++) {
					MDB_node *leaf = NODEPTR(mp, j);
					if (leaf->mn_flags & F_SUBDATA) {
						MDB_db db;
						memcpy(&db, NODEDATA(leaf), sizeof(db));
						count += db.md_branch_pages + db.md_leaf_pages +
							db.md_overflow_pages;
					}
				}
			}
			while (mdb_cursor_sibling(&mc, 1) == 0);
		}
	}
	if (freecount + count + 2 /* metapages */ != txn->mt_next_pgno) {
		fprintf(stderr, "audit: %lu freecount: %lu count: %lu total: %lu next_pgno: %lu\n",
			txn->mt_txnid, freecount, count+2, freecount+count+2, txn->mt_next_pgno);
	}
}
#endif

int
mdb_cmp(MDB_txn *txn, MDB_dbi dbi, const MDB_val *a, const MDB_val *b)
{
	return txn->mt_dbxs[dbi].md_cmp(a, b);
}

int
mdb_dcmp(MDB_txn *txn, MDB_dbi dbi, const MDB_val *a, const MDB_val *b)
{
	return txn->mt_dbxs[dbi].md_dcmp(a, b);
}

/** Allocate a page.
 * Re-use old malloc'd pages first for singletons, otherwise just malloc.
 */
static MDB_page *
mdb_page_malloc(MDB_txn *txn, unsigned num)
{
	MDB_env *env = txn->mt_env;
	MDB_page *ret = env->me_dpages;
	size_t sz = env->me_psize;
	if (num == 1) {
		if (ret) {
			VGMEMP_ALLOC(env, ret, sz);
			VGMEMP_DEFINED(ret, sizeof(ret->mp_next));
			env->me_dpages = ret->mp_next;
			return ret;
		}
	} else {
		sz *= num;
	}
	if ((ret = malloc(sz)) != NULL) {
		VGMEMP_ALLOC(env, ret, sz);
	}
	return ret;
}

/** Free a single page.
 * Saves single pages to a list, for future reuse.
 * (This is not used for multi-page overflow pages.)
 */
static void
mdb_page_free(MDB_env *env, MDB_page *mp)
{
	mp->mp_next = env->me_dpages;
	VGMEMP_FREE(env, mp);
	env->me_dpages = mp;
}

/* Free a dirty page */
static void
mdb_dpage_free(MDB_env *env, MDB_page *dp)
{
	if (!IS_OVERFLOW(dp) || dp->mp_pages == 1) {
		mdb_page_free(env, dp);
	} else {
		/* large pages just get freed directly */
		VGMEMP_FREE(env, dp);
		free(dp);
	}
}

/**	Return all dirty pages to dpage list */
static void
mdb_dlist_free(MDB_txn *txn)
{
	MDB_env *env = txn->mt_env;
	MDB_ID2L dl = txn->mt_u.dirty_list;
	unsigned i, n = dl[0].mid;

	for (i = 1; i <= n; i++) {
		mdb_dpage_free(env, dl[i].mptr);
	}
	dl[0].mid = 0;
}

/* Set or clear P_KEEP in non-overflow, non-sub pages in known cursors.
 * When clearing, only consider backup cursors (from parent txns) since
 * other P_KEEP flags have already been cleared.
 * @param[in] mc A cursor handle for the current operation.
 * @param[in] pflags Flags of the pages to update:
 * P_DIRTY to set P_KEEP, P_DIRTY|P_KEEP to clear it.
 */
static void
mdb_cursorpages_mark(MDB_cursor *mc, unsigned pflags)
{
	MDB_txn *txn = mc->mc_txn;
	MDB_cursor *m2, *m3;
	MDB_xcursor *mx;
	unsigned i, j;

	if (mc->mc_flags & C_UNTRACK)
		mc = NULL;				/* will find mc in mt_cursors */
	for (i = txn->mt_numdbs;; mc = txn->mt_cursors[--i]) {
		for (; mc; mc=mc->mc_next) {
			m2 = pflags == P_DIRTY ? mc : mc->mc_backup;
			for (; m2; m2 = m2->mc_backup) {
				for (m3=m2; m3->mc_flags & C_INITIALIZED; m3=&mx->mx_cursor) {
					for (j=0; j<m3->mc_snum; j++)
						if ((m3->mc_pg[j]->mp_flags & (P_SUBP|P_DIRTY|P_KEEP))
								== pflags)
							m3->mc_pg[j]->mp_flags ^= P_KEEP;
					if (!(m3->mc_db->md_flags & MDB_DUPSORT))
						break;
					/* Cursor backups have mx malloced at the end of m2 */
					mx = (m3 == mc ? m3->mc_xcursor : (MDB_xcursor *)(m3+1));
				}
			}
		}
		if (i == 0)
			break;
	}
}

static int mdb_page_flush(MDB_txn *txn);

/**	Spill pages from the dirty list back to disk.
 * This is intended to prevent running into #MDB_TXN_FULL situations,
 * but note that they may still occur in a few cases:
 *	1) pages in #MDB_DUPSORT sub-DBs are never spilled, so if there
 *	 are too many of these dirtied in one txn, the txn may still get
 *	 too full.
 *	2) child txns may run out of space if their parents dirtied a
 *	 lot of pages and never spilled them. TODO: we probably should do
 *	 a preemptive spill during #mdb_txn_begin() of a child txn, if
 *	 the parent's dirty_room is below a given threshold.
 *	3) our estimate of the txn size could be too small. At the
 *	 moment this seems unlikely.
 *
 * Otherwise, if not using nested txns, it is expected that apps will
 * not run into #MDB_TXN_FULL any more. The pages are flushed to disk
 * the same way as for a txn commit, e.g. their P_DIRTY flag is cleared.
 * If the txn never references them again, they can be left alone.
 * If the txn only reads them, they can be used without any fuss.
 * If the txn writes them again, they can be dirtied immediately without
 * going thru all of the work of #mdb_page_touch(). Such references are
 * handled by #mdb_page_unspill().
 *
 * Also note, we never spill DB root pages, nor pages of active cursors,
 * because we'll need these back again soon anyway. And in nested txns,
 * we can't spill a page in a child txn if it was already spilled in a
 * parent txn. That would alter the parent txns' data even though
 * the child hasn't committed yet, and we'd have no way to undo it if
 * the child aborted.
 *
 * @param[in] m0 cursor A cursor handle identifying the transaction and
 *	database for which we are checking space.
 * @param[in] key For a put operation, the key being stored.
 * @param[in] data For a put operation, the data being stored.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_page_spill(MDB_cursor *m0, MDB_val *key, MDB_val *data)
{
	MDB_txn *txn = m0->mc_txn;
	MDB_page *dp;
	MDB_ID2L dl = txn->mt_u.dirty_list;
	unsigned int i, j;
	int rc;

	if (m0->mc_flags & C_SUB)
		return MDB_SUCCESS;

	/* Estimate how much space this op will take */
	i = m0->mc_db->md_depth;
	/* Named DBs also dirty the main DB */
	if (m0->mc_dbi > MAIN_DBI)
		i += txn->mt_dbs[MAIN_DBI].md_depth;
	/* For puts, roughly factor in the key+data size */
	if (key)
		i += (LEAFSIZE(key, data) + txn->mt_env->me_psize) / txn->mt_env->me_psize;
	i += i;	/* double it for good measure */

	if (txn->mt_dirty_room > i)
		return MDB_SUCCESS;

	if (!txn->mt_spill_pgs) {
		txn->mt_spill_pgs = mdb_midl_alloc(MDB_IDL_UM_MAX);
		if (!txn->mt_spill_pgs)
			return ENOMEM;
	}

	/* Mark all the dirty root pages we want to preserve */
	for (i=0; i<txn->mt_numdbs; i++) {
		if (txn->mt_dbflags[i] & DB_DIRTY) {
			j = mdb_mid2l_search(dl, txn->mt_dbs[i].md_root);
			if (j <= dl[0].mid) {
				dp = dl[j].mptr;
				dp->mp_flags |= P_KEEP;
			}
		}
	}

	/* Preserve pages used by cursors */
	mdb_cursorpages_mark(m0, P_DIRTY);

	/* Save the page IDs of all the pages we're flushing */
	for (i=1; i<=dl[0].mid; i++) {
		dp = dl[i].mptr;
		if (dp->mp_flags & P_KEEP)
			continue;
		/* Can't spill twice, make sure it's not already in a parent's
		 * spill list.
		 */
		if (txn->mt_parent) {
			MDB_txn *tx2;
			for (tx2 = txn->mt_parent; tx2; tx2 = tx2->mt_parent) {
				if (tx2->mt_spill_pgs) {
					j = mdb_midl_search(tx2->mt_spill_pgs, dl[i].mid);
					if (j <= tx2->mt_spill_pgs[0] && tx2->mt_spill_pgs[j] == dl[i].mid) {
						dp->mp_flags |= P_KEEP;
						break;
					}
				}
			}
			if (tx2)
				continue;
		}
		if ((rc = mdb_midl_append(&txn->mt_spill_pgs, dl[i].mid)))
			return rc;
	}
	mdb_midl_sort(txn->mt_spill_pgs);

	rc = mdb_page_flush(txn);

	mdb_cursorpages_mark(m0, P_DIRTY|P_KEEP);

	if (rc == 0) {
		if (txn->mt_parent) {
			MDB_txn *tx2;
			pgno_t pgno = dl[i].mid;
			txn->mt_dirty_room = txn->mt_parent->mt_dirty_room - dl[0].mid;
			/* dirty pages that are dirty in an ancestor don't
			 * count against this txn's dirty_room.
			 */
			for (i=1; i<=dl[0].mid; i++) {
				for (tx2 = txn->mt_parent; tx2; tx2 = tx2->mt_parent) {
					j = mdb_mid2l_search(tx2->mt_u.dirty_list, pgno);
					if (j <= tx2->mt_u.dirty_list[0].mid &&
						tx2->mt_u.dirty_list[j].mid == pgno) {
						txn->mt_dirty_room++;
						break;
					}
				}
			}
		} else {
			txn->mt_dirty_room = MDB_IDL_UM_MAX - dl[0].mid;
		}
		txn->mt_flags |= MDB_TXN_SPILLS;
	}
	return rc;
}

/** Find oldest txnid still referenced. Expects txn->mt_txnid > 0. */
static txnid_t
mdb_find_oldest(MDB_txn *txn)
{
	int i;
	txnid_t mr, oldest = txn->mt_txnid - 1;
	MDB_reader *r = txn->mt_env->me_txns->mti_readers;
	for (i = txn->mt_env->me_txns->mti_numreaders; --i >= 0; ) {
		if (r[i].mr_pid) {
			mr = r[i].mr_txnid;
			if (oldest > mr)
				oldest = mr;
		}
	}
	return oldest;
}

/** Add a page to the txn's dirty list */
static void
mdb_page_dirty(MDB_txn *txn, MDB_page *mp)
{
	MDB_ID2 mid;
	int (*insert)(MDB_ID2L, MDB_ID2 *);

	if (txn->mt_env->me_flags & MDB_WRITEMAP) {
		insert = mdb_mid2l_append;
	} else {
		insert = mdb_mid2l_insert;
	}
	mid.mid = mp->mp_pgno;
	mid.mptr = mp;
	insert(txn->mt_u.dirty_list, &mid);
	txn->mt_dirty_room--;
}

/** Allocate pages for writing.
 * If there are free pages available from older transactions, they
 * will be re-used first. Otherwise a new page will be allocated.
 * @param[in] mc cursor A cursor handle identifying the transaction and
 *	database for which we are allocating.
 * @param[in] num the number of pages to allocate.
 * @param[out] mp Address of the allocated page(s). Requests for multiple pages
 *  will always be satisfied by a single contiguous chunk of memory.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_page_alloc(MDB_cursor *mc, int num, MDB_page **mp)
{
#ifdef MDB_PARANOID	/* Seems like we can ignore this now */
	/* Get at most <Max_retries> more freeDB records once me_pghead
	 * has enough pages.  If not enough, use new pages from the map.
	 * If <Paranoid> and mc is updating the freeDB, only get new
	 * records if me_pghead is empty. Then the freelist cannot play
	 * catch-up with itself by growing while trying to save it.
	 */
	enum { Paranoid = 1, Max_retries = 500 };
#else
	enum { Paranoid = 0, Max_retries = INT_MAX /*infinite*/ };
#endif
	int rc, n2 = num-1, retry = Max_retries;
	MDB_txn *txn = mc->mc_txn;
	MDB_env *env = txn->mt_env;
	pgno_t pgno, *mop = env->me_pghead;
	unsigned i, j, k, mop_len = mop ? mop[0] : 0;
	MDB_page *np;
	txnid_t oldest = 0, last;
	MDB_cursor_op op;
	MDB_cursor m2;

	*mp = NULL;

	/* If our dirty list is already full, we can't do anything */
	if (txn->mt_dirty_room == 0)
		return MDB_TXN_FULL;

	for (op = MDB_FIRST;; op = MDB_NEXT) {
		MDB_val key, data;
		MDB_node *leaf;
		pgno_t *idl, old_id, new_id;

		/* Seek a big enough contiguous page range. Prefer
		 * pages at the tail, just truncating the list.
		 */
		if (mop_len >= (unsigned)num) {
			i = mop_len;
			do {
				pgno = mop[i];
				if (mop[i-n2] == pgno+n2)
					goto search_done;
			} while (--i >= (unsigned)num);
			if (Max_retries < INT_MAX && --retry < 0)
				break;
		}

		if (op == MDB_FIRST) {	/* 1st iteration */
			/* Prepare to fetch more and coalesce */
			oldest = mdb_find_oldest(txn);
			last = env->me_pglast;
			mdb_cursor_init(&m2, txn, FREE_DBI, NULL);
			if (last) {
				op = MDB_SET_RANGE;
				key.mv_data = &last; /* will loop up last+1 */
				key.mv_size = sizeof(last);
			}
			if (Paranoid && mc->mc_dbi == FREE_DBI)
				retry = -1;
		}
		if (Paranoid && retry < 0 && mop_len)
			break;

		last++;
		/* Do not fetch more if the record will be too recent */
		if (oldest <= last)
			break;
		rc = mdb_cursor_get(&m2, &key, NULL, op);
		if (rc) {
			if (rc == MDB_NOTFOUND)
				break;
			return rc;
		}
		last = *(txnid_t*)key.mv_data;
		if (oldest <= last)
			break;
		np = m2.mc_pg[m2.mc_top];
		leaf = NODEPTR(np, m2.mc_ki[m2.mc_top]);
		if ((rc = mdb_node_read(txn, leaf, &data)) != MDB_SUCCESS)
			return rc;

		idl = (MDB_ID *) data.mv_data;
		i = idl[0];
		if (!mop) {
			if (!(env->me_pghead = mop = mdb_midl_alloc(i)))
				return ENOMEM;
		} else {
			if ((rc = mdb_midl_need(&env->me_pghead, i)) != 0)
				return rc;
			mop = env->me_pghead;
		}
		env->me_pglast = last;
#if MDB_DEBUG > 1
		DPRINTF("IDL read txn %zu root %zu num %u",
				last, txn->mt_dbs[FREE_DBI].md_root, i);
		for (k = i; k; k--)
			DPRINTF("IDL %zu", idl[k]);
#endif
		/* Merge in descending sorted order */
		j = mop_len;
		k = mop_len += i;
		mop[0] = (pgno_t)-1;
		old_id = mop[j];
		while (i) {
			new_id = idl[i--];
			for (; old_id < new_id; old_id = mop[--j])
				mop[k--] = old_id;
			mop[k--] = new_id;
		}
		mop[0] = mop_len;
	}

	/* Use new pages from the map when nothing suitable in the freeDB */
	i = 0;
	pgno = txn->mt_next_pgno;
	if (pgno + num >= env->me_maxpg) {
			DPUTS("DB size maxed out");
			return MDB_MAP_FULL;
	}

search_done:
	if (env->me_flags & MDB_WRITEMAP) {
		np = (MDB_page *)(env->me_map + env->me_psize * pgno);
	} else {
		if (!(np = mdb_page_malloc(txn, num)))
			return ENOMEM;
	}
	if (i) {
		mop[0] = mop_len -= num;
		/* Move any stragglers down */
		for (j = i-num; j < mop_len; )
			mop[++j] = mop[++i];
	} else {
		txn->mt_next_pgno = pgno + num;
	}
	np->mp_pgno = pgno;
	mdb_page_dirty(txn, np);
	*mp = np;

	return MDB_SUCCESS;
}

/** Copy the used portions of a non-overflow page.
 * @param[in] dst page to copy into
 * @param[in] src page to copy from
 * @param[in] psize size of a page
 */
static void
mdb_page_copy(MDB_page *dst, MDB_page *src, unsigned int psize)
{
	enum { Align = sizeof(pgno_t) };
	indx_t upper = src->mp_upper, lower = src->mp_lower, unused = upper-lower;

	/* If page isn't full, just copy the used portion. Adjust
	 * alignment so memcpy may copy words instead of bytes.
	 */
	if ((unused &= -Align) && !IS_LEAF2(src)) {
		upper &= -Align;
		memcpy(dst, src, (lower + (Align-1)) & -Align);
		memcpy((pgno_t *)((char *)dst+upper), (pgno_t *)((char *)src+upper),
			psize - upper);
	} else {
		memcpy(dst, src, psize - unused);
	}
}

/** Pull a page off the txn's spill list, if present.
 * If a page being referenced was spilled to disk in this txn, bring
 * it back and make it dirty/writable again.
 * @param[in] tx0 the transaction handle.
 * @param[in] mp the page being referenced.
 * @param[out] ret the writable page, if any. ret is unchanged if
 * mp wasn't spilled.
 */
static int
mdb_page_unspill(MDB_txn *tx0, MDB_page *mp, MDB_page **ret)
{
	MDB_env *env = tx0->mt_env;
	MDB_txn *txn;
	unsigned x;
	pgno_t pgno = mp->mp_pgno;

	for (txn = tx0; txn; txn=txn->mt_parent) {
		if (!txn->mt_spill_pgs)
			continue;
		x = mdb_midl_search(txn->mt_spill_pgs, pgno);
		if (x <= txn->mt_spill_pgs[0] && txn->mt_spill_pgs[x] == pgno) {
			MDB_page *np;
			int num;
			if (IS_OVERFLOW(mp))
				num = mp->mp_pages;
			else
				num = 1;
			if (env->me_flags & MDB_WRITEMAP) {
				np = mp;
			} else {
				np = mdb_page_malloc(txn, num);
				if (!np)
					return ENOMEM;
				if (num > 1)
					memcpy(np, mp, num * env->me_psize);
				else
					mdb_page_copy(np, mp, env->me_psize);
			}
			if (txn == tx0) {
				/* If in current txn, this page is no longer spilled */
				for (; x < txn->mt_spill_pgs[0]; x++)
					txn->mt_spill_pgs[x] = txn->mt_spill_pgs[x+1];
				txn->mt_spill_pgs[0]--;
			}	/* otherwise, if belonging to a parent txn, the
				 * page remains spilled until child commits
				 */

			if (txn->mt_parent) {
				MDB_txn *tx2;
				/* If this page is also in a parent's dirty list, then
				 * it's already accounted in dirty_room, and we need to
				 * cancel out the decrement that mdb_page_dirty does.
				 */
				for (tx2 = txn->mt_parent; tx2; tx2 = tx2->mt_parent) {
					x = mdb_mid2l_search(tx2->mt_u.dirty_list, pgno);
					if (x <= tx2->mt_u.dirty_list[0].mid &&
						tx2->mt_u.dirty_list[x].mid == pgno) {
						txn->mt_dirty_room++;
						break;
					}
				}
			}
			mdb_page_dirty(tx0, np);
			np->mp_flags |= P_DIRTY;
			*ret = np;
			break;
		}
	}
	return MDB_SUCCESS;
}

/** Touch a page: make it dirty and re-insert into tree with updated pgno.
 * @param[in] mc cursor pointing to the page to be touched
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_page_touch(MDB_cursor *mc)
{
	MDB_page *mp = mc->mc_pg[mc->mc_top], *np;
	MDB_txn *txn = mc->mc_txn;
	MDB_cursor *m2, *m3;
	MDB_dbi dbi;
	pgno_t	pgno;
	int rc;

	if (!F_ISSET(mp->mp_flags, P_DIRTY)) {
		if (txn->mt_flags & MDB_TXN_SPILLS) {
			np = NULL;
			rc = mdb_page_unspill(txn, mp, &np);
			if (rc)
				return rc;
			if (np)
				goto done;
		}
		if ((rc = mdb_midl_need(&txn->mt_free_pgs, 1)) ||
			(rc = mdb_page_alloc(mc, 1, &np)))
			return rc;
		pgno = np->mp_pgno;
		DPRINTF("touched db %u page %zu -> %zu", mc->mc_dbi,mp->mp_pgno,pgno);
		assert(mp->mp_pgno != pgno);
		mdb_midl_xappend(txn->mt_free_pgs, mp->mp_pgno);
		/* Update the parent page, if any, to point to the new page */
		if (mc->mc_top) {
			MDB_page *parent = mc->mc_pg[mc->mc_top-1];
			MDB_node *node = NODEPTR(parent, mc->mc_ki[mc->mc_top-1]);
			SETPGNO(node, pgno);
		} else {
			mc->mc_db->md_root = pgno;
		}
	} else if (txn->mt_parent && !IS_SUBP(mp)) {
		MDB_ID2 mid, *dl = txn->mt_u.dirty_list;
		pgno = mp->mp_pgno;
		/* If txn has a parent, make sure the page is in our
		 * dirty list.
		 */
		if (dl[0].mid) {
			unsigned x = mdb_mid2l_search(dl, pgno);
			if (x <= dl[0].mid && dl[x].mid == pgno) {
				if (mp != dl[x].mptr) { /* bad cursor? */
					mc->mc_flags &= ~(C_INITIALIZED|C_EOF);
					return MDB_CORRUPTED;
				}
				return 0;
			}
		}
		assert(dl[0].mid < MDB_IDL_UM_MAX);
		/* No - copy it */
		np = mdb_page_malloc(txn, 1);
		if (!np)
			return ENOMEM;
		mid.mid = pgno;
		mid.mptr = np;
		mdb_mid2l_insert(dl, &mid);
	} else {
		return 0;
	}

	mdb_page_copy(np, mp, txn->mt_env->me_psize);
	np->mp_pgno = pgno;
	np->mp_flags |= P_DIRTY;

done:
	/* Adjust cursors pointing to mp */
	mc->mc_pg[mc->mc_top] = np;
	dbi = mc->mc_dbi;
	if (mc->mc_flags & C_SUB) {
		dbi--;
		for (m2 = txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
			m3 = &m2->mc_xcursor->mx_cursor;
			if (m3->mc_snum < mc->mc_snum) continue;
			if (m3->mc_pg[mc->mc_top] == mp)
				m3->mc_pg[mc->mc_top] = np;
		}
	} else {
		for (m2 = txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
			if (m2->mc_snum < mc->mc_snum) continue;
			if (m2->mc_pg[mc->mc_top] == mp) {
				m2->mc_pg[mc->mc_top] = np;
				if ((mc->mc_db->md_flags & MDB_DUPSORT) &&
					m2->mc_ki[mc->mc_top] == mc->mc_ki[mc->mc_top])
				{
					MDB_node *leaf = NODEPTR(np, mc->mc_ki[mc->mc_top]);
					if (!(leaf->mn_flags & F_SUBDATA))
						m2->mc_xcursor->mx_cursor.mc_pg[0] = NODEDATA(leaf);
				}
			}
		}
	}
	return 0;
}

int
mdb_env_sync(MDB_env *env, int force)
{
	int rc = 0;
	if (force || !F_ISSET(env->me_flags, MDB_NOSYNC)) {
		if (env->me_flags & MDB_WRITEMAP) {
			int flags = ((env->me_flags & MDB_MAPASYNC) && !force)
				? MS_ASYNC : MS_SYNC;
			if (MDB_MSYNC(env->me_map, env->me_mapsize, flags))
				rc = ErrCode();
#ifdef _WIN32
			else if (flags == MS_SYNC && MDB_FDATASYNC(env->me_fd))
				rc = ErrCode();
#endif
		} else {
			if (MDB_FDATASYNC(env->me_fd))
				rc = ErrCode();
		}
	}
	return rc;
}

/** Back up parent txn's cursors, then grab the originals for tracking */
static int
mdb_cursor_shadow(MDB_txn *src, MDB_txn *dst)
{
	MDB_cursor *mc, *bk;
	MDB_xcursor *mx;
	size_t size;
	int i;

	for (i = src->mt_numdbs; --i >= 0; ) {
		if ((mc = src->mt_cursors[i]) != NULL) {
			size = sizeof(MDB_cursor);
			if (mc->mc_xcursor)
				size += sizeof(MDB_xcursor);
			for (; mc; mc = bk->mc_next) {
				bk = malloc(size);
				if (!bk)
					return ENOMEM;
				*bk = *mc;
				mc->mc_backup = bk;
				mc->mc_db = &dst->mt_dbs[i];
				/* Kill pointers into src - and dst to reduce abuse: The
				 * user may not use mc until dst ends. Otherwise we'd...
				 */
				mc->mc_txn    = NULL;	/* ...set this to dst */
				mc->mc_dbflag = NULL;	/* ...and &dst->mt_dbflags[i] */
				if ((mx = mc->mc_xcursor) != NULL) {
					*(MDB_xcursor *)(bk+1) = *mx;
					mx->mx_cursor.mc_txn = NULL; /* ...and dst. */
				}
				mc->mc_next = dst->mt_cursors[i];
				dst->mt_cursors[i] = mc;
			}
		}
	}
	return MDB_SUCCESS;
}

/** Close this write txn's cursors, give parent txn's cursors back to parent.
 * @param[in] txn the transaction handle.
 * @param[in] merge true to keep changes to parent cursors, false to revert.
 * @return 0 on success, non-zero on failure.
 */
static void
mdb_cursors_close(MDB_txn *txn, unsigned merge)
{
	MDB_cursor **cursors = txn->mt_cursors, *mc, *next, *bk;
	MDB_xcursor *mx;
	int i;

	for (i = txn->mt_numdbs; --i >= 0; ) {
		for (mc = cursors[i]; mc; mc = next) {
			next = mc->mc_next;
			if ((bk = mc->mc_backup) != NULL) {
				if (merge) {
					/* Commit changes to parent txn */
					mc->mc_next = bk->mc_next;
					mc->mc_backup = bk->mc_backup;
					mc->mc_txn = bk->mc_txn;
					mc->mc_db = bk->mc_db;
					mc->mc_dbflag = bk->mc_dbflag;
					if ((mx = mc->mc_xcursor) != NULL)
						mx->mx_cursor.mc_txn = bk->mc_txn;
				} else {
					/* Abort nested txn */
					*mc = *bk;
					if ((mx = mc->mc_xcursor) != NULL)
						*mx = *(MDB_xcursor *)(bk+1);
				}
				mc = bk;
			}
			free(mc);
		}
		cursors[i] = NULL;
	}
}

#ifdef MDB_DEBUG_SKIP
#define mdb_txn_reset0(txn, act) mdb_txn_reset0(txn)
#endif
static void
mdb_txn_reset0(MDB_txn *txn, const char *act);

#ifdef _WIN32
enum Pidlock_op {
	Pidset, Pidcheck
};
#else
enum Pidlock_op {
	Pidset = F_SETLK, Pidcheck = F_GETLK
};
#endif

/** Set or check a pid lock. Set returns 0 on success.
 * Check returns 0 if lock exists (meaning the process is alive).
 *
 * On Windows Pidset is a no-op, we merely check for the existence
 * of the process with the given pid. On POSIX we use a single byte
 * lock on the lockfile, set at an offset equal to the pid.
 */
static int
mdb_reader_pid(MDB_env *env, enum Pidlock_op op, pid_t pid)
{
#ifdef _WIN32
	HANDLE h;
	int ver, query;
	switch(op) {
	case Pidset:
		break;
	case Pidcheck:
		h = OpenProcess(env->me_pidquery, FALSE, pid);
		if (!h)
			return GetLastError();
		CloseHandle(h);
		break;
	}
	return 0;
#else
	int rc;
	struct flock lock_info;
	memset((void *)&lock_info, 0, sizeof(lock_info));
	lock_info.l_type = F_WRLCK;
	lock_info.l_whence = SEEK_SET;
	lock_info.l_start = pid;
	lock_info.l_len = 1;
	while ((rc = fcntl(env->me_lfd, op, &lock_info)) &&
			(rc = ErrCode()) == EINTR) ;
	if (op == F_GETLK && rc == 0 && lock_info.l_type == F_UNLCK)
		rc = -1;
	return rc;
#endif
}

/** Common code for #mdb_txn_begin() and #mdb_txn_renew().
 * @param[in] txn the transaction handle to initialize
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_txn_renew0(MDB_txn *txn)
{
	MDB_env *env = txn->mt_env;
	unsigned int i;
	uint16_t x;
	int rc, new_notls = 0;

	/* Setup db info */
	txn->mt_numdbs = env->me_numdbs;
	txn->mt_dbxs = env->me_dbxs;	/* mostly static anyway */

	if (txn->mt_flags & MDB_TXN_RDONLY) {
		if (!env->me_txns) {
			i = mdb_env_pick_meta(env);
			txn->mt_txnid = env->me_metas[i]->mm_txnid;
			txn->mt_u.reader = NULL;
		} else {
			MDB_reader *r = (env->me_flags & MDB_NOTLS) ? txn->mt_u.reader :
				pthread_getspecific(env->me_txkey);
			if (r) {
				if (r->mr_pid != env->me_pid || r->mr_txnid != (txnid_t)-1)
					return MDB_BAD_RSLOT;
			} else {
				pid_t pid = env->me_pid;
				pthread_t tid = pthread_self();

				if (!(env->me_flags & MDB_LIVE_READER)) {
					rc = mdb_reader_pid(env, Pidset, pid);
					if (rc) {
						UNLOCK_MUTEX_R(env);
						return rc;
					}
					env->me_flags |= MDB_LIVE_READER;
				}

				LOCK_MUTEX_R(env);
				for (i=0; i<env->me_txns->mti_numreaders; i++)
					if (env->me_txns->mti_readers[i].mr_pid == 0)
						break;
				if (i == env->me_maxreaders) {
					UNLOCK_MUTEX_R(env);
					return MDB_READERS_FULL;
				}
				env->me_txns->mti_readers[i].mr_pid = pid;
				env->me_txns->mti_readers[i].mr_tid = tid;
				if (i >= env->me_txns->mti_numreaders)
					env->me_txns->mti_numreaders = i+1;
				/* Save numreaders for un-mutexed mdb_env_close() */
				env->me_numreaders = env->me_txns->mti_numreaders;
				UNLOCK_MUTEX_R(env);
				r = &env->me_txns->mti_readers[i];
				new_notls = (env->me_flags & MDB_NOTLS);
				if (!new_notls && (rc=pthread_setspecific(env->me_txkey, r))) {
					r->mr_pid = 0;
					return rc;
				}
			}
			txn->mt_txnid = r->mr_txnid = env->me_txns->mti_txnid;
			txn->mt_u.reader = r;
		}
		txn->mt_toggle = txn->mt_txnid & 1;
	} else {
		LOCK_MUTEX_W(env);

		txn->mt_txnid = env->me_txns->mti_txnid;
		txn->mt_toggle = txn->mt_txnid & 1;
		txn->mt_txnid++;
#if MDB_DEBUG
		if (txn->mt_txnid == mdb_debug_start)
			mdb_debug = 1;
#endif
		txn->mt_dirty_room = MDB_IDL_UM_MAX;
		txn->mt_u.dirty_list = env->me_dirty_list;
		txn->mt_u.dirty_list[0].mid = 0;
		txn->mt_free_pgs = env->me_free_pgs;
		txn->mt_free_pgs[0] = 0;
		txn->mt_spill_pgs = NULL;
		env->me_txn = txn;
	}

	/* Copy the DB info and flags */
	memcpy(txn->mt_dbs, env->me_metas[txn->mt_toggle]->mm_dbs, 2 * sizeof(MDB_db));

	/* Moved to here to avoid a data race in read TXNs */
	txn->mt_next_pgno = env->me_metas[txn->mt_toggle]->mm_last_pg+1;

	for (i=2; i<txn->mt_numdbs; i++) {
		x = env->me_dbflags[i];
		txn->mt_dbs[i].md_flags = x & PERSISTENT_FLAGS;
		txn->mt_dbflags[i] = (x & MDB_VALID) ? DB_VALID|DB_STALE : 0;
	}
	txn->mt_dbflags[0] = txn->mt_dbflags[1] = DB_VALID;

	if (env->me_maxpg < txn->mt_next_pgno) {
		mdb_txn_reset0(txn, "renew0-mapfail");
		if (new_notls) {
			txn->mt_u.reader->mr_pid = 0;
			txn->mt_u.reader = NULL;
		}
		return MDB_MAP_RESIZED;
	}

	return MDB_SUCCESS;
}

int
mdb_txn_renew(MDB_txn *txn)
{
	int rc;

	if (!txn || txn->mt_dbxs)	/* A reset txn has mt_dbxs==NULL */
		return EINVAL;

	if (txn->mt_env->me_flags & MDB_FATAL_ERROR) {
		DPUTS("environment had fatal error, must shutdown!");
		return MDB_PANIC;
	}

	rc = mdb_txn_renew0(txn);
	if (rc == MDB_SUCCESS) {
		DPRINTF("renew txn %zu%c %p on mdbenv %p, root page %zu",
			txn->mt_txnid, (txn->mt_flags & MDB_TXN_RDONLY) ? 'r' : 'w',
			(void *)txn, (void *)txn->mt_env, txn->mt_dbs[MAIN_DBI].md_root);
	}
	return rc;
}

int
mdb_txn_begin(MDB_env *env, MDB_txn *parent, unsigned int flags, MDB_txn **ret)
{
	MDB_txn *txn;
	MDB_ntxn *ntxn;
	int rc, size, tsize = sizeof(MDB_txn);

	if (env->me_flags & MDB_FATAL_ERROR) {
		DPUTS("environment had fatal error, must shutdown!");
		return MDB_PANIC;
	}
	if ((env->me_flags & MDB_RDONLY) && !(flags & MDB_RDONLY))
		return EACCES;
	if (parent) {
		/* Nested transactions: Max 1 child, write txns only, no writemap */
		if (parent->mt_child ||
			(flags & MDB_RDONLY) || (parent->mt_flags & MDB_TXN_RDONLY) ||
			(env->me_flags & MDB_WRITEMAP))
		{
			return EINVAL;
		}
		tsize = sizeof(MDB_ntxn);
	}
	size = tsize + env->me_maxdbs * (sizeof(MDB_db)+1);
	if (!(flags & MDB_RDONLY))
		size += env->me_maxdbs * sizeof(MDB_cursor *);

	if ((txn = calloc(1, size)) == NULL) {
		DPRINTF("calloc: %s", strerror(ErrCode()));
		return ENOMEM;
	}
	txn->mt_dbs = (MDB_db *) ((char *)txn + tsize);
	if (flags & MDB_RDONLY) {
		txn->mt_flags |= MDB_TXN_RDONLY;
		txn->mt_dbflags = (unsigned char *)(txn->mt_dbs + env->me_maxdbs);
	} else {
		txn->mt_cursors = (MDB_cursor **)(txn->mt_dbs + env->me_maxdbs);
		txn->mt_dbflags = (unsigned char *)(txn->mt_cursors + env->me_maxdbs);
	}
	txn->mt_env = env;

	if (parent) {
		unsigned int i;
		txn->mt_u.dirty_list = malloc(sizeof(MDB_ID2)*MDB_IDL_UM_SIZE);
		if (!txn->mt_u.dirty_list ||
			!(txn->mt_free_pgs = mdb_midl_alloc(MDB_IDL_UM_MAX)))
		{
			free(txn->mt_u.dirty_list);
			free(txn);
			return ENOMEM;
		}
		txn->mt_txnid = parent->mt_txnid;
		txn->mt_toggle = parent->mt_toggle;
		txn->mt_dirty_room = parent->mt_dirty_room;
		txn->mt_u.dirty_list[0].mid = 0;
		txn->mt_spill_pgs = NULL;
		txn->mt_next_pgno = parent->mt_next_pgno;
		parent->mt_child = txn;
		txn->mt_parent = parent;
		txn->mt_numdbs = parent->mt_numdbs;
		txn->mt_flags = parent->mt_flags;
		txn->mt_dbxs = parent->mt_dbxs;
		memcpy(txn->mt_dbs, parent->mt_dbs, txn->mt_numdbs * sizeof(MDB_db));
		/* Copy parent's mt_dbflags, but clear DB_NEW */
		for (i=0; i<txn->mt_numdbs; i++)
			txn->mt_dbflags[i] = parent->mt_dbflags[i] & ~DB_NEW;
		rc = 0;
		ntxn = (MDB_ntxn *)txn;
		ntxn->mnt_pgstate = env->me_pgstate; /* save parent me_pghead & co */
		if (env->me_pghead) {
			size = MDB_IDL_SIZEOF(env->me_pghead);
			env->me_pghead = mdb_midl_alloc(env->me_pghead[0]);
			if (env->me_pghead)
				memcpy(env->me_pghead, ntxn->mnt_pgstate.mf_pghead, size);
			else
				rc = ENOMEM;
		}
		if (!rc)
			rc = mdb_cursor_shadow(parent, txn);
		if (rc)
			mdb_txn_reset0(txn, "beginchild-fail");
	} else {
		rc = mdb_txn_renew0(txn);
	}
	if (rc)
		free(txn);
	else {
		*ret = txn;
		DPRINTF("begin txn %zu%c %p on mdbenv %p, root page %zu",
			txn->mt_txnid, (txn->mt_flags & MDB_TXN_RDONLY) ? 'r' : 'w',
			(void *) txn, (void *) env, txn->mt_dbs[MAIN_DBI].md_root);
	}

	return rc;
}

/** Export or close DBI handles opened in this txn. */
static void
mdb_dbis_update(MDB_txn *txn, int keep)
{
	int i;
	MDB_dbi n = txn->mt_numdbs;
	MDB_env *env = txn->mt_env;
	unsigned char *tdbflags = txn->mt_dbflags;

	for (i = n; --i >= 2;) {
		if (tdbflags[i] & DB_NEW) {
			if (keep) {
				env->me_dbflags[i] = txn->mt_dbs[i].md_flags | MDB_VALID;
			} else {
				char *ptr = env->me_dbxs[i].md_name.mv_data;
				env->me_dbxs[i].md_name.mv_data = NULL;
				env->me_dbxs[i].md_name.mv_size = 0;
				env->me_dbflags[i] = 0;
				free(ptr);
			}
		}
	}
	if (keep && env->me_numdbs < n)
		env->me_numdbs = n;
}

/** Common code for #mdb_txn_reset() and #mdb_txn_abort().
 * May be called twice for readonly txns: First reset it, then abort.
 * @param[in] txn the transaction handle to reset
 */
static void
mdb_txn_reset0(MDB_txn *txn, const char *act)
{
	MDB_env	*env = txn->mt_env;

	/* Close any DBI handles opened in this txn */
	mdb_dbis_update(txn, 0);

	DPRINTF("%s txn %zu%c %p on mdbenv %p, root page %zu",
		act, txn->mt_txnid, (txn->mt_flags & MDB_TXN_RDONLY) ? 'r' : 'w',
		(void *) txn, (void *)env, txn->mt_dbs[MAIN_DBI].md_root);

	if (F_ISSET(txn->mt_flags, MDB_TXN_RDONLY)) {
		if (txn->mt_u.reader) {
			txn->mt_u.reader->mr_txnid = (txnid_t)-1;
			if (!(env->me_flags & MDB_NOTLS))
				txn->mt_u.reader = NULL; /* txn does not own reader */
		}
		txn->mt_numdbs = 0;		/* close nothing if called again */
		txn->mt_dbxs = NULL;	/* mark txn as reset */
	} else {
		mdb_cursors_close(txn, 0);

		if (!(env->me_flags & MDB_WRITEMAP)) {
			mdb_dlist_free(txn);
		}
		mdb_midl_free(env->me_pghead);

		if (txn->mt_parent) {
			txn->mt_parent->mt_child = NULL;
			env->me_pgstate = ((MDB_ntxn *)txn)->mnt_pgstate;
			mdb_midl_free(txn->mt_free_pgs);
			mdb_midl_free(txn->mt_spill_pgs);
			free(txn->mt_u.dirty_list);
			return;
		}

		if (mdb_midl_shrink(&txn->mt_free_pgs))
			env->me_free_pgs = txn->mt_free_pgs;
		env->me_pghead = NULL;
		env->me_pglast = 0;

		env->me_txn = NULL;
		/* The writer mutex was locked in mdb_txn_begin. */
		UNLOCK_MUTEX_W(env);
	}
}

void
mdb_txn_reset(MDB_txn *txn)
{
	if (txn == NULL)
		return;

	/* This call is only valid for read-only txns */
	if (!(txn->mt_flags & MDB_TXN_RDONLY))
		return;

	mdb_txn_reset0(txn, "reset");
}

void
mdb_txn_abort(MDB_txn *txn)
{
	if (txn == NULL)
		return;

	if (txn->mt_child)
		mdb_txn_abort(txn->mt_child);

	mdb_txn_reset0(txn, "abort");
	/* Free reader slot tied to this txn (if MDB_NOTLS && writable FS) */
	if ((txn->mt_flags & MDB_TXN_RDONLY) && txn->mt_u.reader)
		txn->mt_u.reader->mr_pid = 0;

	free(txn);
}

/** Save the freelist as of this transaction to the freeDB.
 * This changes the freelist. Keep trying until it stabilizes.
 */
static int
mdb_freelist_save(MDB_txn *txn)
{
	/* env->me_pghead[] can grow and shrink during this call.
	 * env->me_pglast and txn->mt_free_pgs[] can only grow.
	 * Page numbers cannot disappear from txn->mt_free_pgs[].
	 */
	MDB_cursor mc;
	MDB_env	*env = txn->mt_env;
	int rc, maxfree_1pg = env->me_maxfree_1pg, more = 1;
	txnid_t	pglast = 0, head_id = 0;
	pgno_t	freecnt = 0, *free_pgs, *mop;
	ssize_t	head_room = 0, total_room = 0, mop_len;

	mdb_cursor_init(&mc, txn, FREE_DBI, NULL);

	if (env->me_pghead) {
		/* Make sure first page of freeDB is touched and on freelist */
		rc = mdb_page_search(&mc, NULL, MDB_PS_MODIFY);
		if (rc && rc != MDB_NOTFOUND)
			return rc;
	}

	for (;;) {
		/* Come back here after each Put() in case freelist changed */
		MDB_val key, data;

		/* If using records from freeDB which we have not yet
		 * deleted, delete them and any we reserved for me_pghead.
		 */
		while (pglast < env->me_pglast) {
			rc = mdb_cursor_first(&mc, &key, NULL);
			if (rc)
				return rc;
			pglast = head_id = *(txnid_t *)key.mv_data;
			total_room = head_room = 0;
			assert(pglast <= env->me_pglast);
			rc = mdb_cursor_del(&mc, 0);
			if (rc)
				return rc;
		}

		/* Save the IDL of pages freed by this txn, to a single record */
		if (freecnt < txn->mt_free_pgs[0]) {
			if (!freecnt) {
				/* Make sure last page of freeDB is touched and on freelist */
				key.mv_size = MDB_MAXKEYSIZE+1;
				key.mv_data = NULL;
				rc = mdb_page_search(&mc, &key, MDB_PS_MODIFY);
				if (rc && rc != MDB_NOTFOUND)
					return rc;
			}
			free_pgs = txn->mt_free_pgs;
			/* Write to last page of freeDB */
			key.mv_size = sizeof(txn->mt_txnid);
			key.mv_data = &txn->mt_txnid;
			do {
				freecnt = free_pgs[0];
				data.mv_size = MDB_IDL_SIZEOF(free_pgs);
				rc = mdb_cursor_put(&mc, &key, &data, MDB_RESERVE);
				if (rc)
					return rc;
				/* Retry if mt_free_pgs[] grew during the Put() */
				free_pgs = txn->mt_free_pgs;
			} while (freecnt < free_pgs[0]);
			mdb_midl_sort(free_pgs);
			memcpy(data.mv_data, free_pgs, data.mv_size);
#if MDB_DEBUG > 1
			{
				unsigned int i = free_pgs[0];
				DPRINTF("IDL write txn %zu root %zu num %u",
					txn->mt_txnid, txn->mt_dbs[FREE_DBI].md_root, i);
				for (; i; i--)
					DPRINTF("IDL %zu", free_pgs[i]);
			}
#endif
			continue;
		}

		mop = env->me_pghead;
		mop_len = mop ? mop[0] : 0;

		/* Reserve records for me_pghead[]. Split it if multi-page,
		 * to avoid searching freeDB for a page range. Use keys in
		 * range [1,me_pglast]: Smaller than txnid of oldest reader.
		 */
		if (total_room >= mop_len) {
			if (total_room == mop_len || --more < 0)
				break;
		} else if (head_room >= maxfree_1pg && head_id > 1) {
			/* Keep current record (overflow page), add a new one */
			head_id--;
			head_room = 0;
		}
		/* (Re)write {key = head_id, IDL length = head_room} */
		total_room -= head_room;
		head_room = mop_len - total_room;
		if (head_room > maxfree_1pg && head_id > 1) {
			/* Overflow multi-page for part of me_pghead */
			head_room /= head_id; /* amortize page sizes */
			head_room += maxfree_1pg - head_room % (maxfree_1pg + 1);
		} else if (head_room < 0) {
			/* Rare case, not bothering to delete this record */
			head_room = 0;
		}
		key.mv_size = sizeof(head_id);
		key.mv_data = &head_id;
		data.mv_size = (head_room + 1) * sizeof(pgno_t);
		rc = mdb_cursor_put(&mc, &key, &data, MDB_RESERVE);
		if (rc)
			return rc;
		*(MDB_ID *)data.mv_data = 0; /* IDL is initially empty */
		total_room += head_room;
	}

	/* Fill in the reserved, touched me_pghead records */
	rc = MDB_SUCCESS;
	if (mop_len) {
		MDB_val key, data;

		mop += mop_len;
		rc = mdb_cursor_first(&mc, &key, &data);
		for (; !rc; rc = mdb_cursor_next(&mc, &key, &data, MDB_NEXT)) {
			unsigned flags = MDB_CURRENT;
			txnid_t id = *(txnid_t *)key.mv_data;
			ssize_t	len = (ssize_t)(data.mv_size / sizeof(MDB_ID)) - 1;
			MDB_ID save;

			assert(len >= 0 && id <= env->me_pglast);
			key.mv_data = &id;
			if (len > mop_len) {
				len = mop_len;
				data.mv_size = (len + 1) * sizeof(MDB_ID);
				flags = 0;
			}
			data.mv_data = mop -= len;
			save = mop[0];
			mop[0] = len;
			rc = mdb_cursor_put(&mc, &key, &data, flags);
			mop[0] = save;
			if (rc || !(mop_len -= len))
				break;
		}
	}
	return rc;
}

/** Flush dirty pages to the map, after clearing their dirty flag.
 */
static int
mdb_page_flush(MDB_txn *txn)
{
	MDB_env		*env = txn->mt_env;
	MDB_ID2L	dl = txn->mt_u.dirty_list;
	unsigned	psize = env->me_psize, j;
	int			i, pagecount = dl[0].mid, rc;
	size_t		size = 0, pos = 0;
	pgno_t		pgno = 0;
	MDB_page	*dp = NULL;
#ifdef _WIN32
	OVERLAPPED	ov;
#else
	struct iovec iov[MDB_COMMIT_PAGES];
	ssize_t		wpos = 0, wsize = 0, wres;
	size_t		next_pos = 1; /* impossible pos, so pos != next_pos */
	int			n = 0;
#endif

	j = 0;
	if (env->me_flags & MDB_WRITEMAP) {
		/* Clear dirty flags */
		for (i = pagecount; i; i--) {
			dp = dl[i].mptr;
			/* Don't flush this page yet */
			if (dp->mp_flags & P_KEEP) {
				dp->mp_flags ^= P_KEEP;
				dl[++j] = dl[i];
				continue;
			}
			dp->mp_flags &= ~P_DIRTY;
		}
		dl[0].mid = j;
		return MDB_SUCCESS;
	}

	/* Write the pages */
	for (i = 1;; i++) {
		if (i <= pagecount) {
			dp = dl[i].mptr;
			/* Don't flush this page yet */
			if (dp->mp_flags & P_KEEP) {
				dp->mp_flags ^= P_KEEP;
				dl[i].mid = 0;
				continue;
			}
			pgno = dl[i].mid;
			/* clear dirty flag */
			dp->mp_flags &= ~P_DIRTY;
			pos = pgno * psize;
			size = psize;
			if (IS_OVERFLOW(dp)) size *= dp->mp_pages;
		}
#ifdef _WIN32
		else break;

		/* Windows actually supports scatter/gather I/O, but only on
		 * unbuffered file handles. Since we're relying on the OS page
		 * cache for all our data, that's self-defeating. So we just
		 * write pages one at a time. We use the ov structure to set
		 * the write offset, to at least save the overhead of a Seek
		 * system call.
		 */
		DPRINTF("committing page %zu", pgno);
		memset(&ov, 0, sizeof(ov));
		ov.Offset = pos & 0xffffffff;
		ov.OffsetHigh = pos >> 16 >> 16;
		if (!WriteFile(env->me_fd, dp, size, NULL, &ov)) {
			rc = ErrCode();
			DPRINTF("WriteFile: %d", rc);
			return rc;
		}
#else
		/* Write up to MDB_COMMIT_PAGES dirty pages at a time. */
		if (pos!=next_pos || n==MDB_COMMIT_PAGES || wsize+size>MAX_WRITE) {
			if (n) {
				/* Write previous page(s) */
#ifdef MDB_USE_PWRITEV
				wres = pwritev(env->me_fd, iov, n, wpos);
#else
				if (n == 1) {
					wres = pwrite(env->me_fd, iov[0].iov_base, wsize, wpos);
				} else {
					if (lseek(env->me_fd, wpos, SEEK_SET) == -1) {
						rc = ErrCode();
						DPRINTF("lseek: %s", strerror(rc));
						return rc;
					}
					wres = writev(env->me_fd, iov, n);
				}
#endif
				if (wres != wsize) {
					if (wres < 0) {
						rc = ErrCode();
						DPRINTF("Write error: %s", strerror(rc));
					} else {
						rc = EIO; /* TODO: Use which error code? */
						DPUTS("short write, filesystem full?");
					}
					return rc;
				}
				n = 0;
			}
			if (i > pagecount)
				break;
			wpos = pos;
			wsize = 0;
		}
		DPRINTF("committing page %zu", pgno);
		next_pos = pos + size;
		iov[n].iov_len = size;
		iov[n].iov_base = (char *)dp;
		wsize += size;
		n++;
#endif	/* _WIN32 */
	}

	j = 0;
	for (i=1; i<=pagecount; i++) {
		dp = dl[i].mptr;
		/* This is a page we skipped above */
		if (!dl[i].mid) {
			dl[++j] = dl[i];
			dl[j].mid = dp->mp_pgno;
			continue;
		}
		mdb_dpage_free(env, dp);
	}
	dl[0].mid = j;

	return MDB_SUCCESS;
}

int
mdb_txn_commit(MDB_txn *txn)
{
	int		rc;
	unsigned int i;
	MDB_env	*env;

	assert(txn != NULL);
	assert(txn->mt_env != NULL);

	if (txn->mt_child) {
		rc = mdb_txn_commit(txn->mt_child);
		txn->mt_child = NULL;
		if (rc)
			goto fail;
	}

	env = txn->mt_env;

	if (F_ISSET(txn->mt_flags, MDB_TXN_RDONLY)) {
		mdb_dbis_update(txn, 1);
		txn->mt_numdbs = 2; /* so txn_abort() doesn't close any new handles */
		mdb_txn_abort(txn);
		return MDB_SUCCESS;
	}

	if (F_ISSET(txn->mt_flags, MDB_TXN_ERROR)) {
		DPUTS("error flag is set, can't commit");
		if (txn->mt_parent)
			txn->mt_parent->mt_flags |= MDB_TXN_ERROR;
		rc = EINVAL;
		goto fail;
	}

	if (txn->mt_parent) {
		MDB_txn *parent = txn->mt_parent;
		unsigned x, y, len;
		MDB_ID2L dst, src;

		/* Append our free list to parent's */
		rc = mdb_midl_append_list(&parent->mt_free_pgs, txn->mt_free_pgs);
		if (rc)
			goto fail;
		mdb_midl_free(txn->mt_free_pgs);

		parent->mt_next_pgno = txn->mt_next_pgno;
		parent->mt_flags = txn->mt_flags;

		/* Merge our cursors into parent's and close them */
		mdb_cursors_close(txn, 1);

		/* Update parent's DB table. */
		memcpy(parent->mt_dbs, txn->mt_dbs, txn->mt_numdbs * sizeof(MDB_db));
		parent->mt_numdbs = txn->mt_numdbs;
		parent->mt_dbflags[0] = txn->mt_dbflags[0];
		parent->mt_dbflags[1] = txn->mt_dbflags[1];
		for (i=2; i<txn->mt_numdbs; i++) {
			/* preserve parent's DB_NEW status */
			x = parent->mt_dbflags[i] & DB_NEW;
			parent->mt_dbflags[i] = txn->mt_dbflags[i] | x;
		}

		dst = parent->mt_u.dirty_list;
		src = txn->mt_u.dirty_list;
		/* Remove anything in our dirty list from parent's spill list */
		if (parent->mt_spill_pgs) {
			x = parent->mt_spill_pgs[0];
			len = x;
			/* zero out our dirty pages in parent spill list */
			for (i=1; i<=src[0].mid; i++) {
				if (src[i].mid < parent->mt_spill_pgs[x])
					continue;
				if (src[i].mid > parent->mt_spill_pgs[x]) {
					if (x <= 1)
						break;
					x--;
					continue;
				}
				parent->mt_spill_pgs[x] = 0;
				len--;
			}
			/* OK, we had a few hits, squash zeros from the spill list */
			if (len < parent->mt_spill_pgs[0]) {
				x=1;
				for (y=1; y<=parent->mt_spill_pgs[0]; y++) {
					if (parent->mt_spill_pgs[y]) {
						if (y != x) {
							parent->mt_spill_pgs[x] = parent->mt_spill_pgs[y];
						}
						x++;
					}
				}
				parent->mt_spill_pgs[0] = len;
			}
		}
		/* Find len = length of merging our dirty list with parent's */
		x = dst[0].mid;
		dst[0].mid = 0;		/* simplify loops */
		if (parent->mt_parent) {
			len = x + src[0].mid;
			y = mdb_mid2l_search(src, dst[x].mid + 1) - 1;
			for (i = x; y && i; y--) {
				pgno_t yp = src[y].mid;
				while (yp < dst[i].mid)
					i--;
				if (yp == dst[i].mid) {
					i--;
					len--;
				}
			}
		} else { /* Simplify the above for single-ancestor case */
			len = MDB_IDL_UM_MAX - txn->mt_dirty_room;
		}
		/* Merge our dirty list with parent's */
		y = src[0].mid;
		for (i = len; y; dst[i--] = src[y--]) {
			pgno_t yp = src[y].mid;
			while (yp < dst[x].mid)
				dst[i--] = dst[x--];
			if (yp == dst[x].mid)
				free(dst[x--].mptr);
		}
		assert(i == x);
		dst[0].mid = len;
		free(txn->mt_u.dirty_list);
		parent->mt_dirty_room = txn->mt_dirty_room;
		if (txn->mt_spill_pgs) {
			if (parent->mt_spill_pgs) {
				mdb_midl_append_list(&parent->mt_spill_pgs, txn->mt_spill_pgs);
				mdb_midl_free(txn->mt_spill_pgs);
				mdb_midl_sort(parent->mt_spill_pgs);
			} else {
				parent->mt_spill_pgs = txn->mt_spill_pgs;
			}
		}

		parent->mt_child = NULL;
		mdb_midl_free(((MDB_ntxn *)txn)->mnt_pgstate.mf_pghead);
		free(txn);
		return MDB_SUCCESS;
	}

	if (txn != env->me_txn) {
		DPUTS("attempt to commit unknown transaction");
		rc = EINVAL;
		goto fail;
	}

	mdb_cursors_close(txn, 0);

	if (!txn->mt_u.dirty_list[0].mid && !(txn->mt_flags & MDB_TXN_DIRTY))
		goto done;

	DPRINTF("committing txn %zu %p on mdbenv %p, root page %zu",
	    txn->mt_txnid, (void *)txn, (void *)env, txn->mt_dbs[MAIN_DBI].md_root);

	/* Update DB root pointers */
	if (txn->mt_numdbs > 2) {
		MDB_cursor mc;
		MDB_dbi i;
		MDB_val data;
		data.mv_size = sizeof(MDB_db);

		mdb_cursor_init(&mc, txn, MAIN_DBI, NULL);
		for (i = 2; i < txn->mt_numdbs; i++) {
			if (txn->mt_dbflags[i] & DB_DIRTY) {
				data.mv_data = &txn->mt_dbs[i];
				rc = mdb_cursor_put(&mc, &txn->mt_dbxs[i].md_name, &data, 0);
				if (rc)
					goto fail;
			}
		}
	}

	rc = mdb_freelist_save(txn);
	if (rc)
		goto fail;

	mdb_midl_free(env->me_pghead);
	env->me_pghead = NULL;
	if (mdb_midl_shrink(&txn->mt_free_pgs))
		env->me_free_pgs = txn->mt_free_pgs;

#if MDB_DEBUG > 2
	mdb_audit(txn);
#endif

	if ((rc = mdb_page_flush(txn)) ||
		(rc = mdb_env_sync(env, 0)) ||
		(rc = mdb_env_write_meta(txn)))
		goto fail;

done:
	env->me_pglast = 0;
	env->me_txn = NULL;
	mdb_dbis_update(txn, 1);

	UNLOCK_MUTEX_W(env);
	free(txn);

	return MDB_SUCCESS;

fail:
	mdb_txn_abort(txn);
	return rc;
}

/** Read the environment parameters of a DB environment before
 * mapping it into memory.
 * @param[in] env the environment handle
 * @param[out] meta address of where to store the meta information
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_env_read_header(MDB_env *env, MDB_meta *meta)
{
	MDB_pagebuf	pbuf;
	MDB_page	*p;
	MDB_meta	*m;
	int			i, rc, off;

	/* We don't know the page size yet, so use a minimum value.
	 * Read both meta pages so we can use the latest one.
	 */

	for (i=off=0; i<2; i++, off = meta->mm_psize) {
#ifdef _WIN32
		DWORD len;
		OVERLAPPED ov;
		memset(&ov, 0, sizeof(ov));
		ov.Offset = off;
		rc = ReadFile(env->me_fd,&pbuf,MDB_PAGESIZE,&len,&ov) ? (int)len : -1;
		if (rc == -1 && ErrCode() == ERROR_HANDLE_EOF)
			rc = 0;
#else
		rc = pread(env->me_fd, &pbuf, MDB_PAGESIZE, off);
#endif
		if (rc != MDB_PAGESIZE) {
			if (rc == 0 && off == 0)
				return ENOENT;
			rc = rc < 0 ? (int) ErrCode() : MDB_INVALID;
			DPRINTF("read: %s", mdb_strerror(rc));
			return rc;
		}

		p = (MDB_page *)&pbuf;

		if (!F_ISSET(p->mp_flags, P_META)) {
			DPRINTF("page %zu not a meta page", p->mp_pgno);
			return MDB_INVALID;
		}

		m = METADATA(p);
		if (m->mm_magic != MDB_MAGIC) {
			DPUTS("meta has invalid magic");
			return MDB_INVALID;
		}

		if (m->mm_version != MDB_DATA_VERSION) {
			DPRINTF("database is version %u, expected version %u",
				m->mm_version, MDB_DATA_VERSION);
			return MDB_VERSION_MISMATCH;
		}

		if (off == 0 || m->mm_txnid > meta->mm_txnid)
			*meta = *m;
	}
	return 0;
}

/** Write the environment parameters of a freshly created DB environment.
 * @param[in] env the environment handle
 * @param[out] meta address of where to store the meta information
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_env_init_meta(MDB_env *env, MDB_meta *meta)
{
	MDB_page *p, *q;
	int rc;
	unsigned int	 psize;

	DPUTS("writing new meta page");

	GET_PAGESIZE(psize);

	meta->mm_magic = MDB_MAGIC;
	meta->mm_version = MDB_DATA_VERSION;
	meta->mm_mapsize = env->me_mapsize;
	meta->mm_psize = psize;
	meta->mm_last_pg = 1;
	meta->mm_flags = env->me_flags & 0xffff;
	meta->mm_flags |= MDB_INTEGERKEY;
	meta->mm_dbs[0].md_root = P_INVALID;
	meta->mm_dbs[1].md_root = P_INVALID;

	p = calloc(2, psize);
	p->mp_pgno = 0;
	p->mp_flags = P_META;
	*(MDB_meta *)METADATA(p) = *meta;

	q = (MDB_page *)((char *)p + psize);
	q->mp_pgno = 1;
	q->mp_flags = P_META;
	*(MDB_meta *)METADATA(q) = *meta;

#ifdef _WIN32
	{
		DWORD len;
		OVERLAPPED ov;
		memset(&ov, 0, sizeof(ov));
		rc = WriteFile(env->me_fd, p, psize * 2, &len, &ov);
		rc = rc ? (len == psize * 2 ? MDB_SUCCESS : EIO) : ErrCode();
	}
#else
	rc = pwrite(env->me_fd, p, psize * 2, 0);
	rc = (rc == (int)psize * 2) ? MDB_SUCCESS : rc < 0 ? ErrCode() : EIO;
#endif
	free(p);
	return rc;
}

/** Update the environment info to commit a transaction.
 * @param[in] txn the transaction that's being committed
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_env_write_meta(MDB_txn *txn)
{
	MDB_env *env;
	MDB_meta	meta, metab, *mp;
	off_t off;
	int rc, len, toggle;
	char *ptr;
	HANDLE mfd;
#ifdef _WIN32
	OVERLAPPED ov;
#else
	int r2;
#endif

	assert(txn != NULL);
	assert(txn->mt_env != NULL);

	toggle = !txn->mt_toggle;
	DPRINTF("writing meta page %d for root page %zu",
		toggle, txn->mt_dbs[MAIN_DBI].md_root);

	env = txn->mt_env;
	mp = env->me_metas[toggle];

	if (env->me_flags & MDB_WRITEMAP) {
		/* Persist any increases of mapsize config */
		if (env->me_mapsize > mp->mm_mapsize)
			mp->mm_mapsize = env->me_mapsize;
		mp->mm_dbs[0] = txn->mt_dbs[0];
		mp->mm_dbs[1] = txn->mt_dbs[1];
		mp->mm_last_pg = txn->mt_next_pgno - 1;
		mp->mm_txnid = txn->mt_txnid;
		if (!(env->me_flags & (MDB_NOMETASYNC|MDB_NOSYNC))) {
			rc = (env->me_flags & MDB_MAPASYNC) ? MS_ASYNC : MS_SYNC;
			ptr = env->me_map;
			if (toggle)
				ptr += env->me_psize;
			if (MDB_MSYNC(ptr, env->me_psize, rc)) {
				rc = ErrCode();
				goto fail;
			}
		}
		goto done;
	}
	metab.mm_txnid = env->me_metas[toggle]->mm_txnid;
	metab.mm_last_pg = env->me_metas[toggle]->mm_last_pg;

	ptr = (char *)&meta;
	if (env->me_mapsize > mp->mm_mapsize) {
		/* Persist any increases of mapsize config */
		meta.mm_mapsize = env->me_mapsize;
		off = offsetof(MDB_meta, mm_mapsize);
	} else {
		off = offsetof(MDB_meta, mm_dbs[0].md_depth);
	}
	len = sizeof(MDB_meta) - off;

	ptr += off;
	meta.mm_dbs[0] = txn->mt_dbs[0];
	meta.mm_dbs[1] = txn->mt_dbs[1];
	meta.mm_last_pg = txn->mt_next_pgno - 1;
	meta.mm_txnid = txn->mt_txnid;

	if (toggle)
		off += env->me_psize;
	off += PAGEHDRSZ;

	/* Write to the SYNC fd */
	mfd = env->me_flags & (MDB_NOSYNC|MDB_NOMETASYNC) ?
		env->me_fd : env->me_mfd;
#ifdef _WIN32
	{
		memset(&ov, 0, sizeof(ov));
		ov.Offset = off;
		if (!WriteFile(mfd, ptr, len, (DWORD *)&rc, &ov))
			rc = -1;
	}
#else
	rc = pwrite(mfd, ptr, len, off);
#endif
	if (rc != len) {
		rc = rc < 0 ? ErrCode() : EIO;
		DPUTS("write failed, disk error?");
		/* On a failure, the pagecache still contains the new data.
		 * Write some old data back, to prevent it from being used.
		 * Use the non-SYNC fd; we know it will fail anyway.
		 */
		meta.mm_last_pg = metab.mm_last_pg;
		meta.mm_txnid = metab.mm_txnid;
#ifdef _WIN32
		memset(&ov, 0, sizeof(ov));
		ov.Offset = off;
		WriteFile(env->me_fd, ptr, len, NULL, &ov);
#else
		r2 = pwrite(env->me_fd, ptr, len, off);
#endif
fail:
		env->me_flags |= MDB_FATAL_ERROR;
		return rc;
	}
done:
	/* Memory ordering issues are irrelevant; since the entire writer
	 * is wrapped by wmutex, all of these changes will become visible
	 * after the wmutex is unlocked. Since the DB is multi-version,
	 * readers will get consistent data regardless of how fresh or
	 * how stale their view of these values is.
	 */
	env->me_txns->mti_txnid = txn->mt_txnid;

	return MDB_SUCCESS;
}

/** Check both meta pages to see which one is newer.
 * @param[in] env the environment handle
 * @return meta toggle (0 or 1).
 */
static int
mdb_env_pick_meta(const MDB_env *env)
{
	return (env->me_metas[0]->mm_txnid < env->me_metas[1]->mm_txnid);
}

int
mdb_env_create(MDB_env **env)
{
	MDB_env *e;

	e = calloc(1, sizeof(MDB_env));
	if (!e)
		return ENOMEM;

	e->me_maxreaders = DEFAULT_READERS;
	e->me_maxdbs = e->me_numdbs = 2;
	e->me_fd = INVALID_HANDLE_VALUE;
	e->me_lfd = INVALID_HANDLE_VALUE;
	e->me_mfd = INVALID_HANDLE_VALUE;
#ifdef MDB_USE_POSIX_SEM
	e->me_rmutex = SEM_FAILED;
	e->me_wmutex = SEM_FAILED;
#endif
	e->me_pid = getpid();
	VGMEMP_CREATE(e,0,0);
	*env = e;
	return MDB_SUCCESS;
}

int
mdb_env_set_mapsize(MDB_env *env, size_t size)
{
	if (env->me_map)
		return EINVAL;
	env->me_mapsize = size;
	if (env->me_psize)
		env->me_maxpg = env->me_mapsize / env->me_psize;
	return MDB_SUCCESS;
}

int
mdb_env_set_maxdbs(MDB_env *env, MDB_dbi dbs)
{
	if (env->me_map)
		return EINVAL;
	env->me_maxdbs = dbs + 2; /* Named databases + main and free DB */
	return MDB_SUCCESS;
}

int
mdb_env_set_maxreaders(MDB_env *env, unsigned int readers)
{
	if (env->me_map || readers < 1)
		return EINVAL;
	env->me_maxreaders = readers;
	return MDB_SUCCESS;
}

int
mdb_env_get_maxreaders(MDB_env *env, unsigned int *readers)
{
	if (!env || !readers)
		return EINVAL;
	*readers = env->me_maxreaders;
	return MDB_SUCCESS;
}

/** Further setup required for opening an MDB environment
 */
static int
mdb_env_open2(MDB_env *env)
{
	unsigned int flags = env->me_flags;
	int i, newenv = 0;
	MDB_meta meta;
	MDB_page *p;
#ifndef _WIN32
	int prot;
#endif

	memset(&meta, 0, sizeof(meta));

	if ((i = mdb_env_read_header(env, &meta)) != 0) {
		if (i != ENOENT)
			return i;
		DPUTS("new mdbenv");
		newenv = 1;
	}

	/* Was a mapsize configured? */
	if (!env->me_mapsize) {
		/* If this is a new environment, take the default,
		 * else use the size recorded in the existing env.
		 */
		env->me_mapsize = newenv ? DEFAULT_MAPSIZE : meta.mm_mapsize;
	} else if (env->me_mapsize < meta.mm_mapsize) {
		/* If the configured size is smaller, make sure it's
		 * still big enough. Silently round up to minimum if not.
		 */
		size_t minsize = (meta.mm_last_pg + 1) * meta.mm_psize;
		if (env->me_mapsize < minsize)
			env->me_mapsize = minsize;
	}

#ifdef _WIN32
	{
		int rc;
		HANDLE mh;
		LONG sizelo, sizehi;
		sizelo = env->me_mapsize & 0xffffffff;
		sizehi = env->me_mapsize >> 16 >> 16; /* only needed on Win64 */

		/* See if we should use QueryLimited */
		rc = GetVersion();
		if ((rc & 0xff) > 5)
			env->me_pidquery = PROCESS_QUERY_LIMITED_INFORMATION;
		else
			env->me_pidquery = PROCESS_QUERY_INFORMATION;

		/* Windows won't create mappings for zero length files.
		 * Just allocate the maxsize right now.
		 */
		if (newenv) {
			if (SetFilePointer(env->me_fd, sizelo, &sizehi, 0) != (DWORD)sizelo
				|| !SetEndOfFile(env->me_fd)
				|| SetFilePointer(env->me_fd, 0, NULL, 0) != 0)
				return ErrCode();
		}
		mh = CreateFileMapping(env->me_fd, NULL, flags & MDB_WRITEMAP ?
			PAGE_READWRITE : PAGE_READONLY,
			sizehi, sizelo, NULL);
		if (!mh)
			return ErrCode();
		env->me_map = MapViewOfFileEx(mh, flags & MDB_WRITEMAP ?
			FILE_MAP_WRITE : FILE_MAP_READ,
			0, 0, env->me_mapsize, meta.mm_address);
		rc = env->me_map ? 0 : ErrCode();
		CloseHandle(mh);
		if (rc)
			return rc;
	}
#else
	i = MAP_SHARED;
	prot = PROT_READ;
	if (flags & MDB_WRITEMAP) {
		prot |= PROT_WRITE;
		if (ftruncate(env->me_fd, env->me_mapsize) < 0)
			return ErrCode();
	}
	env->me_map = mmap(meta.mm_address, env->me_mapsize, prot, i,
		env->me_fd, 0);
	if (env->me_map == MAP_FAILED) {
		env->me_map = NULL;
		return ErrCode();
	}
	/* Turn off readahead. It's harmful when the DB is larger than RAM. */
#ifdef MADV_RANDOM
	madvise(env->me_map, env->me_mapsize, MADV_RANDOM);
#else
#ifdef POSIX_MADV_RANDOM
	posix_madvise(env->me_map, env->me_mapsize, POSIX_MADV_RANDOM);
#endif /* POSIX_MADV_RANDOM */
#endif /* MADV_RANDOM */
#endif /* _WIN32 */

	if (newenv) {
		if (flags & MDB_FIXEDMAP)
			meta.mm_address = env->me_map;
		i = mdb_env_init_meta(env, &meta);
		if (i != MDB_SUCCESS) {
			return i;
		}
	} else if (meta.mm_address && env->me_map != meta.mm_address) {
		/* Can happen because the address argument to mmap() is just a
		 * hint.  mmap() can pick another, e.g. if the range is in use.
		 * The MAP_FIXED flag would prevent that, but then mmap could
		 * instead unmap existing pages to make room for the new map.
		 */
		return EBUSY;	/* TODO: Make a new MDB_* error code? */
	}
	env->me_psize = meta.mm_psize;
	env->me_maxfree_1pg = (env->me_psize - PAGEHDRSZ) / sizeof(pgno_t) - 1;
	env->me_nodemax = (env->me_psize - PAGEHDRSZ) / MDB_MINKEYS;

	env->me_maxpg = env->me_mapsize / env->me_psize;

	p = (MDB_page *)env->me_map;
	env->me_metas[0] = METADATA(p);
	env->me_metas[1] = (MDB_meta *)((char *)env->me_metas[0] + meta.mm_psize);

#if MDB_DEBUG
	{
		int toggle = mdb_env_pick_meta(env);
		MDB_db *db = &env->me_metas[toggle]->mm_dbs[MAIN_DBI];

		DPRINTF("opened database version %u, pagesize %u",
			env->me_metas[0]->mm_version, env->me_psize);
		DPRINTF("using meta page %d",  toggle);
		DPRINTF("depth: %u",           db->md_depth);
		DPRINTF("entries: %zu",        db->md_entries);
		DPRINTF("branch pages: %zu",   db->md_branch_pages);
		DPRINTF("leaf pages: %zu",     db->md_leaf_pages);
		DPRINTF("overflow pages: %zu", db->md_overflow_pages);
		DPRINTF("root: %zu",           db->md_root);
	}
#endif

	return MDB_SUCCESS;
}


/** Release a reader thread's slot in the reader lock table.
 *	This function is called automatically when a thread exits.
 * @param[in] ptr This points to the slot in the reader lock table.
 */
static void
mdb_env_reader_dest(void *ptr)
{
	MDB_reader *reader = ptr;

	reader->mr_pid = 0;
}

#ifdef _WIN32
/** Junk for arranging thread-specific callbacks on Windows. This is
 *	necessarily platform and compiler-specific. Windows supports up
 *	to 1088 keys. Let's assume nobody opens more than 64 environments
 *	in a single process, for now. They can override this if needed.
 */
#ifndef MAX_TLS_KEYS
#define MAX_TLS_KEYS	64
#endif
static pthread_key_t mdb_tls_keys[MAX_TLS_KEYS];
static int mdb_tls_nkeys;

static void NTAPI mdb_tls_callback(PVOID module, DWORD reason, PVOID ptr)
{
	int i;
	switch(reason) {
	case DLL_PROCESS_ATTACH: break;
	case DLL_THREAD_ATTACH: break;
	case DLL_THREAD_DETACH:
		for (i=0; i<mdb_tls_nkeys; i++) {
			MDB_reader *r = pthread_getspecific(mdb_tls_keys[i]);
			mdb_env_reader_dest(r);
		}
		break;
	case DLL_PROCESS_DETACH: break;
	}
}
#ifdef __GNUC__
#ifdef _WIN64
const PIMAGE_TLS_CALLBACK mdb_tls_cbp __attribute__((section (".CRT$XLB"))) = mdb_tls_callback;
#else
PIMAGE_TLS_CALLBACK mdb_tls_cbp __attribute__((section (".CRT$XLB"))) = mdb_tls_callback;
#endif
#else
#ifdef _WIN64
/* Force some symbol references.
 *	_tls_used forces the linker to create the TLS directory if not already done
 *	mdb_tls_cbp prevents whole-program-optimizer from dropping the symbol.
 */
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:mdb_tls_cbp")
#pragma const_seg(".CRT$XLB")
extern const PIMAGE_TLS_CALLBACK mdb_tls_callback;
const PIMAGE_TLS_CALLBACK mdb_tls_cbp = mdb_tls_callback;
#pragma const_seg()
#else	/* WIN32 */
#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma comment(linker, "/INCLUDE:_mdb_tls_cbp")
#pragma data_seg(".CRT$XLB")
PIMAGE_TLS_CALLBACK mdb_tls_cbp = mdb_tls_callback;
#pragma data_seg()
#endif	/* WIN 32/64 */
#endif	/* !__GNUC__ */
#endif

/** Downgrade the exclusive lock on the region back to shared */
static int
mdb_env_share_locks(MDB_env *env, int *excl)
{
	int rc = 0, toggle = mdb_env_pick_meta(env);

	env->me_txns->mti_txnid = env->me_metas[toggle]->mm_txnid;

#ifdef _WIN32
	{
		OVERLAPPED ov;
		/* First acquire a shared lock. The Unlock will
		 * then release the existing exclusive lock.
		 */
		memset(&ov, 0, sizeof(ov));
		if (!LockFileEx(env->me_lfd, 0, 0, 1, 0, &ov)) {
			rc = ErrCode();
		} else {
			UnlockFile(env->me_lfd, 0, 0, 1, 0);
			*excl = 0;
		}
	}
#else
	{
		struct flock lock_info;
		/* The shared lock replaces the existing lock */
		memset((void *)&lock_info, 0, sizeof(lock_info));
		lock_info.l_type = F_RDLCK;
		lock_info.l_whence = SEEK_SET;
		lock_info.l_start = 0;
		lock_info.l_len = 1;
		while ((rc = fcntl(env->me_lfd, F_SETLK, &lock_info)) &&
				(rc = ErrCode()) == EINTR) ;
		*excl = rc ? -1 : 0;	/* error may mean we lost the lock */
	}
#endif

	return rc;
}

/** Try to get exlusive lock, otherwise shared.
 *	Maintain *excl = -1: no/unknown lock, 0: shared, 1: exclusive.
 */
static int
mdb_env_excl_lock(MDB_env *env, int *excl)
{
	int rc = 0;
#ifdef _WIN32
	if (LockFile(env->me_lfd, 0, 0, 1, 0)) {
		*excl = 1;
	} else {
		OVERLAPPED ov;
		memset(&ov, 0, sizeof(ov));
		if (LockFileEx(env->me_lfd, 0, 0, 1, 0, &ov)) {
			*excl = 0;
		} else {
			rc = ErrCode();
		}
	}
#else
	struct flock lock_info;
	memset((void *)&lock_info, 0, sizeof(lock_info));
	lock_info.l_type = F_WRLCK;
	lock_info.l_whence = SEEK_SET;
	lock_info.l_start = 0;
	lock_info.l_len = 1;
	while ((rc = fcntl(env->me_lfd, F_SETLK, &lock_info)) &&
			(rc = ErrCode()) == EINTR) ;
	if (!rc) {
		*excl = 1;
	} else
# ifdef MDB_USE_POSIX_SEM
	if (*excl < 0) /* always true when !MDB_USE_POSIX_SEM */
# endif
	{
		lock_info.l_type = F_RDLCK;
		while ((rc = fcntl(env->me_lfd, F_SETLKW, &lock_info)) &&
				(rc = ErrCode()) == EINTR) ;
		if (rc == 0)
			*excl = 0;
	}
#endif
	return rc;
}

#if defined(_WIN32) || defined(MDB_USE_POSIX_SEM)
/*
 * hash_64 - 64 bit Fowler/Noll/Vo-0 FNV-1a hash code
 *
 * @(#) $Revision: 5.1 $
 * @(#) $Id: hash_64a.c,v 5.1 2009/06/30 09:01:38 chongo Exp $
 * @(#) $Source: /usr/local/src/cmd/fnv/RCS/hash_64a.c,v $
 *
 *	  http://www.isthe.com/chongo/tech/comp/fnv/index.html
 *
 ***
 *
 * Please do not copyright this code.  This code is in the public domain.
 *
 * LANDON CURT NOLL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO
 * EVENT SHALL LANDON CURT NOLL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * By:
 *	chongo <Landon Curt Noll> /\oo/\
 *	  http://www.isthe.com/chongo/
 *
 * Share and Enjoy!	:-)
 */

typedef unsigned long long	mdb_hash_t;
#define MDB_HASH_INIT ((mdb_hash_t)0xcbf29ce484222325ULL)

/** perform a 64 bit Fowler/Noll/Vo FNV-1a hash on a buffer
 * @param[in] str string to hash
 * @param[in] hval	initial value for hash
 * @return 64 bit hash
 *
 * NOTE: To use the recommended 64 bit FNV-1a hash, use MDB_HASH_INIT as the
 * 	 hval arg on the first call.
 */
static mdb_hash_t
mdb_hash_val(MDB_val *val, mdb_hash_t hval)
{
	unsigned char *s = (unsigned char *)val->mv_data;	/* unsigned string */
	unsigned char *end = s + val->mv_size;
	/*
	 * FNV-1a hash each octet of the string
	 */
	while (s < end) {
		/* xor the bottom with the current octet */
		hval ^= (mdb_hash_t)*s++;

		/* multiply by the 64 bit FNV magic prime mod 2^64 */
		hval += (hval << 1) + (hval << 4) + (hval << 5) +
			(hval << 7) + (hval << 8) + (hval << 40);
	}
	/* return our new hash value */
	return hval;
}

/** Hash the string and output the hash in hex.
 * @param[in] str string to hash
 * @param[out] hexbuf an array of 17 chars to hold the hash
 */
static void
mdb_hash_hex(MDB_val *val, char *hexbuf)
{
	int i;
	mdb_hash_t h = mdb_hash_val(val, MDB_HASH_INIT);
	for (i=0; i<8; i++) {
		hexbuf += sprintf(hexbuf, "%02x", (unsigned int)h & 0xff);
		h >>= 8;
	}
}
#endif

/** Open and/or initialize the lock region for the environment.
 * @param[in] env The MDB environment.
 * @param[in] lpath The pathname of the file used for the lock region.
 * @param[in] mode The Unix permissions for the file, if we create it.
 * @param[out] excl Resulting file lock type: -1 none, 0 shared, 1 exclusive
 * @param[in,out] excl In -1, out lock type: -1 none, 0 shared, 1 exclusive
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_env_setup_locks(MDB_env *env, char *lpath, int mode, int *excl)
{
#ifdef _WIN32
#	define MDB_ERRCODE_ROFS	ERROR_WRITE_PROTECT
#else
#	define MDB_ERRCODE_ROFS	EROFS
#ifdef O_CLOEXEC	/* Linux: Open file and set FD_CLOEXEC atomically */
#	define MDB_CLOEXEC		O_CLOEXEC
#else
	int fdflags;
#	define MDB_CLOEXEC		0
#endif
#endif
	int rc;
	off_t size, rsize;

#ifdef _WIN32
	env->me_lfd = CreateFile(lpath, GENERIC_READ|GENERIC_WRITE,
		FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
#else
	env->me_lfd = open(lpath, O_RDWR|O_CREAT|MDB_CLOEXEC, mode);
#endif
	if (env->me_lfd == INVALID_HANDLE_VALUE) {
		rc = ErrCode();
		if (rc == MDB_ERRCODE_ROFS && (env->me_flags & MDB_RDONLY)) {
			return MDB_SUCCESS;
		}
		goto fail_errno;
	}
#if ! ((MDB_CLOEXEC) || defined(_WIN32))
	/* Lose record locks when exec*() */
	if ((fdflags = fcntl(env->me_lfd, F_GETFD) | FD_CLOEXEC) >= 0)
			fcntl(env->me_lfd, F_SETFD, fdflags);
#endif

	if (!(env->me_flags & MDB_NOTLS)) {
		rc = pthread_key_create(&env->me_txkey, mdb_env_reader_dest);
		if (rc)
			goto fail;
		env->me_flags |= MDB_ENV_TXKEY;
#ifdef _WIN32
		/* Windows TLS callbacks need help finding their TLS info. */
		if (mdb_tls_nkeys >= MAX_TLS_KEYS) {
			rc = MDB_TLS_FULL;
			goto fail;
		}
		mdb_tls_keys[mdb_tls_nkeys++] = env->me_txkey;
#endif
	}

	/* Try to get exclusive lock. If we succeed, then
	 * nobody is using the lock region and we should initialize it.
	 */
	if ((rc = mdb_env_excl_lock(env, excl))) goto fail;

#ifdef _WIN32
	size = GetFileSize(env->me_lfd, NULL);
#else
	size = lseek(env->me_lfd, 0, SEEK_END);
	if (size == -1) goto fail_errno;
#endif
	rsize = (env->me_maxreaders-1) * sizeof(MDB_reader) + sizeof(MDB_txninfo);
	if (size < rsize && *excl > 0) {
#ifdef _WIN32
		if (SetFilePointer(env->me_lfd, rsize, NULL, FILE_BEGIN) != rsize
			|| !SetEndOfFile(env->me_lfd))
			goto fail_errno;
#else
		if (ftruncate(env->me_lfd, rsize) != 0) goto fail_errno;
#endif
	} else {
		rsize = size;
		size = rsize - sizeof(MDB_txninfo);
		env->me_maxreaders = size/sizeof(MDB_reader) + 1;
	}
	{
#ifdef _WIN32
		HANDLE mh;
		mh = CreateFileMapping(env->me_lfd, NULL, PAGE_READWRITE,
			0, 0, NULL);
		if (!mh) goto fail_errno;
		env->me_txns = MapViewOfFileEx(mh, FILE_MAP_WRITE, 0, 0, rsize, NULL);
		CloseHandle(mh);
		if (!env->me_txns) goto fail_errno;
#else
		void *m = mmap(NULL, rsize, PROT_READ|PROT_WRITE, MAP_SHARED,
			env->me_lfd, 0);
		if (m == MAP_FAILED) goto fail_errno;
		env->me_txns = m;
#endif
	}
	if (*excl > 0) {
#ifdef _WIN32
		BY_HANDLE_FILE_INFORMATION stbuf;
		struct {
			DWORD volume;
			DWORD nhigh;
			DWORD nlow;
		} idbuf;
		MDB_val val;
		char hexbuf[17];

		if (!mdb_sec_inited) {
			InitializeSecurityDescriptor(&mdb_null_sd,
				SECURITY_DESCRIPTOR_REVISION);
			SetSecurityDescriptorDacl(&mdb_null_sd, TRUE, 0, FALSE);
			mdb_all_sa.nLength = sizeof(SECURITY_ATTRIBUTES);
			mdb_all_sa.bInheritHandle = FALSE;
			mdb_all_sa.lpSecurityDescriptor = &mdb_null_sd;
			mdb_sec_inited = 1;
		}
		if (!GetFileInformationByHandle(env->me_lfd, &stbuf)) goto fail_errno;
		idbuf.volume = stbuf.dwVolumeSerialNumber;
		idbuf.nhigh  = stbuf.nFileIndexHigh;
		idbuf.nlow   = stbuf.nFileIndexLow;
		val.mv_data = &idbuf;
		val.mv_size = sizeof(idbuf);
		mdb_hash_hex(&val, hexbuf);
		sprintf(env->me_txns->mti_rmname, "Global\\MDBr%s", hexbuf);
		sprintf(env->me_txns->mti_wmname, "Global\\MDBw%s", hexbuf);
		env->me_rmutex = CreateMutex(&mdb_all_sa, FALSE, env->me_txns->mti_rmname);
		if (!env->me_rmutex) goto fail_errno;
		env->me_wmutex = CreateMutex(&mdb_all_sa, FALSE, env->me_txns->mti_wmname);
		if (!env->me_wmutex) goto fail_errno;
#elif defined(MDB_USE_POSIX_SEM)
		struct stat stbuf;
		struct {
			dev_t dev;
			ino_t ino;
		} idbuf;
		MDB_val val;
		char hexbuf[17];

		if (fstat(env->me_lfd, &stbuf)) goto fail_errno;
		idbuf.dev = stbuf.st_dev;
		idbuf.ino = stbuf.st_ino;
		val.mv_data = &idbuf;
		val.mv_size = sizeof(idbuf);
		mdb_hash_hex(&val, hexbuf);
		sprintf(env->me_txns->mti_rmname, "/MDBr%s", hexbuf);
		sprintf(env->me_txns->mti_wmname, "/MDBw%s", hexbuf);
		/* Clean up after a previous run, if needed:  Try to
		 * remove both semaphores before doing anything else.
		 */
		sem_unlink(env->me_txns->mti_rmname);
		sem_unlink(env->me_txns->mti_wmname);
		env->me_rmutex = sem_open(env->me_txns->mti_rmname,
			O_CREAT|O_EXCL, mode, 1);
		if (env->me_rmutex == SEM_FAILED) goto fail_errno;
		env->me_wmutex = sem_open(env->me_txns->mti_wmname,
			O_CREAT|O_EXCL, mode, 1);
		if (env->me_wmutex == SEM_FAILED) goto fail_errno;
#else	/* MDB_USE_POSIX_SEM */
		pthread_mutexattr_t mattr;

		if ((rc = pthread_mutexattr_init(&mattr))
			|| (rc = pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED))
			|| (rc = pthread_mutex_init(&env->me_txns->mti_mutex, &mattr))
			|| (rc = pthread_mutex_init(&env->me_txns->mti_wmutex, &mattr)))
			goto fail;
		pthread_mutexattr_destroy(&mattr);
#endif	/* _WIN32 || MDB_USE_POSIX_SEM */

		env->me_txns->mti_version = MDB_LOCK_VERSION;
		env->me_txns->mti_magic = MDB_MAGIC;
		env->me_txns->mti_txnid = 0;
		env->me_txns->mti_numreaders = 0;

	} else {
		if (env->me_txns->mti_magic != MDB_MAGIC) {
			DPUTS("lock region has invalid magic");
			rc = MDB_INVALID;
			goto fail;
		}
		if (env->me_txns->mti_version != MDB_LOCK_VERSION) {
			DPRINTF("lock region is version %u, expected version %u",
				env->me_txns->mti_version, MDB_LOCK_VERSION);
			rc = MDB_VERSION_MISMATCH;
			goto fail;
		}
		rc = ErrCode();
		if (rc && rc != EACCES && rc != EAGAIN) {
			goto fail;
		}
#ifdef _WIN32
		env->me_rmutex = OpenMutex(SYNCHRONIZE, FALSE, env->me_txns->mti_rmname);
		if (!env->me_rmutex) goto fail_errno;
		env->me_wmutex = OpenMutex(SYNCHRONIZE, FALSE, env->me_txns->mti_wmname);
		if (!env->me_wmutex) goto fail_errno;
#elif defined(MDB_USE_POSIX_SEM)
		env->me_rmutex = sem_open(env->me_txns->mti_rmname, 0);
		if (env->me_rmutex == SEM_FAILED) goto fail_errno;
		env->me_wmutex = sem_open(env->me_txns->mti_wmname, 0);
		if (env->me_wmutex == SEM_FAILED) goto fail_errno;
#endif
	}
	return MDB_SUCCESS;

fail_errno:
	rc = ErrCode();
fail:
	return rc;
}

	/** The name of the lock file in the DB environment */
#define LOCKNAME	"/lock.mdb"
	/** The name of the data file in the DB environment */
#define DATANAME	"/data.mdb"
	/** The suffix of the lock file when no subdir is used */
#define LOCKSUFF	"-lock"
	/** Only a subset of the @ref mdb_env flags can be changed
	 *	at runtime. Changing other flags requires closing the
	 *	environment and re-opening it with the new flags.
	 */
#define	CHANGEABLE	(MDB_NOSYNC|MDB_NOMETASYNC|MDB_MAPASYNC)
#define	CHANGELESS	(MDB_FIXEDMAP|MDB_NOSUBDIR|MDB_RDONLY|MDB_WRITEMAP|MDB_NOTLS)

int
mdb_env_open(MDB_env *env, const char *path, unsigned int flags, mdb_mode_t mode)
{
	int		oflags, rc, len, excl = -1;
	char *lpath, *dpath;

	if (env->me_fd!=INVALID_HANDLE_VALUE || (flags & ~(CHANGEABLE|CHANGELESS)))
		return EINVAL;

	len = strlen(path);
	if (flags & MDB_NOSUBDIR) {
		rc = len + sizeof(LOCKSUFF) + len + 1;
	} else {
		rc = len + sizeof(LOCKNAME) + len + sizeof(DATANAME);
	}
	lpath = malloc(rc);
	if (!lpath)
		return ENOMEM;
	if (flags & MDB_NOSUBDIR) {
		dpath = lpath + len + sizeof(LOCKSUFF);
		sprintf(lpath, "%s" LOCKSUFF, path);
		strcpy(dpath, path);
	} else {
		dpath = lpath + len + sizeof(LOCKNAME);
		sprintf(lpath, "%s" LOCKNAME, path);
		sprintf(dpath, "%s" DATANAME, path);
	}

	rc = MDB_SUCCESS;
	flags |= env->me_flags;
	if (flags & MDB_RDONLY) {
		/* silently ignore WRITEMAP when we're only getting read access */
		flags &= ~MDB_WRITEMAP;
	} else {
		if (!((env->me_free_pgs = mdb_midl_alloc(MDB_IDL_UM_MAX)) &&
			  (env->me_dirty_list = calloc(MDB_IDL_UM_SIZE, sizeof(MDB_ID2)))))
			rc = ENOMEM;
	}
	env->me_flags = flags |= MDB_ENV_ACTIVE;
	if (rc)
		goto leave;

	env->me_path = strdup(path);
	env->me_dbxs = calloc(env->me_maxdbs, sizeof(MDB_dbx));
	env->me_dbflags = calloc(env->me_maxdbs, sizeof(uint16_t));
	if (!(env->me_dbxs && env->me_path && env->me_dbflags)) {
		rc = ENOMEM;
		goto leave;
	}

	rc = mdb_env_setup_locks(env, lpath, mode, &excl);
	if (rc)
		goto leave;

#ifdef _WIN32
	if (F_ISSET(flags, MDB_RDONLY)) {
		oflags = GENERIC_READ;
		len = OPEN_EXISTING;
	} else {
		oflags = GENERIC_READ|GENERIC_WRITE;
		len = OPEN_ALWAYS;
	}
	mode = FILE_ATTRIBUTE_NORMAL;
	env->me_fd = CreateFile(dpath, oflags, FILE_SHARE_READ|FILE_SHARE_WRITE,
		NULL, len, mode, NULL);
#else
	if (F_ISSET(flags, MDB_RDONLY))
		oflags = O_RDONLY;
	else
		oflags = O_RDWR | O_CREAT;

	env->me_fd = open(dpath, oflags, mode);
#endif
	if (env->me_fd == INVALID_HANDLE_VALUE) {
		rc = ErrCode();
		goto leave;
	}

	if ((rc = mdb_env_open2(env)) == MDB_SUCCESS) {
		if (flags & (MDB_RDONLY|MDB_WRITEMAP)) {
			env->me_mfd = env->me_fd;
		} else {
			/* Synchronous fd for meta writes. Needed even with
			 * MDB_NOSYNC/MDB_NOMETASYNC, in case these get reset.
			 */
#ifdef _WIN32
			env->me_mfd = CreateFile(dpath, oflags,
				FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, len,
				mode | FILE_FLAG_WRITE_THROUGH, NULL);
#else
			env->me_mfd = open(dpath, oflags | MDB_DSYNC, mode);
#endif
			if (env->me_mfd == INVALID_HANDLE_VALUE) {
				rc = ErrCode();
				goto leave;
			}
		}
		DPRINTF("opened dbenv %p", (void *) env);
		if (excl > 0) {
			rc = mdb_env_share_locks(env, &excl);
		}
	}

leave:
	if (rc) {
		mdb_env_close0(env, excl);
	}
	free(lpath);
	return rc;
}

/** Destroy resources from mdb_env_open(), clear our readers & DBIs */
static void
mdb_env_close0(MDB_env *env, int excl)
{
	int i;

	if (!(env->me_flags & MDB_ENV_ACTIVE))
		return;

	/* Doing this here since me_dbxs may not exist during mdb_env_close */
	for (i = env->me_maxdbs; --i > MAIN_DBI; )
		free(env->me_dbxs[i].md_name.mv_data);

	free(env->me_dbflags);
	free(env->me_dbxs);
	free(env->me_path);
	free(env->me_dirty_list);
	mdb_midl_free(env->me_free_pgs);

	if (env->me_flags & MDB_ENV_TXKEY) {
		pthread_key_delete(env->me_txkey);
#ifdef _WIN32
		/* Delete our key from the global list */
		for (i=0; i<mdb_tls_nkeys; i++)
			if (mdb_tls_keys[i] == env->me_txkey) {
				mdb_tls_keys[i] = mdb_tls_keys[mdb_tls_nkeys-1];
				mdb_tls_nkeys--;
				break;
			}
#endif
	}

	if (env->me_map) {
		munmap(env->me_map, env->me_mapsize);
	}
	if (env->me_mfd != env->me_fd && env->me_mfd != INVALID_HANDLE_VALUE)
		(void) close(env->me_mfd);
	if (env->me_fd != INVALID_HANDLE_VALUE)
		(void) close(env->me_fd);
	if (env->me_txns) {
		pid_t pid = env->me_pid;
		/* Clearing readers is done in this function because
		 * me_txkey with its destructor must be disabled first.
		 */
		for (i = env->me_numreaders; --i >= 0; )
			if (env->me_txns->mti_readers[i].mr_pid == pid)
				env->me_txns->mti_readers[i].mr_pid = 0;
#ifdef _WIN32
		if (env->me_rmutex) {
			CloseHandle(env->me_rmutex);
			if (env->me_wmutex) CloseHandle(env->me_wmutex);
		}
		/* Windows automatically destroys the mutexes when
		 * the last handle closes.
		 */
#elif defined(MDB_USE_POSIX_SEM)
		if (env->me_rmutex != SEM_FAILED) {
			sem_close(env->me_rmutex);
			if (env->me_wmutex != SEM_FAILED)
				sem_close(env->me_wmutex);
			/* If we have the filelock:  If we are the
			 * only remaining user, clean up semaphores.
			 */
			if (excl == 0)
				mdb_env_excl_lock(env, &excl);
			if (excl > 0) {
				sem_unlink(env->me_txns->mti_rmname);
				sem_unlink(env->me_txns->mti_wmname);
			}
		}
#endif
		munmap((void *)env->me_txns, (env->me_maxreaders-1)*sizeof(MDB_reader)+sizeof(MDB_txninfo));
	}
	if (env->me_lfd != INVALID_HANDLE_VALUE) {
#ifdef _WIN32
		if (excl >= 0) {
			/* Unlock the lockfile.  Windows would have unlocked it
			 * after closing anyway, but not necessarily at once.
			 */
			UnlockFile(env->me_lfd, 0, 0, 1, 0);
		}
#endif
		(void) close(env->me_lfd);
	}

	env->me_flags &= ~(MDB_ENV_ACTIVE|MDB_ENV_TXKEY);
}

int
mdb_env_copyfd(MDB_env *env, HANDLE fd)
{
	MDB_txn *txn = NULL;
	int rc;
	size_t wsize;
	char *ptr;

	/* Do the lock/unlock of the reader mutex before starting the
	 * write txn.  Otherwise other read txns could block writers.
	 */
	rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
	if (rc)
		return rc;

	if (env->me_txns) {
		/* We must start the actual read txn after blocking writers */
		mdb_txn_reset0(txn, "reset-stage1");

		/* Temporarily block writers until we snapshot the meta pages */
		LOCK_MUTEX_W(env);

		rc = mdb_txn_renew0(txn);
		if (rc) {
			UNLOCK_MUTEX_W(env);
			goto leave;
		}
	}

	wsize = env->me_psize * 2;
#ifdef _WIN32
	{
		DWORD len;
		rc = WriteFile(fd, env->me_map, wsize, &len, NULL);
		rc = rc ? (len == wsize ? MDB_SUCCESS : EIO) : ErrCode();
	}
#else
	rc = write(fd, env->me_map, wsize);
	rc = rc == (int)wsize ? MDB_SUCCESS : rc < 0 ? ErrCode() : EIO;
#endif
	if (env->me_txns)
		UNLOCK_MUTEX_W(env);

	if (rc)
		goto leave;

	ptr = env->me_map + wsize;
	wsize = txn->mt_next_pgno * env->me_psize - wsize;
#ifdef _WIN32
	while (wsize > 0) {
		DWORD len, w2;
		if (wsize > MAX_WRITE)
			w2 = MAX_WRITE;
		else
			w2 = wsize;
		rc = WriteFile(fd, ptr, w2, &len, NULL);
		rc = rc ? (len == w2 ? MDB_SUCCESS : EIO) : ErrCode();
		if (rc) break;
		wsize -= w2;
		ptr += w2;
	}
#else
	while (wsize > 0) {
		size_t w2;
		ssize_t wres;
		if (wsize > MAX_WRITE)
			w2 = MAX_WRITE;
		else
			w2 = wsize;
		wres = write(fd, ptr, w2);
		rc = wres == (ssize_t)w2 ? MDB_SUCCESS : wres < 0 ? ErrCode() : EIO;
		if (rc) break;
		wsize -= wres;
		ptr += wres;
	}
#endif

leave:
	mdb_txn_abort(txn);
	return rc;
}

int
mdb_env_copy(MDB_env *env, const char *path)
{
	int rc, len;
	char *lpath;
	HANDLE newfd = INVALID_HANDLE_VALUE;

	if (env->me_flags & MDB_NOSUBDIR) {
		lpath = (char *)path;
	} else {
		len = strlen(path);
		len += sizeof(DATANAME);
		lpath = malloc(len);
		if (!lpath)
			return ENOMEM;
		sprintf(lpath, "%s" DATANAME, path);
	}

	/* The destination path must exist, but the destination file must not.
	 * We don't want the OS to cache the writes, since the source data is
	 * already in the OS cache.
	 */
#ifdef _WIN32
	newfd = CreateFile(lpath, GENERIC_WRITE, 0, NULL, CREATE_NEW,
				FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH, NULL);
#else
	newfd = open(lpath, O_WRONLY|O_CREAT|O_EXCL
#ifdef O_DIRECT
		|O_DIRECT
#endif
		, 0666);
#endif
	if (newfd == INVALID_HANDLE_VALUE) {
		rc = ErrCode();
		goto leave;
	}

#ifdef F_NOCACHE	/* __APPLE__ */
	rc = fcntl(newfd, F_NOCACHE, 1);
	if (rc) {
		rc = ErrCode();
		goto leave;
	}
#endif

	rc = mdb_env_copyfd(env, newfd);

leave:
	if (!(env->me_flags & MDB_NOSUBDIR))
		free(lpath);
	if (newfd != INVALID_HANDLE_VALUE)
		if (close(newfd) < 0 && rc == MDB_SUCCESS)
			rc = ErrCode();

	return rc;
}

void
mdb_env_close(MDB_env *env)
{
	MDB_page *dp;

	if (env == NULL)
		return;

	VGMEMP_DESTROY(env);
	while ((dp = env->me_dpages) != NULL) {
		VGMEMP_DEFINED(&dp->mp_next, sizeof(dp->mp_next));
		env->me_dpages = dp->mp_next;
		free(dp);
	}

	mdb_env_close0(env, 0);
	free(env);
}

/** Compare two items pointing at aligned size_t's */
static int
mdb_cmp_long(const MDB_val *a, const MDB_val *b)
{
	return (*(size_t *)a->mv_data < *(size_t *)b->mv_data) ? -1 :
		*(size_t *)a->mv_data > *(size_t *)b->mv_data;
}

/** Compare two items pointing at aligned int's */
static int
mdb_cmp_int(const MDB_val *a, const MDB_val *b)
{
	return (*(unsigned int *)a->mv_data < *(unsigned int *)b->mv_data) ? -1 :
		*(unsigned int *)a->mv_data > *(unsigned int *)b->mv_data;
}

/** Compare two items pointing at ints of unknown alignment.
 *	Nodes and keys are guaranteed to be 2-byte aligned.
 */
static int
mdb_cmp_cint(const MDB_val *a, const MDB_val *b)
{
#if BYTE_ORDER == LITTLE_ENDIAN
	unsigned short *u, *c;
	int x;

	u = (unsigned short *) ((char *) a->mv_data + a->mv_size);
	c = (unsigned short *) ((char *) b->mv_data + a->mv_size);
	do {
		x = *--u - *--c;
	} while(!x && u > (unsigned short *)a->mv_data);
	return x;
#else
	return memcmp(a->mv_data, b->mv_data, a->mv_size);
#endif
}

/** Compare two items lexically */
static int
mdb_cmp_memn(const MDB_val *a, const MDB_val *b)
{
	int diff;
	ssize_t len_diff;
	unsigned int len;

	len = a->mv_size;
	len_diff = (ssize_t) a->mv_size - (ssize_t) b->mv_size;
	if (len_diff > 0) {
		len = b->mv_size;
		len_diff = 1;
	}

	diff = memcmp(a->mv_data, b->mv_data, len);
	return diff ? diff : len_diff<0 ? -1 : len_diff;
}

/** Compare two items in reverse byte order */
static int
mdb_cmp_memnr(const MDB_val *a, const MDB_val *b)
{
	const unsigned char	*p1, *p2, *p1_lim;
	ssize_t len_diff;
	int diff;

	p1_lim = (const unsigned char *)a->mv_data;
	p1 = (const unsigned char *)a->mv_data + a->mv_size;
	p2 = (const unsigned char *)b->mv_data + b->mv_size;

	len_diff = (ssize_t) a->mv_size - (ssize_t) b->mv_size;
	if (len_diff > 0) {
		p1_lim += len_diff;
		len_diff = 1;
	}

	while (p1 > p1_lim) {
		diff = *--p1 - *--p2;
		if (diff)
			return diff;
	}
	return len_diff<0 ? -1 : len_diff;
}

/** Search for key within a page, using binary search.
 * Returns the smallest entry larger or equal to the key.
 * If exactp is non-null, stores whether the found entry was an exact match
 * in *exactp (1 or 0).
 * Updates the cursor index with the index of the found entry.
 * If no entry larger or equal to the key is found, returns NULL.
 */
static MDB_node *
mdb_node_search(MDB_cursor *mc, MDB_val *key, int *exactp)
{
	unsigned int	 i = 0, nkeys;
	int		 low, high;
	int		 rc = 0;
	MDB_page *mp = mc->mc_pg[mc->mc_top];
	MDB_node	*node = NULL;
	MDB_val	 nodekey;
	MDB_cmp_func *cmp;
	DKBUF;

	nkeys = NUMKEYS(mp);

#if MDB_DEBUG
	{
	pgno_t pgno;
	COPY_PGNO(pgno, mp->mp_pgno);
	DPRINTF("searching %u keys in %s %spage %zu",
	    nkeys, IS_LEAF(mp) ? "leaf" : "branch", IS_SUBP(mp) ? "sub-" : "",
	    pgno);
	}
#endif

	assert(nkeys > 0);

	low = IS_LEAF(mp) ? 0 : 1;
	high = nkeys - 1;
	cmp = mc->mc_dbx->md_cmp;

	/* Branch pages have no data, so if using integer keys,
	 * alignment is guaranteed. Use faster mdb_cmp_int.
	 */
	if (cmp == mdb_cmp_cint && IS_BRANCH(mp)) {
		if (NODEPTR(mp, 1)->mn_ksize == sizeof(size_t))
			cmp = mdb_cmp_long;
		else
			cmp = mdb_cmp_int;
	}

	if (IS_LEAF2(mp)) {
		nodekey.mv_size = mc->mc_db->md_pad;
		node = NODEPTR(mp, 0);	/* fake */
		while (low <= high) {
			i = (low + high) >> 1;
			nodekey.mv_data = LEAF2KEY(mp, i, nodekey.mv_size);
			rc = cmp(key, &nodekey);
			DPRINTF("found leaf index %u [%s], rc = %i",
			    i, DKEY(&nodekey), rc);
			if (rc == 0)
				break;
			if (rc > 0)
				low = i + 1;
			else
				high = i - 1;
		}
	} else {
		while (low <= high) {
			i = (low + high) >> 1;

			node = NODEPTR(mp, i);
			nodekey.mv_size = NODEKSZ(node);
			nodekey.mv_data = NODEKEY(node);

			rc = cmp(key, &nodekey);
#if MDB_DEBUG
			if (IS_LEAF(mp))
				DPRINTF("found leaf index %u [%s], rc = %i",
				    i, DKEY(&nodekey), rc);
			else
				DPRINTF("found branch index %u [%s -> %zu], rc = %i",
				    i, DKEY(&nodekey), NODEPGNO(node), rc);
#endif
			if (rc == 0)
				break;
			if (rc > 0)
				low = i + 1;
			else
				high = i - 1;
		}
	}

	if (rc > 0) {	/* Found entry is less than the key. */
		i++;	/* Skip to get the smallest entry larger than key. */
		if (!IS_LEAF2(mp))
			node = NODEPTR(mp, i);
	}
	if (exactp)
		*exactp = (rc == 0);
	/* store the key index */
	mc->mc_ki[mc->mc_top] = i;
	if (i >= nkeys)
		/* There is no entry larger or equal to the key. */
		return NULL;

	/* nodeptr is fake for LEAF2 */
	return node;
}

#if 0
static void
mdb_cursor_adjust(MDB_cursor *mc, func)
{
	MDB_cursor *m2;

	for (m2 = mc->mc_txn->mt_cursors[mc->mc_dbi]; m2; m2=m2->mc_next) {
		if (m2->mc_pg[m2->mc_top] == mc->mc_pg[mc->mc_top]) {
			func(mc, m2);
		}
	}
}
#endif

/** Pop a page off the top of the cursor's stack. */
static void
mdb_cursor_pop(MDB_cursor *mc)
{
	if (mc->mc_snum) {
#ifndef MDB_DEBUG_SKIP
		MDB_page	*top = mc->mc_pg[mc->mc_top];
#endif
		mc->mc_snum--;
		if (mc->mc_snum)
			mc->mc_top--;

		DPRINTF("popped page %zu off db %u cursor %p", top->mp_pgno,
			mc->mc_dbi, (void *) mc);
	}
}

/** Push a page onto the top of the cursor's stack. */
static int
mdb_cursor_push(MDB_cursor *mc, MDB_page *mp)
{
	DPRINTF("pushing page %zu on db %u cursor %p", mp->mp_pgno,
		mc->mc_dbi, (void *) mc);

	if (mc->mc_snum >= CURSOR_STACK) {
		assert(mc->mc_snum < CURSOR_STACK);
		return MDB_CURSOR_FULL;
	}

	mc->mc_top = mc->mc_snum++;
	mc->mc_pg[mc->mc_top] = mp;
	mc->mc_ki[mc->mc_top] = 0;

	return MDB_SUCCESS;
}

/** Find the address of the page corresponding to a given page number.
 * @param[in] txn the transaction for this access.
 * @param[in] pgno the page number for the page to retrieve.
 * @param[out] ret address of a pointer where the page's address will be stored.
 * @param[out] lvl dirty_list inheritance level of found page. 1=current txn, 0=mapped page.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_page_get(MDB_txn *txn, pgno_t pgno, MDB_page **ret, int *lvl)
{
	MDB_page *p = NULL;
	int level;

	if (!((txn->mt_flags & MDB_TXN_RDONLY) |
		  (txn->mt_env->me_flags & MDB_WRITEMAP)))
	{
		MDB_txn *tx2 = txn;
		level = 1;
		do {
			MDB_ID2L dl = tx2->mt_u.dirty_list;
			unsigned x;
			/* Spilled pages were dirtied in this txn and flushed
			 * because the dirty list got full. Bring this page
			 * back in from the map (but don't unspill it here,
			 * leave that unless page_touch happens again).
			 */
			if (tx2->mt_spill_pgs) {
				x = mdb_midl_search(tx2->mt_spill_pgs, pgno);
				if (x <= tx2->mt_spill_pgs[0] && tx2->mt_spill_pgs[x] == pgno) {
					p = (MDB_page *)(txn->mt_env->me_map + txn->mt_env->me_psize * pgno);
					goto done;
				}
			}
			if (dl[0].mid) {
				unsigned x = mdb_mid2l_search(dl, pgno);
				if (x <= dl[0].mid && dl[x].mid == pgno) {
					p = dl[x].mptr;
					goto done;
				}
			}
			level++;
		} while ((tx2 = tx2->mt_parent) != NULL);
	}

	if (pgno < txn->mt_next_pgno) {
		level = 0;
		p = (MDB_page *)(txn->mt_env->me_map + txn->mt_env->me_psize * pgno);
	} else {
		DPRINTF("page %zu not found", pgno);
		assert(p != NULL);
		return MDB_PAGE_NOTFOUND;
	}

done:
	*ret = p;
	if (lvl)
		*lvl = level;
	return MDB_SUCCESS;
}

/** Search for the page a given key should be in.
 * Pushes parent pages on the cursor stack. This function continues a
 * search on a cursor that has already been initialized. (Usually by
 * #mdb_page_search() but also by #mdb_node_move().)
 * @param[in,out] mc the cursor for this operation.
 * @param[in] key the key to search for. If NULL, search for the lowest
 * page. (This is used by #mdb_cursor_first().)
 * @param[in] modify If true, visited pages are updated with new page numbers.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_page_search_root(MDB_cursor *mc, MDB_val *key, int modify)
{
	MDB_page	*mp = mc->mc_pg[mc->mc_top];
	DKBUF;
	int rc;


	while (IS_BRANCH(mp)) {
		MDB_node	*node;
		indx_t		i;

		DPRINTF("branch page %zu has %u keys", mp->mp_pgno, NUMKEYS(mp));
		assert(NUMKEYS(mp) > 1);
		DPRINTF("found index 0 to page %zu", NODEPGNO(NODEPTR(mp, 0)));

		if (key == NULL)	/* Initialize cursor to first page. */
			i = 0;
		else if (key->mv_size > MDB_MAXKEYSIZE && key->mv_data == NULL) {
							/* cursor to last page */
			i = NUMKEYS(mp)-1;
		} else {
			int	 exact;
			node = mdb_node_search(mc, key, &exact);
			if (node == NULL)
				i = NUMKEYS(mp) - 1;
			else {
				i = mc->mc_ki[mc->mc_top];
				if (!exact) {
					assert(i > 0);
					i--;
				}
			}
		}

		if (key)
			DPRINTF("following index %u for key [%s]",
			    i, DKEY(key));
		assert(i < NUMKEYS(mp));
		node = NODEPTR(mp, i);

		if ((rc = mdb_page_get(mc->mc_txn, NODEPGNO(node), &mp, NULL)) != 0)
			return rc;

		mc->mc_ki[mc->mc_top] = i;
		if ((rc = mdb_cursor_push(mc, mp)))
			return rc;

		if (modify) {
			if ((rc = mdb_page_touch(mc)) != 0)
				return rc;
			mp = mc->mc_pg[mc->mc_top];
		}
	}

	if (!IS_LEAF(mp)) {
		DPRINTF("internal error, index points to a %02X page!?",
		    mp->mp_flags);
		return MDB_CORRUPTED;
	}

	DPRINTF("found leaf page %zu for key [%s]", mp->mp_pgno,
	    key ? DKEY(key) : NULL);
	mc->mc_flags |= C_INITIALIZED;
	mc->mc_flags &= ~C_EOF;

	return MDB_SUCCESS;
}

/** Search for the lowest key under the current branch page.
 * This just bypasses a NUMKEYS check in the current page
 * before calling mdb_page_search_root(), because the callers
 * are all in situations where the current page is known to
 * be underfilled.
 */
static int
mdb_page_search_lowest(MDB_cursor *mc)
{
	MDB_page	*mp = mc->mc_pg[mc->mc_top];
	MDB_node	*node = NODEPTR(mp, 0);
	int rc;

	if ((rc = mdb_page_get(mc->mc_txn, NODEPGNO(node), &mp, NULL)) != 0)
		return rc;

	mc->mc_ki[mc->mc_top] = 0;
	if ((rc = mdb_cursor_push(mc, mp)))
		return rc;
	return mdb_page_search_root(mc, NULL, 0);
}

/** Search for the page a given key should be in.
 * Pushes parent pages on the cursor stack. This function just sets up
 * the search; it finds the root page for \b mc's database and sets this
 * as the root of the cursor's stack. Then #mdb_page_search_root() is
 * called to complete the search.
 * @param[in,out] mc the cursor for this operation.
 * @param[in] key the key to search for. If NULL, search for the lowest
 * page. (This is used by #mdb_cursor_first().)
 * @param[in] flags If MDB_PS_MODIFY set, visited pages are updated with new page numbers.
 *   If MDB_PS_ROOTONLY set, just fetch root node, no further lookups.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_page_search(MDB_cursor *mc, MDB_val *key, int flags)
{
	int		 rc;
	pgno_t		 root;

	/* Make sure the txn is still viable, then find the root from
	 * the txn's db table.
	 */
	if (F_ISSET(mc->mc_txn->mt_flags, MDB_TXN_ERROR)) {
		DPUTS("transaction has failed, must abort");
		return EINVAL;
	} else {
		/* Make sure we're using an up-to-date root */
		if (mc->mc_dbi > MAIN_DBI) {
			if ((*mc->mc_dbflag & DB_STALE) ||
			((flags & MDB_PS_MODIFY) && !(*mc->mc_dbflag & DB_DIRTY))) {
				MDB_cursor mc2;
				unsigned char dbflag = 0;
				mdb_cursor_init(&mc2, mc->mc_txn, MAIN_DBI, NULL);
				rc = mdb_page_search(&mc2, &mc->mc_dbx->md_name, flags & MDB_PS_MODIFY);
				if (rc)
					return rc;
				if (*mc->mc_dbflag & DB_STALE) {
					MDB_val data;
					int exact = 0;
					uint16_t flags;
					MDB_node *leaf = mdb_node_search(&mc2,
						&mc->mc_dbx->md_name, &exact);
					if (!exact)
						return MDB_NOTFOUND;
					rc = mdb_node_read(mc->mc_txn, leaf, &data);
					if (rc)
						return rc;
					memcpy(&flags, ((char *) data.mv_data + offsetof(MDB_db, md_flags)),
						sizeof(uint16_t));
					/* The txn may not know this DBI, or another process may
					 * have dropped and recreated the DB with other flags.
					 */
					if ((mc->mc_db->md_flags & PERSISTENT_FLAGS) != flags)
						return MDB_INCOMPATIBLE;
					memcpy(mc->mc_db, data.mv_data, sizeof(MDB_db));
				}
				if (flags & MDB_PS_MODIFY)
					dbflag = DB_DIRTY;
				*mc->mc_dbflag &= ~DB_STALE;
				*mc->mc_dbflag |= dbflag;
			}
		}
		root = mc->mc_db->md_root;

		if (root == P_INVALID) {		/* Tree is empty. */
			DPUTS("tree is empty");
			return MDB_NOTFOUND;
		}
	}

	assert(root > 1);
	if (!mc->mc_pg[0] || mc->mc_pg[0]->mp_pgno != root)
		if ((rc = mdb_page_get(mc->mc_txn, root, &mc->mc_pg[0], NULL)) != 0)
			return rc;

	mc->mc_snum = 1;
	mc->mc_top = 0;

	DPRINTF("db %u root page %zu has flags 0x%X",
		mc->mc_dbi, root, mc->mc_pg[0]->mp_flags);

	if (flags & MDB_PS_MODIFY) {
		if ((rc = mdb_page_touch(mc)))
			return rc;
	}

	if (flags & MDB_PS_ROOTONLY)
		return MDB_SUCCESS;

	return mdb_page_search_root(mc, key, flags);
}

static int
mdb_ovpage_free(MDB_cursor *mc, MDB_page *mp)
{
	MDB_txn *txn = mc->mc_txn;
	pgno_t pg = mp->mp_pgno;
	unsigned i, ovpages = mp->mp_pages;
	MDB_env *env = txn->mt_env;
	int rc;

	DPRINTF("free ov page %zu (%d)", pg, ovpages);
	/* If the page is dirty or on the spill list we just acquired it,
	 * so we should give it back to our current free list, if any.
	 * Not currently supported in nested txns.
	 * Otherwise put it onto the list of pages we freed in this txn.
	 */
	if (!(mp->mp_flags & P_DIRTY) && txn->mt_spill_pgs) {
		unsigned x = mdb_midl_search(txn->mt_spill_pgs, pg);
		if (x <= txn->mt_spill_pgs[0] && txn->mt_spill_pgs[x] == pg) {
			/* This page is no longer spilled */
			for (; x < txn->mt_spill_pgs[0]; x++)
				txn->mt_spill_pgs[x] = txn->mt_spill_pgs[x+1];
			txn->mt_spill_pgs[0]--;
			goto release;
		}
	}
	if ((mp->mp_flags & P_DIRTY) && !txn->mt_parent && env->me_pghead) {
		unsigned j, x;
		pgno_t *mop;
		MDB_ID2 *dl, ix, iy;
		rc = mdb_midl_need(&env->me_pghead, ovpages);
		if (rc)
			return rc;
		/* Remove from dirty list */
		dl = txn->mt_u.dirty_list;
		x = dl[0].mid--;
		for (ix = dl[x]; ix.mptr != mp; ix = iy) {
			if (x > 1) {
				x--;
				iy = dl[x];
				dl[x] = ix;
			} else {
				assert(x > 1);
				j = ++(dl[0].mid);
				dl[j] = ix;		/* Unsorted. OK when MDB_TXN_ERROR. */
				txn->mt_flags |= MDB_TXN_ERROR;
				return MDB_CORRUPTED;
			}
		}
		if (!(env->me_flags & MDB_WRITEMAP))
			mdb_dpage_free(env, mp);
release:
		/* Insert in me_pghead */
		mop = env->me_pghead;
		j = mop[0] + ovpages;
		for (i = mop[0]; i && mop[i] < pg; i--)
			mop[j--] = mop[i];
		while (j>i)
			mop[j--] = pg++;
		mop[0] += ovpages;
	} else {
		rc = mdb_midl_append_range(&txn->mt_free_pgs, pg, ovpages);
		if (rc)
			return rc;
	}
	mc->mc_db->md_overflow_pages -= ovpages;
	return 0;
}

/** Return the data associated with a given node.
 * @param[in] txn The transaction for this operation.
 * @param[in] leaf The node being read.
 * @param[out] data Updated to point to the node's data.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_node_read(MDB_txn *txn, MDB_node *leaf, MDB_val *data)
{
	MDB_page	*omp;		/* overflow page */
	pgno_t		 pgno;
	int rc;

	if (!F_ISSET(leaf->mn_flags, F_BIGDATA)) {
		data->mv_size = NODEDSZ(leaf);
		data->mv_data = NODEDATA(leaf);
		return MDB_SUCCESS;
	}

	/* Read overflow data.
	 */
	data->mv_size = NODEDSZ(leaf);
	memcpy(&pgno, NODEDATA(leaf), sizeof(pgno));
	if ((rc = mdb_page_get(txn, pgno, &omp, NULL)) != 0) {
		DPRINTF("read overflow page %zu failed", pgno);
		return rc;
	}
	data->mv_data = METADATA(omp);

	return MDB_SUCCESS;
}

int
mdb_get(MDB_txn *txn, MDB_dbi dbi,
    MDB_val *key, MDB_val *data)
{
	MDB_cursor	mc;
	MDB_xcursor	mx;
	int exact = 0;
	DKBUF;

	assert(key);
	assert(data);
	DPRINTF("===> get db %u key [%s]", dbi, DKEY(key));

	if (txn == NULL || !dbi || dbi >= txn->mt_numdbs || !(txn->mt_dbflags[dbi] & DB_VALID))
		return EINVAL;

	if (key->mv_size == 0 || key->mv_size > MDB_MAXKEYSIZE) {
		return EINVAL;
	}

	mdb_cursor_init(&mc, txn, dbi, &mx);
	return mdb_cursor_set(&mc, key, data, MDB_SET, &exact);
}

/** Find a sibling for a page.
 * Replaces the page at the top of the cursor's stack with the
 * specified sibling, if one exists.
 * @param[in] mc The cursor for this operation.
 * @param[in] move_right Non-zero if the right sibling is requested,
 * otherwise the left sibling.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_cursor_sibling(MDB_cursor *mc, int move_right)
{
	int		 rc;
	MDB_node	*indx;
	MDB_page	*mp;

	if (mc->mc_snum < 2) {
		return MDB_NOTFOUND;		/* root has no siblings */
	}

	mdb_cursor_pop(mc);
	DPRINTF("parent page is page %zu, index %u",
		mc->mc_pg[mc->mc_top]->mp_pgno, mc->mc_ki[mc->mc_top]);

	if (move_right ? (mc->mc_ki[mc->mc_top] + 1u >= NUMKEYS(mc->mc_pg[mc->mc_top]))
		       : (mc->mc_ki[mc->mc_top] == 0)) {
		DPRINTF("no more keys left, moving to %s sibling",
		    move_right ? "right" : "left");
		if ((rc = mdb_cursor_sibling(mc, move_right)) != MDB_SUCCESS) {
			/* undo cursor_pop before returning */
			mc->mc_top++;
			mc->mc_snum++;
			return rc;
		}
	} else {
		if (move_right)
			mc->mc_ki[mc->mc_top]++;
		else
			mc->mc_ki[mc->mc_top]--;
		DPRINTF("just moving to %s index key %u",
		    move_right ? "right" : "left", mc->mc_ki[mc->mc_top]);
	}
	assert(IS_BRANCH(mc->mc_pg[mc->mc_top]));

	indx = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
	if ((rc = mdb_page_get(mc->mc_txn, NODEPGNO(indx), &mp, NULL) != 0))
		return rc;

	mdb_cursor_push(mc, mp);
	if (!move_right)
		mc->mc_ki[mc->mc_top] = NUMKEYS(mp)-1;

	return MDB_SUCCESS;
}

/** Move the cursor to the next data item. */
static int
mdb_cursor_next(MDB_cursor *mc, MDB_val *key, MDB_val *data, MDB_cursor_op op)
{
	MDB_page	*mp;
	MDB_node	*leaf;
	int rc;

	if (mc->mc_flags & C_EOF) {
		return MDB_NOTFOUND;
	}

	assert(mc->mc_flags & C_INITIALIZED);

	mp = mc->mc_pg[mc->mc_top];

	if (mc->mc_db->md_flags & MDB_DUPSORT) {
		leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);
		if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
			if (op == MDB_NEXT || op == MDB_NEXT_DUP) {
				rc = mdb_cursor_next(&mc->mc_xcursor->mx_cursor, data, NULL, MDB_NEXT);
				if (op != MDB_NEXT || rc != MDB_NOTFOUND)
					return rc;
			}
		} else {
			mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);
			if (op == MDB_NEXT_DUP)
				return MDB_NOTFOUND;
		}
	}

	DPRINTF("cursor_next: top page is %zu in cursor %p", mp->mp_pgno, (void *) mc);

	if (mc->mc_ki[mc->mc_top] + 1u >= NUMKEYS(mp)) {
		DPUTS("=====> move to next sibling page");
		if ((rc = mdb_cursor_sibling(mc, 1)) != MDB_SUCCESS) {
			mc->mc_flags |= C_EOF;
			return rc;
		}
		mp = mc->mc_pg[mc->mc_top];
		DPRINTF("next page is %zu, key index %u", mp->mp_pgno, mc->mc_ki[mc->mc_top]);
	} else
		mc->mc_ki[mc->mc_top]++;

	DPRINTF("==> cursor points to page %zu with %u keys, key index %u",
	    mp->mp_pgno, NUMKEYS(mp), mc->mc_ki[mc->mc_top]);

	if (IS_LEAF2(mp)) {
		key->mv_size = mc->mc_db->md_pad;
		key->mv_data = LEAF2KEY(mp, mc->mc_ki[mc->mc_top], key->mv_size);
		return MDB_SUCCESS;
	}

	assert(IS_LEAF(mp));
	leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);

	if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
		mdb_xcursor_init1(mc, leaf);
	}
	if (data) {
		if ((rc = mdb_node_read(mc->mc_txn, leaf, data)) != MDB_SUCCESS)
			return rc;

		if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
			rc = mdb_cursor_first(&mc->mc_xcursor->mx_cursor, data, NULL);
			if (rc != MDB_SUCCESS)
				return rc;
		}
	}

	MDB_GET_KEY(leaf, key);
	return MDB_SUCCESS;
}

/** Move the cursor to the previous data item. */
static int
mdb_cursor_prev(MDB_cursor *mc, MDB_val *key, MDB_val *data, MDB_cursor_op op)
{
	MDB_page	*mp;
	MDB_node	*leaf;
	int rc;

	assert(mc->mc_flags & C_INITIALIZED);

	mp = mc->mc_pg[mc->mc_top];

	if (mc->mc_db->md_flags & MDB_DUPSORT) {
		leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);
		if (op == MDB_PREV || op == MDB_PREV_DUP) {
			if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
				rc = mdb_cursor_prev(&mc->mc_xcursor->mx_cursor, data, NULL, MDB_PREV);
				if (op != MDB_PREV || rc != MDB_NOTFOUND)
					return rc;
			} else {
				mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);
				if (op == MDB_PREV_DUP)
					return MDB_NOTFOUND;
			}
		}
	}

	DPRINTF("cursor_prev: top page is %zu in cursor %p", mp->mp_pgno, (void *) mc);

	if (mc->mc_ki[mc->mc_top] == 0)  {
		DPUTS("=====> move to prev sibling page");
		if ((rc = mdb_cursor_sibling(mc, 0)) != MDB_SUCCESS) {
			return rc;
		}
		mp = mc->mc_pg[mc->mc_top];
		mc->mc_ki[mc->mc_top] = NUMKEYS(mp) - 1;
		DPRINTF("prev page is %zu, key index %u", mp->mp_pgno, mc->mc_ki[mc->mc_top]);
	} else
		mc->mc_ki[mc->mc_top]--;

	mc->mc_flags &= ~C_EOF;

	DPRINTF("==> cursor points to page %zu with %u keys, key index %u",
	    mp->mp_pgno, NUMKEYS(mp), mc->mc_ki[mc->mc_top]);

	if (IS_LEAF2(mp)) {
		key->mv_size = mc->mc_db->md_pad;
		key->mv_data = LEAF2KEY(mp, mc->mc_ki[mc->mc_top], key->mv_size);
		return MDB_SUCCESS;
	}

	assert(IS_LEAF(mp));
	leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);

	if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
		mdb_xcursor_init1(mc, leaf);
	}
	if (data) {
		if ((rc = mdb_node_read(mc->mc_txn, leaf, data)) != MDB_SUCCESS)
			return rc;

		if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
			rc = mdb_cursor_last(&mc->mc_xcursor->mx_cursor, data, NULL);
			if (rc != MDB_SUCCESS)
				return rc;
		}
	}

	MDB_GET_KEY(leaf, key);
	return MDB_SUCCESS;
}

/** Set the cursor on a specific data item. */
static int
mdb_cursor_set(MDB_cursor *mc, MDB_val *key, MDB_val *data,
    MDB_cursor_op op, int *exactp)
{
	int		 rc;
	MDB_page	*mp;
	MDB_node	*leaf = NULL;
	DKBUF;

	assert(mc);
	assert(key);
	assert(key->mv_size > 0);

	if (mc->mc_xcursor)
		mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);

	/* See if we're already on the right page */
	if (mc->mc_flags & C_INITIALIZED) {
		MDB_val nodekey;

		mp = mc->mc_pg[mc->mc_top];
		if (!NUMKEYS(mp)) {
			mc->mc_ki[mc->mc_top] = 0;
			return MDB_NOTFOUND;
		}
		if (mp->mp_flags & P_LEAF2) {
			nodekey.mv_size = mc->mc_db->md_pad;
			nodekey.mv_data = LEAF2KEY(mp, 0, nodekey.mv_size);
		} else {
			leaf = NODEPTR(mp, 0);
			MDB_GET_KEY(leaf, &nodekey);
		}
		rc = mc->mc_dbx->md_cmp(key, &nodekey);
		if (rc == 0) {
			/* Probably happens rarely, but first node on the page
			 * was the one we wanted.
			 */
			mc->mc_ki[mc->mc_top] = 0;
			if (exactp)
				*exactp = 1;
			goto set1;
		}
		if (rc > 0) {
			unsigned int i;
			unsigned int nkeys = NUMKEYS(mp);
			if (nkeys > 1) {
				if (mp->mp_flags & P_LEAF2) {
					nodekey.mv_data = LEAF2KEY(mp,
						 nkeys-1, nodekey.mv_size);
				} else {
					leaf = NODEPTR(mp, nkeys-1);
					MDB_GET_KEY(leaf, &nodekey);
				}
				rc = mc->mc_dbx->md_cmp(key, &nodekey);
				if (rc == 0) {
					/* last node was the one we wanted */
					mc->mc_ki[mc->mc_top] = nkeys-1;
					if (exactp)
						*exactp = 1;
					goto set1;
				}
				if (rc < 0) {
					if (mc->mc_ki[mc->mc_top] < NUMKEYS(mp)) {
						/* This is definitely the right page, skip search_page */
						if (mp->mp_flags & P_LEAF2) {
							nodekey.mv_data = LEAF2KEY(mp,
								 mc->mc_ki[mc->mc_top], nodekey.mv_size);
						} else {
							leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);
							MDB_GET_KEY(leaf, &nodekey);
						}
						rc = mc->mc_dbx->md_cmp(key, &nodekey);
						if (rc == 0) {
							/* current node was the one we wanted */
							if (exactp)
								*exactp = 1;
							goto set1;
						}
					}
					rc = 0;
					goto set2;
				}
			}
			/* If any parents have right-sibs, search.
			 * Otherwise, there's nothing further.
			 */
			for (i=0; i<mc->mc_top; i++)
				if (mc->mc_ki[i] <
					NUMKEYS(mc->mc_pg[i])-1)
					break;
			if (i == mc->mc_top) {
				/* There are no other pages */
				mc->mc_ki[mc->mc_top] = nkeys;
				return MDB_NOTFOUND;
			}
		}
		if (!mc->mc_top) {
			/* There are no other pages */
			mc->mc_ki[mc->mc_top] = 0;
			return MDB_NOTFOUND;
		}
	}

	rc = mdb_page_search(mc, key, 0);
	if (rc != MDB_SUCCESS)
		return rc;

	mp = mc->mc_pg[mc->mc_top];
	assert(IS_LEAF(mp));

set2:
	leaf = mdb_node_search(mc, key, exactp);
	if (exactp != NULL && !*exactp) {
		/* MDB_SET specified and not an exact match. */
		return MDB_NOTFOUND;
	}

	if (leaf == NULL) {
		DPUTS("===> inexact leaf not found, goto sibling");
		if ((rc = mdb_cursor_sibling(mc, 1)) != MDB_SUCCESS)
			return rc;		/* no entries matched */
		mp = mc->mc_pg[mc->mc_top];
		assert(IS_LEAF(mp));
		leaf = NODEPTR(mp, 0);
	}

set1:
	mc->mc_flags |= C_INITIALIZED;
	mc->mc_flags &= ~C_EOF;

	if (IS_LEAF2(mp)) {
		key->mv_size = mc->mc_db->md_pad;
		key->mv_data = LEAF2KEY(mp, mc->mc_ki[mc->mc_top], key->mv_size);
		return MDB_SUCCESS;
	}

	if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
		mdb_xcursor_init1(mc, leaf);
	}
	if (data) {
		if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
			if (op == MDB_SET || op == MDB_SET_KEY || op == MDB_SET_RANGE) {
				rc = mdb_cursor_first(&mc->mc_xcursor->mx_cursor, data, NULL);
			} else {
				int ex2, *ex2p;
				if (op == MDB_GET_BOTH) {
					ex2p = &ex2;
					ex2 = 0;
				} else {
					ex2p = NULL;
				}
				rc = mdb_cursor_set(&mc->mc_xcursor->mx_cursor, data, NULL, MDB_SET_RANGE, ex2p);
				if (rc != MDB_SUCCESS)
					return rc;
			}
		} else if (op == MDB_GET_BOTH || op == MDB_GET_BOTH_RANGE) {
			MDB_val d2;
			if ((rc = mdb_node_read(mc->mc_txn, leaf, &d2)) != MDB_SUCCESS)
				return rc;
			rc = mc->mc_dbx->md_dcmp(data, &d2);
			if (rc) {
				if (op == MDB_GET_BOTH || rc > 0)
					return MDB_NOTFOUND;
			}

		} else {
			if (mc->mc_xcursor)
				mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);
			if ((rc = mdb_node_read(mc->mc_txn, leaf, data)) != MDB_SUCCESS)
				return rc;
		}
	}

	/* The key already matches in all other cases */
	if (op == MDB_SET_RANGE || op == MDB_SET_KEY)
		MDB_GET_KEY(leaf, key);
	DPRINTF("==> cursor placed on key [%s]", DKEY(key));

	return rc;
}

/** Move the cursor to the first item in the database. */
static int
mdb_cursor_first(MDB_cursor *mc, MDB_val *key, MDB_val *data)
{
	int		 rc;
	MDB_node	*leaf;

	if (mc->mc_xcursor)
		mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);

	if (!(mc->mc_flags & C_INITIALIZED) || mc->mc_top) {
		rc = mdb_page_search(mc, NULL, 0);
		if (rc != MDB_SUCCESS)
			return rc;
	}
	assert(IS_LEAF(mc->mc_pg[mc->mc_top]));

	leaf = NODEPTR(mc->mc_pg[mc->mc_top], 0);
	mc->mc_flags |= C_INITIALIZED;
	mc->mc_flags &= ~C_EOF;

	mc->mc_ki[mc->mc_top] = 0;

	if (IS_LEAF2(mc->mc_pg[mc->mc_top])) {
		key->mv_size = mc->mc_db->md_pad;
		key->mv_data = LEAF2KEY(mc->mc_pg[mc->mc_top], 0, key->mv_size);
		return MDB_SUCCESS;
	}

	if (data) {
		if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
			mdb_xcursor_init1(mc, leaf);
			rc = mdb_cursor_first(&mc->mc_xcursor->mx_cursor, data, NULL);
			if (rc)
				return rc;
		} else {
			if ((rc = mdb_node_read(mc->mc_txn, leaf, data)) != MDB_SUCCESS)
				return rc;
		}
	}
	MDB_GET_KEY(leaf, key);
	return MDB_SUCCESS;
}

/** Move the cursor to the last item in the database. */
static int
mdb_cursor_last(MDB_cursor *mc, MDB_val *key, MDB_val *data)
{
	int		 rc;
	MDB_node	*leaf;

	if (mc->mc_xcursor)
		mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);

	if (!(mc->mc_flags & C_EOF)) {

		if (!(mc->mc_flags & C_INITIALIZED) || mc->mc_top) {
			MDB_val	lkey;

			lkey.mv_size = MDB_MAXKEYSIZE+1;
			lkey.mv_data = NULL;
			rc = mdb_page_search(mc, &lkey, 0);
			if (rc != MDB_SUCCESS)
				return rc;
		}
		assert(IS_LEAF(mc->mc_pg[mc->mc_top]));

	}
	mc->mc_ki[mc->mc_top] = NUMKEYS(mc->mc_pg[mc->mc_top]) - 1;
	mc->mc_flags |= C_INITIALIZED|C_EOF;
	leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);

	if (IS_LEAF2(mc->mc_pg[mc->mc_top])) {
		key->mv_size = mc->mc_db->md_pad;
		key->mv_data = LEAF2KEY(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top], key->mv_size);
		return MDB_SUCCESS;
	}

	if (data) {
		if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
			mdb_xcursor_init1(mc, leaf);
			rc = mdb_cursor_last(&mc->mc_xcursor->mx_cursor, data, NULL);
			if (rc)
				return rc;
		} else {
			if ((rc = mdb_node_read(mc->mc_txn, leaf, data)) != MDB_SUCCESS)
				return rc;
		}
	}

	MDB_GET_KEY(leaf, key);
	return MDB_SUCCESS;
}

int
mdb_cursor_get(MDB_cursor *mc, MDB_val *key, MDB_val *data,
    MDB_cursor_op op)
{
	int		 rc;
	int		 exact = 0;

	assert(mc);

	switch (op) {
	case MDB_GET_CURRENT:
		if (!(mc->mc_flags & C_INITIALIZED)) {
			rc = EINVAL;
		} else {
			MDB_page *mp = mc->mc_pg[mc->mc_top];
			if (!NUMKEYS(mp)) {
				mc->mc_ki[mc->mc_top] = 0;
				rc = MDB_NOTFOUND;
				break;
			}
			rc = MDB_SUCCESS;
			if (IS_LEAF2(mp)) {
				key->mv_size = mc->mc_db->md_pad;
				key->mv_data = LEAF2KEY(mp, mc->mc_ki[mc->mc_top], key->mv_size);
			} else {
				MDB_node *leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);
				MDB_GET_KEY(leaf, key);
				if (data) {
					if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
						rc = mdb_cursor_get(&mc->mc_xcursor->mx_cursor, data, NULL, MDB_GET_CURRENT);
					} else {
						rc = mdb_node_read(mc->mc_txn, leaf, data);
					}
				}
			}
		}
		break;
	case MDB_GET_BOTH:
	case MDB_GET_BOTH_RANGE:
		if (data == NULL || mc->mc_xcursor == NULL) {
			rc = EINVAL;
			break;
		}
		/* FALLTHRU */
	case MDB_SET:
	case MDB_SET_KEY:
	case MDB_SET_RANGE:
		if (key == NULL || key->mv_size == 0 || key->mv_size > MDB_MAXKEYSIZE) {
			rc = EINVAL;
		} else if (op == MDB_SET_RANGE)
			rc = mdb_cursor_set(mc, key, data, op, NULL);
		else
			rc = mdb_cursor_set(mc, key, data, op, &exact);
		break;
	case MDB_GET_MULTIPLE:
		if (data == NULL ||
			!(mc->mc_db->md_flags & MDB_DUPFIXED) ||
			!(mc->mc_flags & C_INITIALIZED)) {
			rc = EINVAL;
			break;
		}
		rc = MDB_SUCCESS;
		if (!(mc->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED) ||
			(mc->mc_xcursor->mx_cursor.mc_flags & C_EOF))
			break;
		goto fetchm;
	case MDB_NEXT_MULTIPLE:
		if (data == NULL ||
			!(mc->mc_db->md_flags & MDB_DUPFIXED)) {
			rc = EINVAL;
			break;
		}
		if (!(mc->mc_flags & C_INITIALIZED))
			rc = mdb_cursor_first(mc, key, data);
		else
			rc = mdb_cursor_next(mc, key, data, MDB_NEXT_DUP);
		if (rc == MDB_SUCCESS) {
			if (mc->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED) {
				MDB_cursor *mx;
fetchm:
				mx = &mc->mc_xcursor->mx_cursor;
				data->mv_size = NUMKEYS(mx->mc_pg[mx->mc_top]) *
					mx->mc_db->md_pad;
				data->mv_data = METADATA(mx->mc_pg[mx->mc_top]);
				mx->mc_ki[mx->mc_top] = NUMKEYS(mx->mc_pg[mx->mc_top])-1;
			} else {
				rc = MDB_NOTFOUND;
			}
		}
		break;
	case MDB_NEXT:
	case MDB_NEXT_DUP:
	case MDB_NEXT_NODUP:
		if (!(mc->mc_flags & C_INITIALIZED))
			rc = mdb_cursor_first(mc, key, data);
		else
			rc = mdb_cursor_next(mc, key, data, op);
		break;
	case MDB_PREV:
	case MDB_PREV_DUP:
	case MDB_PREV_NODUP:
		if (!(mc->mc_flags & C_INITIALIZED)) {
			rc = mdb_cursor_last(mc, key, data);
			if (rc)
				break;
			mc->mc_flags |= C_INITIALIZED;
			mc->mc_ki[mc->mc_top]++;
		}
		rc = mdb_cursor_prev(mc, key, data, op);
		break;
	case MDB_FIRST:
		rc = mdb_cursor_first(mc, key, data);
		break;
	case MDB_FIRST_DUP:
		if (data == NULL ||
			!(mc->mc_db->md_flags & MDB_DUPSORT) ||
			!(mc->mc_flags & C_INITIALIZED) ||
			!(mc->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED)) {
			rc = EINVAL;
			break;
		}
		rc = mdb_cursor_first(&mc->mc_xcursor->mx_cursor, data, NULL);
		break;
	case MDB_LAST:
		rc = mdb_cursor_last(mc, key, data);
		break;
	case MDB_LAST_DUP:
		if (data == NULL ||
			!(mc->mc_db->md_flags & MDB_DUPSORT) ||
			!(mc->mc_flags & C_INITIALIZED) ||
			!(mc->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED)) {
			rc = EINVAL;
			break;
		}
		rc = mdb_cursor_last(&mc->mc_xcursor->mx_cursor, data, NULL);
		break;
	default:
		DPRINTF("unhandled/unimplemented cursor operation %u", op);
		rc = EINVAL;
		break;
	}

	return rc;
}

/** Touch all the pages in the cursor stack.
 *	Makes sure all the pages are writable, before attempting a write operation.
 * @param[in] mc The cursor to operate on.
 */
static int
mdb_cursor_touch(MDB_cursor *mc)
{
	int rc;

	if (mc->mc_dbi > MAIN_DBI && !(*mc->mc_dbflag & DB_DIRTY)) {
		MDB_cursor mc2;
		MDB_xcursor mcx;
		mdb_cursor_init(&mc2, mc->mc_txn, MAIN_DBI, &mcx);
		rc = mdb_page_search(&mc2, &mc->mc_dbx->md_name, MDB_PS_MODIFY);
		if (rc)
			 return rc;
		*mc->mc_dbflag |= DB_DIRTY;
	}
	for (mc->mc_top = 0; mc->mc_top < mc->mc_snum; mc->mc_top++) {
		rc = mdb_page_touch(mc);
		if (rc)
			return rc;
	}
	mc->mc_top = mc->mc_snum-1;
	return MDB_SUCCESS;
}

/** Do not spill pages to disk if txn is getting full, may fail instead */
#define MDB_NOSPILL	0x8000

int
mdb_cursor_put(MDB_cursor *mc, MDB_val *key, MDB_val *data,
    unsigned int flags)
{
	enum { MDB_NO_ROOT = MDB_LAST_ERRCODE+10 }; /* internal code */
	MDB_node	*leaf = NULL;
	MDB_val	xdata, *rdata, dkey;
	MDB_page	*fp;
	MDB_db dummy;
	int do_sub = 0, insert = 0;
	unsigned int mcount = 0, dcount = 0, nospill;
	size_t nsize;
	int rc, rc2;
	MDB_pagebuf pbuf;
	char dbuf[MDB_MAXKEYSIZE+1];
	unsigned int nflags;
	DKBUF;

	/* Check this first so counter will always be zero on any
	 * early failures.
	 */
	if (flags & MDB_MULTIPLE) {
		dcount = data[1].mv_size;
		data[1].mv_size = 0;
		if (!F_ISSET(mc->mc_db->md_flags, MDB_DUPFIXED))
			return EINVAL;
	}

	nospill = flags & MDB_NOSPILL;
	flags &= ~MDB_NOSPILL;

	if (F_ISSET(mc->mc_txn->mt_flags, MDB_TXN_RDONLY))
		return EACCES;

	if (flags != MDB_CURRENT && (key->mv_size == 0 || key->mv_size > MDB_MAXKEYSIZE))
		return EINVAL;

	if (F_ISSET(mc->mc_db->md_flags, MDB_DUPSORT) && data->mv_size > MDB_MAXKEYSIZE)
		return EINVAL;

#if SIZE_MAX > MAXDATASIZE
	if (data->mv_size > MAXDATASIZE)
		return EINVAL;
#endif

	DPRINTF("==> put db %u key [%s], size %zu, data size %zu",
		mc->mc_dbi, DKEY(key), key ? key->mv_size:0, data->mv_size);

	dkey.mv_size = 0;

	if (flags == MDB_CURRENT) {
		if (!(mc->mc_flags & C_INITIALIZED))
			return EINVAL;
		rc = MDB_SUCCESS;
	} else if (mc->mc_db->md_root == P_INVALID) {
		/* new database, cursor has nothing to point to */
		mc->mc_snum = 0;
		mc->mc_flags &= ~C_INITIALIZED;
		rc = MDB_NO_ROOT;
	} else {
		int exact = 0;
		MDB_val d2;
		if (flags & MDB_APPEND) {
			MDB_val k2;
			rc = mdb_cursor_last(mc, &k2, &d2);
			if (rc == 0) {
				rc = mc->mc_dbx->md_cmp(key, &k2);
				if (rc > 0) {
					rc = MDB_NOTFOUND;
					mc->mc_ki[mc->mc_top]++;
				} else {
					/* new key is <= last key */
					rc = MDB_KEYEXIST;
				}
			}
		} else {
			rc = mdb_cursor_set(mc, key, &d2, MDB_SET, &exact);
		}
		if ((flags & MDB_NOOVERWRITE) && rc == 0) {
			DPRINTF("duplicate key [%s]", DKEY(key));
			*data = d2;
			return MDB_KEYEXIST;
		}
		if (rc && rc != MDB_NOTFOUND)
			return rc;
	}

	/* Cursor is positioned, check for room in the dirty list */
	if (!nospill) {
		if (flags & MDB_MULTIPLE) {
			rdata = &xdata;
			xdata.mv_size = data->mv_size * dcount;
		} else {
			rdata = data;
		}
		if ((rc2 = mdb_page_spill(mc, key, rdata)))
			return rc2;
	}

	if (rc == MDB_NO_ROOT) {
		MDB_page *np;
		/* new database, write a root leaf page */
		DPUTS("allocating new root leaf page");
		if ((rc2 = mdb_page_new(mc, P_LEAF, 1, &np))) {
			return rc2;
		}
		mdb_cursor_push(mc, np);
		mc->mc_db->md_root = np->mp_pgno;
		mc->mc_db->md_depth++;
		*mc->mc_dbflag |= DB_DIRTY;
		if ((mc->mc_db->md_flags & (MDB_DUPSORT|MDB_DUPFIXED))
			== MDB_DUPFIXED)
			np->mp_flags |= P_LEAF2;
		mc->mc_flags |= C_INITIALIZED;
	} else {
		/* make sure all cursor pages are writable */
		rc2 = mdb_cursor_touch(mc);
		if (rc2)
			return rc2;
	}

	/* The key already exists */
	if (rc == MDB_SUCCESS) {
		/* there's only a key anyway, so this is a no-op */
		if (IS_LEAF2(mc->mc_pg[mc->mc_top])) {
			unsigned int ksize = mc->mc_db->md_pad;
			if (key->mv_size != ksize)
				return EINVAL;
			if (flags == MDB_CURRENT) {
				char *ptr = LEAF2KEY(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top], ksize);
				memcpy(ptr, key->mv_data, ksize);
			}
			return MDB_SUCCESS;
		}

		leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);

		/* DB has dups? */
		if (F_ISSET(mc->mc_db->md_flags, MDB_DUPSORT)) {
			/* Was a single item before, must convert now */
more:
			if (!F_ISSET(leaf->mn_flags, F_DUPDATA)) {
				/* Just overwrite the current item */
				if (flags == MDB_CURRENT)
					goto current;

				dkey.mv_size = NODEDSZ(leaf);
				dkey.mv_data = NODEDATA(leaf);
#if UINT_MAX < SIZE_MAX
				if (mc->mc_dbx->md_dcmp == mdb_cmp_int && dkey.mv_size == sizeof(size_t))
#ifdef MISALIGNED_OK
					mc->mc_dbx->md_dcmp = mdb_cmp_long;
#else
					mc->mc_dbx->md_dcmp = mdb_cmp_cint;
#endif
#endif
				/* if data matches, ignore it */
				if (!mc->mc_dbx->md_dcmp(data, &dkey))
					return (flags == MDB_NODUPDATA) ? MDB_KEYEXIST : MDB_SUCCESS;

				/* create a fake page for the dup items */
				memcpy(dbuf, dkey.mv_data, dkey.mv_size);
				dkey.mv_data = dbuf;
				fp = (MDB_page *)&pbuf;
				fp->mp_pgno = mc->mc_pg[mc->mc_top]->mp_pgno;
				fp->mp_flags = P_LEAF|P_DIRTY|P_SUBP;
				fp->mp_lower = PAGEHDRSZ;
				fp->mp_upper = PAGEHDRSZ + dkey.mv_size + data->mv_size;
				if (mc->mc_db->md_flags & MDB_DUPFIXED) {
					fp->mp_flags |= P_LEAF2;
					fp->mp_pad = data->mv_size;
					fp->mp_upper += 2 * data->mv_size;	/* leave space for 2 more */
				} else {
					fp->mp_upper += 2 * sizeof(indx_t) + 2 * NODESIZE +
						(dkey.mv_size & 1) + (data->mv_size & 1);
				}
				mdb_node_del(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top], 0);
				do_sub = 1;
				rdata = &xdata;
				xdata.mv_size = fp->mp_upper;
				xdata.mv_data = fp;
				flags |= F_DUPDATA;
				goto new_sub;
			}
			if (!F_ISSET(leaf->mn_flags, F_SUBDATA)) {
				/* See if we need to convert from fake page to subDB */
				MDB_page *mp;
				unsigned int offset;
				unsigned int i;
				uint16_t fp_flags;

				fp = NODEDATA(leaf);
				if (flags == MDB_CURRENT) {
reuse:
					fp->mp_flags |= P_DIRTY;
					COPY_PGNO(fp->mp_pgno, mc->mc_pg[mc->mc_top]->mp_pgno);
					mc->mc_xcursor->mx_cursor.mc_pg[0] = fp;
					flags |= F_DUPDATA;
					goto put_sub;
				}
				if (mc->mc_db->md_flags & MDB_DUPFIXED) {
					offset = fp->mp_pad;
					if (SIZELEFT(fp) >= offset)
						goto reuse;
					offset *= 4;	/* space for 4 more */
				} else {
					offset = NODESIZE + sizeof(indx_t) + data->mv_size;
				}
				offset += offset & 1;
				fp_flags = fp->mp_flags;
				if (NODESIZE + sizeof(indx_t) + NODEKSZ(leaf) + NODEDSZ(leaf) +
					offset >= mc->mc_txn->mt_env->me_nodemax) {
					/* yes, convert it */
					dummy.md_flags = 0;
					if (mc->mc_db->md_flags & MDB_DUPFIXED) {
						dummy.md_pad = fp->mp_pad;
						dummy.md_flags = MDB_DUPFIXED;
						if (mc->mc_db->md_flags & MDB_INTEGERDUP)
							dummy.md_flags |= MDB_INTEGERKEY;
					}
					dummy.md_depth = 1;
					dummy.md_branch_pages = 0;
					dummy.md_leaf_pages = 1;
					dummy.md_overflow_pages = 0;
					dummy.md_entries = NUMKEYS(fp);
					rdata = &xdata;
					xdata.mv_size = sizeof(MDB_db);
					xdata.mv_data = &dummy;
					if ((rc = mdb_page_alloc(mc, 1, &mp)))
						return rc;
					offset = mc->mc_txn->mt_env->me_psize - NODEDSZ(leaf);
					flags |= F_DUPDATA|F_SUBDATA;
					dummy.md_root = mp->mp_pgno;
					fp_flags &= ~P_SUBP;
				} else {
					/* no, just grow it */
					rdata = &xdata;
					xdata.mv_size = NODEDSZ(leaf) + offset;
					xdata.mv_data = &pbuf;
					mp = (MDB_page *)&pbuf;
					mp->mp_pgno = mc->mc_pg[mc->mc_top]->mp_pgno;
					flags |= F_DUPDATA;
				}
				mp->mp_flags = fp_flags | P_DIRTY;
				mp->mp_pad   = fp->mp_pad;
				mp->mp_lower = fp->mp_lower;
				mp->mp_upper = fp->mp_upper + offset;
				if (IS_LEAF2(fp)) {
					memcpy(METADATA(mp), METADATA(fp), NUMKEYS(fp) * fp->mp_pad);
				} else {
					nsize = NODEDSZ(leaf) - fp->mp_upper;
					memcpy((char *)mp + mp->mp_upper, (char *)fp + fp->mp_upper, nsize);
					for (i=0; i<NUMKEYS(fp); i++)
						mp->mp_ptrs[i] = fp->mp_ptrs[i] + offset;
				}
				mdb_node_del(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top], 0);
				do_sub = 1;
				goto new_sub;
			}
			/* data is on sub-DB, just store it */
			flags |= F_DUPDATA|F_SUBDATA;
			goto put_sub;
		}
current:
		/* overflow page overwrites need special handling */
		if (F_ISSET(leaf->mn_flags, F_BIGDATA)) {
			MDB_page *omp;
			pgno_t pg;
			unsigned psize = mc->mc_txn->mt_env->me_psize;
			int level, ovpages, dpages = OVPAGES(data->mv_size, psize);

			memcpy(&pg, NODEDATA(leaf), sizeof(pg));
			if ((rc2 = mdb_page_get(mc->mc_txn, pg, &omp, &level)) != 0)
				return rc2;
			ovpages = omp->mp_pages;

			/* Is the ov page large enough? */
			if (ovpages >= dpages) {
			  if (!(omp->mp_flags & P_DIRTY) &&
				  (level || (mc->mc_txn->mt_env->me_flags & MDB_WRITEMAP)))
			  {
				rc = mdb_page_unspill(mc->mc_txn, omp, &omp);
				if (rc)
					return rc;
				level = 0;		/* dirty in this txn or clean */
			  }
			  /* Is it dirty? */
			  if (omp->mp_flags & P_DIRTY) {
				/* yes, overwrite it. Note in this case we don't
				 * bother to try shrinking the page if the new data
				 * is smaller than the overflow threshold.
				 */
				if (level > 1) {
					/* It is writable only in a parent txn */
					size_t sz = (size_t) psize * ovpages, off;
					MDB_page *np = mdb_page_malloc(mc->mc_txn, ovpages);
					MDB_ID2 id2;
					if (!np)
						return ENOMEM;
					id2.mid = pg;
					id2.mptr = np;
					mdb_mid2l_insert(mc->mc_txn->mt_u.dirty_list, &id2);
					if (!(flags & MDB_RESERVE)) {
						/* Copy end of page, adjusting alignment so
						 * compiler may copy words instead of bytes.
						 */
						off = (PAGEHDRSZ + data->mv_size) & -sizeof(size_t);
						memcpy((size_t *)((char *)np + off),
							(size_t *)((char *)omp + off), sz - off);
						sz = PAGEHDRSZ;
					}
					memcpy(np, omp, sz); /* Copy beginning of page */
					omp = np;
				}
				SETDSZ(leaf, data->mv_size);
				if (F_ISSET(flags, MDB_RESERVE))
					data->mv_data = METADATA(omp);
				else
					memcpy(METADATA(omp), data->mv_data, data->mv_size);
				goto done;
			  }
			}
			if ((rc2 = mdb_ovpage_free(mc, omp)) != MDB_SUCCESS)
				return rc2;
		} else if (NODEDSZ(leaf) == data->mv_size) {
			/* same size, just replace it. Note that we could
			 * also reuse this node if the new data is smaller,
			 * but instead we opt to shrink the node in that case.
			 */
			if (F_ISSET(flags, MDB_RESERVE))
				data->mv_data = NODEDATA(leaf);
			else if (data->mv_size)
				memcpy(NODEDATA(leaf), data->mv_data, data->mv_size);
			else
				memcpy(NODEKEY(leaf), key->mv_data, key->mv_size);
			goto done;
		}
		mdb_node_del(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top], 0);
		mc->mc_db->md_entries--;
	} else {
		DPRINTF("inserting key at index %i", mc->mc_ki[mc->mc_top]);
		insert = 1;
	}

	rdata = data;

new_sub:
	nflags = flags & NODE_ADD_FLAGS;
	nsize = IS_LEAF2(mc->mc_pg[mc->mc_top]) ? key->mv_size : mdb_leaf_size(mc->mc_txn->mt_env, key, rdata);
	if (SIZELEFT(mc->mc_pg[mc->mc_top]) < nsize) {
		if (( flags & (F_DUPDATA|F_SUBDATA)) == F_DUPDATA )
			nflags &= ~MDB_APPEND;
		if (!insert)
			nflags |= MDB_SPLIT_REPLACE;
		rc = mdb_page_split(mc, key, rdata, P_INVALID, nflags);
	} else {
		/* There is room already in this leaf page. */
		rc = mdb_node_add(mc, mc->mc_ki[mc->mc_top], key, rdata, 0, nflags);
		if (rc == 0 && !do_sub && insert) {
			/* Adjust other cursors pointing to mp */
			MDB_cursor *m2, *m3;
			MDB_dbi dbi = mc->mc_dbi;
			unsigned i = mc->mc_top;
			MDB_page *mp = mc->mc_pg[i];

			if (mc->mc_flags & C_SUB)
				dbi--;

			for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
				if (mc->mc_flags & C_SUB)
					m3 = &m2->mc_xcursor->mx_cursor;
				else
					m3 = m2;
				if (m3 == mc || m3->mc_snum < mc->mc_snum) continue;
				if (m3->mc_pg[i] == mp && m3->mc_ki[i] >= mc->mc_ki[i]) {
					m3->mc_ki[i]++;
				}
			}
		}
	}

	if (rc != MDB_SUCCESS)
		mc->mc_txn->mt_flags |= MDB_TXN_ERROR;
	else {
		/* Now store the actual data in the child DB. Note that we're
		 * storing the user data in the keys field, so there are strict
		 * size limits on dupdata. The actual data fields of the child
		 * DB are all zero size.
		 */
		if (do_sub) {
			int xflags;
put_sub:
			xdata.mv_size = 0;
			xdata.mv_data = "";
			leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
			if (flags & MDB_CURRENT) {
				xflags = MDB_CURRENT|MDB_NOSPILL;
			} else {
				mdb_xcursor_init1(mc, leaf);
				xflags = (flags & MDB_NODUPDATA) ?
					MDB_NOOVERWRITE|MDB_NOSPILL : MDB_NOSPILL;
			}
			/* converted, write the original data first */
			if (dkey.mv_size) {
				rc = mdb_cursor_put(&mc->mc_xcursor->mx_cursor, &dkey, &xdata, xflags);
				if (rc)
					return rc;
				{
					/* Adjust other cursors pointing to mp */
					MDB_cursor *m2;
					unsigned i = mc->mc_top;
					MDB_page *mp = mc->mc_pg[i];

					for (m2 = mc->mc_txn->mt_cursors[mc->mc_dbi]; m2; m2=m2->mc_next) {
						if (m2 == mc || m2->mc_snum < mc->mc_snum) continue;
						if (!(m2->mc_flags & C_INITIALIZED)) continue;
						if (m2->mc_pg[i] == mp && m2->mc_ki[i] == mc->mc_ki[i]) {
							mdb_xcursor_init1(m2, leaf);
						}
					}
				}
				/* we've done our job */
				dkey.mv_size = 0;
			}
			if (flags & MDB_APPENDDUP)
				xflags |= MDB_APPEND;
			rc = mdb_cursor_put(&mc->mc_xcursor->mx_cursor, data, &xdata, xflags);
			if (flags & F_SUBDATA) {
				void *db = NODEDATA(leaf);
				memcpy(db, &mc->mc_xcursor->mx_db, sizeof(MDB_db));
			}
		}
		/* sub-writes might have failed so check rc again.
		 * Don't increment count if we just replaced an existing item.
		 */
		if (!rc && !(flags & MDB_CURRENT))
			mc->mc_db->md_entries++;
		if (flags & MDB_MULTIPLE) {
			if (!rc) {
				mcount++;
				if (mcount < dcount) {
					data[0].mv_data = (char *)data[0].mv_data + data[0].mv_size;
					leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
					goto more;
				}
			}
			/* let caller know how many succeeded, if any */
			data[1].mv_size = mcount;
		}
	}
done:
	/* If we succeeded and the key didn't exist before, make sure
	 * the cursor is marked valid.
	 */
	if (!rc && insert)
		mc->mc_flags |= C_INITIALIZED;
	return rc;
}

int
mdb_cursor_del(MDB_cursor *mc, unsigned int flags)
{
	MDB_node	*leaf;
	int rc;

	if (F_ISSET(mc->mc_txn->mt_flags, MDB_TXN_RDONLY))
		return EACCES;

	if (!(mc->mc_flags & C_INITIALIZED))
		return EINVAL;

	if (!(flags & MDB_NOSPILL) && (rc = mdb_page_spill(mc, NULL, NULL)))
		return rc;
	flags &= ~MDB_NOSPILL; /* TODO: Or change (flags != MDB_NODUPDATA) to ~(flags & MDB_NODUPDATA), not looking at the logic of that code just now */

	rc = mdb_cursor_touch(mc);
	if (rc)
		return rc;

	leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);

	if (!IS_LEAF2(mc->mc_pg[mc->mc_top]) && F_ISSET(leaf->mn_flags, F_DUPDATA)) {
		if (flags != MDB_NODUPDATA) {
			if (!F_ISSET(leaf->mn_flags, F_SUBDATA)) {
				mc->mc_xcursor->mx_cursor.mc_pg[0] = NODEDATA(leaf);
			}
			rc = mdb_cursor_del(&mc->mc_xcursor->mx_cursor, MDB_NOSPILL);
			/* If sub-DB still has entries, we're done */
			if (mc->mc_xcursor->mx_db.md_entries) {
				if (leaf->mn_flags & F_SUBDATA) {
					/* update subDB info */
					void *db = NODEDATA(leaf);
					memcpy(db, &mc->mc_xcursor->mx_db, sizeof(MDB_db));
				} else {
					MDB_cursor *m2;
					/* shrink fake page */
					mdb_node_shrink(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
					leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
					mc->mc_xcursor->mx_cursor.mc_pg[0] = NODEDATA(leaf);
					/* fix other sub-DB cursors pointed at this fake page */
					for (m2 = mc->mc_txn->mt_cursors[mc->mc_dbi]; m2; m2=m2->mc_next) {
						if (m2 == mc || m2->mc_snum < mc->mc_snum) continue;
						if (m2->mc_pg[mc->mc_top] == mc->mc_pg[mc->mc_top] &&
							m2->mc_ki[mc->mc_top] == mc->mc_ki[mc->mc_top])
							m2->mc_xcursor->mx_cursor.mc_pg[0] = NODEDATA(leaf);
					}
				}
				mc->mc_db->md_entries--;
				return rc;
			}
			/* otherwise fall thru and delete the sub-DB */
		}

		if (leaf->mn_flags & F_SUBDATA) {
			/* add all the child DB's pages to the free list */
			rc = mdb_drop0(&mc->mc_xcursor->mx_cursor, 0);
			if (rc == MDB_SUCCESS) {
				mc->mc_db->md_entries -=
					mc->mc_xcursor->mx_db.md_entries;
			}
		}
	}

	return mdb_cursor_del0(mc, leaf);
}

/** Allocate and initialize new pages for a database.
 * @param[in] mc a cursor on the database being added to.
 * @param[in] flags flags defining what type of page is being allocated.
 * @param[in] num the number of pages to allocate. This is usually 1,
 * unless allocating overflow pages for a large record.
 * @param[out] mp Address of a page, or NULL on failure.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_page_new(MDB_cursor *mc, uint32_t flags, int num, MDB_page **mp)
{
	MDB_page	*np;
	int rc;

	if ((rc = mdb_page_alloc(mc, num, &np)))
		return rc;
	DPRINTF("allocated new mpage %zu, page size %u",
	    np->mp_pgno, mc->mc_txn->mt_env->me_psize);
	np->mp_flags = flags | P_DIRTY;
	np->mp_lower = PAGEHDRSZ;
	np->mp_upper = mc->mc_txn->mt_env->me_psize;

	if (IS_BRANCH(np))
		mc->mc_db->md_branch_pages++;
	else if (IS_LEAF(np))
		mc->mc_db->md_leaf_pages++;
	else if (IS_OVERFLOW(np)) {
		mc->mc_db->md_overflow_pages += num;
		np->mp_pages = num;
	}
	*mp = np;

	return 0;
}

/** Calculate the size of a leaf node.
 * The size depends on the environment's page size; if a data item
 * is too large it will be put onto an overflow page and the node
 * size will only include the key and not the data. Sizes are always
 * rounded up to an even number of bytes, to guarantee 2-byte alignment
 * of the #MDB_node headers.
 * @param[in] env The environment handle.
 * @param[in] key The key for the node.
 * @param[in] data The data for the node.
 * @return The number of bytes needed to store the node.
 */
static size_t
mdb_leaf_size(MDB_env *env, MDB_val *key, MDB_val *data)
{
	size_t		 sz;

	sz = LEAFSIZE(key, data);
	if (sz >= env->me_nodemax) {
		/* put on overflow page */
		sz -= data->mv_size - sizeof(pgno_t);
	}
	sz += sz & 1;

	return sz + sizeof(indx_t);
}

/** Calculate the size of a branch node.
 * The size should depend on the environment's page size but since
 * we currently don't support spilling large keys onto overflow
 * pages, it's simply the size of the #MDB_node header plus the
 * size of the key. Sizes are always rounded up to an even number
 * of bytes, to guarantee 2-byte alignment of the #MDB_node headers.
 * @param[in] env The environment handle.
 * @param[in] key The key for the node.
 * @return The number of bytes needed to store the node.
 */
static size_t
mdb_branch_size(MDB_env *env, MDB_val *key)
{
	size_t		 sz;

	sz = INDXSIZE(key);
	if (sz >= env->me_nodemax) {
		/* put on overflow page */
		/* not implemented */
		/* sz -= key->size - sizeof(pgno_t); */
	}

	return sz + sizeof(indx_t);
}

/** Add a node to the page pointed to by the cursor.
 * @param[in] mc The cursor for this operation.
 * @param[in] indx The index on the page where the new node should be added.
 * @param[in] key The key for the new node.
 * @param[in] data The data for the new node, if any.
 * @param[in] pgno The page number, if adding a branch node.
 * @param[in] flags Flags for the node.
 * @return 0 on success, non-zero on failure. Possible errors are:
 * <ul>
 *	<li>ENOMEM - failed to allocate overflow pages for the node.
 *	<li>MDB_PAGE_FULL - there is insufficient room in the page. This error
 *	should never happen since all callers already calculate the
 *	page's free space before calling this function.
 * </ul>
 */
static int
mdb_node_add(MDB_cursor *mc, indx_t indx,
    MDB_val *key, MDB_val *data, pgno_t pgno, unsigned int flags)
{
	unsigned int	 i;
	size_t		 node_size = NODESIZE;
	indx_t		 ofs;
	MDB_node	*node;
	MDB_page	*mp = mc->mc_pg[mc->mc_top];
	MDB_page	*ofp = NULL;		/* overflow page */
	DKBUF;

	assert(mp->mp_upper >= mp->mp_lower);

	DPRINTF("add to %s %spage %zu index %i, data size %zu key size %zu [%s]",
	    IS_LEAF(mp) ? "leaf" : "branch",
		IS_SUBP(mp) ? "sub-" : "",
	    mp->mp_pgno, indx, data ? data->mv_size : 0,
		key ? key->mv_size : 0, key ? DKEY(key) : NULL);

	if (IS_LEAF2(mp)) {
		/* Move higher keys up one slot. */
		int ksize = mc->mc_db->md_pad, dif;
		char *ptr = LEAF2KEY(mp, indx, ksize);
		dif = NUMKEYS(mp) - indx;
		if (dif > 0)
			memmove(ptr+ksize, ptr, dif*ksize);
		/* insert new key */
		memcpy(ptr, key->mv_data, ksize);

		/* Just using these for counting */
		mp->mp_lower += sizeof(indx_t);
		mp->mp_upper -= ksize - sizeof(indx_t);
		return MDB_SUCCESS;
	}

	if (key != NULL)
		node_size += key->mv_size;

	if (IS_LEAF(mp)) {
		assert(data);
		if (F_ISSET(flags, F_BIGDATA)) {
			/* Data already on overflow page. */
			node_size += sizeof(pgno_t);
		} else if (node_size + data->mv_size >= mc->mc_txn->mt_env->me_nodemax) {
			int ovpages = OVPAGES(data->mv_size, mc->mc_txn->mt_env->me_psize);
			int rc;
			/* Put data on overflow page. */
			DPRINTF("data size is %zu, node would be %zu, put data on overflow page",
			    data->mv_size, node_size+data->mv_size);
			node_size += sizeof(pgno_t);
			if ((rc = mdb_page_new(mc, P_OVERFLOW, ovpages, &ofp)))
				return rc;
			DPRINTF("allocated overflow page %zu", ofp->mp_pgno);
			flags |= F_BIGDATA;
		} else {
			node_size += data->mv_size;
		}
	}
	node_size += node_size & 1;

	if (node_size + sizeof(indx_t) > SIZELEFT(mp)) {
		DPRINTF("not enough room in page %zu, got %u ptrs",
		    mp->mp_pgno, NUMKEYS(mp));
		DPRINTF("upper - lower = %u - %u = %u", mp->mp_upper, mp->mp_lower,
		    mp->mp_upper - mp->mp_lower);
		DPRINTF("node size = %zu", node_size);
		return MDB_PAGE_FULL;
	}

	/* Move higher pointers up one slot. */
	for (i = NUMKEYS(mp); i > indx; i--)
		mp->mp_ptrs[i] = mp->mp_ptrs[i - 1];

	/* Adjust free space offsets. */
	ofs = mp->mp_upper - node_size;
	assert(ofs >= mp->mp_lower + sizeof(indx_t));
	mp->mp_ptrs[indx] = ofs;
	mp->mp_upper = ofs;
	mp->mp_lower += sizeof(indx_t);

	/* Write the node data. */
	node = NODEPTR(mp, indx);
	node->mn_ksize = (key == NULL) ? 0 : key->mv_size;
	node->mn_flags = flags;
	if (IS_LEAF(mp))
		SETDSZ(node,data->mv_size);
	else
		SETPGNO(node,pgno);

	if (key)
		memcpy(NODEKEY(node), key->mv_data, key->mv_size);

	if (IS_LEAF(mp)) {
		assert(key);
		if (ofp == NULL) {
			if (F_ISSET(flags, F_BIGDATA))
				memcpy(node->mn_data + key->mv_size, data->mv_data,
				    sizeof(pgno_t));
			else if (F_ISSET(flags, MDB_RESERVE))
				data->mv_data = node->mn_data + key->mv_size;
			else
				memcpy(node->mn_data + key->mv_size, data->mv_data,
				    data->mv_size);
		} else {
			memcpy(node->mn_data + key->mv_size, &ofp->mp_pgno,
			    sizeof(pgno_t));
			if (F_ISSET(flags, MDB_RESERVE))
				data->mv_data = METADATA(ofp);
			else
				memcpy(METADATA(ofp), data->mv_data, data->mv_size);
		}
	}

	return MDB_SUCCESS;
}

/** Delete the specified node from a page.
 * @param[in] mp The page to operate on.
 * @param[in] indx The index of the node to delete.
 * @param[in] ksize The size of a node. Only used if the page is
 * part of a #MDB_DUPFIXED database.
 */
static void
mdb_node_del(MDB_page *mp, indx_t indx, int ksize)
{
	unsigned int	 sz;
	indx_t		 i, j, numkeys, ptr;
	MDB_node	*node;
	char		*base;

#if MDB_DEBUG
	{
	pgno_t pgno;
	COPY_PGNO(pgno, mp->mp_pgno);
	DPRINTF("delete node %u on %s page %zu", indx,
	    IS_LEAF(mp) ? "leaf" : "branch", pgno);
	}
#endif
	assert(indx < NUMKEYS(mp));

	if (IS_LEAF2(mp)) {
		int x = NUMKEYS(mp) - 1 - indx;
		base = LEAF2KEY(mp, indx, ksize);
		if (x)
			memmove(base, base + ksize, x * ksize);
		mp->mp_lower -= sizeof(indx_t);
		mp->mp_upper += ksize - sizeof(indx_t);
		return;
	}

	node = NODEPTR(mp, indx);
	sz = NODESIZE + node->mn_ksize;
	if (IS_LEAF(mp)) {
		if (F_ISSET(node->mn_flags, F_BIGDATA))
			sz += sizeof(pgno_t);
		else
			sz += NODEDSZ(node);
	}
	sz += sz & 1;

	ptr = mp->mp_ptrs[indx];
	numkeys = NUMKEYS(mp);
	for (i = j = 0; i < numkeys; i++) {
		if (i != indx) {
			mp->mp_ptrs[j] = mp->mp_ptrs[i];
			if (mp->mp_ptrs[i] < ptr)
				mp->mp_ptrs[j] += sz;
			j++;
		}
	}

	base = (char *)mp + mp->mp_upper;
	memmove(base + sz, base, ptr - mp->mp_upper);

	mp->mp_lower -= sizeof(indx_t);
	mp->mp_upper += sz;
}

/** Compact the main page after deleting a node on a subpage.
 * @param[in] mp The main page to operate on.
 * @param[in] indx The index of the subpage on the main page.
 */
static void
mdb_node_shrink(MDB_page *mp, indx_t indx)
{
	MDB_node *node;
	MDB_page *sp, *xp;
	char *base;
	int osize, nsize;
	int delta;
	indx_t		 i, numkeys, ptr;

	node = NODEPTR(mp, indx);
	sp = (MDB_page *)NODEDATA(node);
	osize = NODEDSZ(node);

	delta = sp->mp_upper - sp->mp_lower;
	SETDSZ(node, osize - delta);
	xp = (MDB_page *)((char *)sp + delta);

	/* shift subpage upward */
	if (IS_LEAF2(sp)) {
		nsize = NUMKEYS(sp) * sp->mp_pad;
		memmove(METADATA(xp), METADATA(sp), nsize);
	} else {
		int i;
		nsize = osize - sp->mp_upper;
		numkeys = NUMKEYS(sp);
		for (i=numkeys-1; i>=0; i--)
			xp->mp_ptrs[i] = sp->mp_ptrs[i] - delta;
	}
	xp->mp_upper = sp->mp_lower;
	xp->mp_lower = sp->mp_lower;
	xp->mp_flags = sp->mp_flags;
	xp->mp_pad = sp->mp_pad;
	COPY_PGNO(xp->mp_pgno, mp->mp_pgno);

	/* shift lower nodes upward */
	ptr = mp->mp_ptrs[indx];
	numkeys = NUMKEYS(mp);
	for (i = 0; i < numkeys; i++) {
		if (mp->mp_ptrs[i] <= ptr)
			mp->mp_ptrs[i] += delta;
	}

	base = (char *)mp + mp->mp_upper;
	memmove(base + delta, base, ptr - mp->mp_upper + NODESIZE + NODEKSZ(node));
	mp->mp_upper += delta;
}

/** Initial setup of a sorted-dups cursor.
 * Sorted duplicates are implemented as a sub-database for the given key.
 * The duplicate data items are actually keys of the sub-database.
 * Operations on the duplicate data items are performed using a sub-cursor
 * initialized when the sub-database is first accessed. This function does
 * the preliminary setup of the sub-cursor, filling in the fields that
 * depend only on the parent DB.
 * @param[in] mc The main cursor whose sorted-dups cursor is to be initialized.
 */
static void
mdb_xcursor_init0(MDB_cursor *mc)
{
	MDB_xcursor *mx = mc->mc_xcursor;

	mx->mx_cursor.mc_xcursor = NULL;
	mx->mx_cursor.mc_txn = mc->mc_txn;
	mx->mx_cursor.mc_db = &mx->mx_db;
	mx->mx_cursor.mc_dbx = &mx->mx_dbx;
	mx->mx_cursor.mc_dbi = mc->mc_dbi+1;
	mx->mx_cursor.mc_dbflag = &mx->mx_dbflag;
	mx->mx_cursor.mc_snum = 0;
	mx->mx_cursor.mc_top = 0;
	mx->mx_cursor.mc_flags = C_SUB;
	mx->mx_dbx.md_cmp = mc->mc_dbx->md_dcmp;
	mx->mx_dbx.md_dcmp = NULL;
	mx->mx_dbx.md_rel = mc->mc_dbx->md_rel;
}

/** Final setup of a sorted-dups cursor.
 *	Sets up the fields that depend on the data from the main cursor.
 * @param[in] mc The main cursor whose sorted-dups cursor is to be initialized.
 * @param[in] node The data containing the #MDB_db record for the
 * sorted-dup database.
 */
static void
mdb_xcursor_init1(MDB_cursor *mc, MDB_node *node)
{
	MDB_xcursor *mx = mc->mc_xcursor;

	if (node->mn_flags & F_SUBDATA) {
		memcpy(&mx->mx_db, NODEDATA(node), sizeof(MDB_db));
		mx->mx_cursor.mc_pg[0] = 0;
		mx->mx_cursor.mc_snum = 0;
		mx->mx_cursor.mc_flags = C_SUB;
	} else {
		MDB_page *fp = NODEDATA(node);
		mx->mx_db.md_pad = mc->mc_pg[mc->mc_top]->mp_pad;
		mx->mx_db.md_flags = 0;
		mx->mx_db.md_depth = 1;
		mx->mx_db.md_branch_pages = 0;
		mx->mx_db.md_leaf_pages = 1;
		mx->mx_db.md_overflow_pages = 0;
		mx->mx_db.md_entries = NUMKEYS(fp);
		COPY_PGNO(mx->mx_db.md_root, fp->mp_pgno);
		mx->mx_cursor.mc_snum = 1;
		mx->mx_cursor.mc_flags = C_INITIALIZED|C_SUB;
		mx->mx_cursor.mc_top = 0;
		mx->mx_cursor.mc_pg[0] = fp;
		mx->mx_cursor.mc_ki[0] = 0;
		if (mc->mc_db->md_flags & MDB_DUPFIXED) {
			mx->mx_db.md_flags = MDB_DUPFIXED;
			mx->mx_db.md_pad = fp->mp_pad;
			if (mc->mc_db->md_flags & MDB_INTEGERDUP)
				mx->mx_db.md_flags |= MDB_INTEGERKEY;
		}
	}
	DPRINTF("Sub-db %u for db %u root page %zu", mx->mx_cursor.mc_dbi, mc->mc_dbi,
		mx->mx_db.md_root);
	mx->mx_dbflag = DB_VALID | (F_ISSET(mc->mc_pg[mc->mc_top]->mp_flags, P_DIRTY) ?
		DB_DIRTY : 0);
	mx->mx_dbx.md_name.mv_data = NODEKEY(node);
	mx->mx_dbx.md_name.mv_size = node->mn_ksize;
#if UINT_MAX < SIZE_MAX
	if (mx->mx_dbx.md_cmp == mdb_cmp_int && mx->mx_db.md_pad == sizeof(size_t))
#ifdef MISALIGNED_OK
		mx->mx_dbx.md_cmp = mdb_cmp_long;
#else
		mx->mx_dbx.md_cmp = mdb_cmp_cint;
#endif
#endif
}

/** Initialize a cursor for a given transaction and database. */
static void
mdb_cursor_init(MDB_cursor *mc, MDB_txn *txn, MDB_dbi dbi, MDB_xcursor *mx)
{
	mc->mc_next = NULL;
	mc->mc_backup = NULL;
	mc->mc_dbi = dbi;
	mc->mc_txn = txn;
	mc->mc_db = &txn->mt_dbs[dbi];
	mc->mc_dbx = &txn->mt_dbxs[dbi];
	mc->mc_dbflag = &txn->mt_dbflags[dbi];
	mc->mc_snum = 0;
	mc->mc_top = 0;
	mc->mc_pg[0] = 0;
	mc->mc_flags = 0;
	if (txn->mt_dbs[dbi].md_flags & MDB_DUPSORT) {
		assert(mx != NULL);
		mc->mc_xcursor = mx;
		mdb_xcursor_init0(mc);
	} else {
		mc->mc_xcursor = NULL;
	}
	if (*mc->mc_dbflag & DB_STALE) {
		mdb_page_search(mc, NULL, MDB_PS_ROOTONLY);
	}
}

int
mdb_cursor_open(MDB_txn *txn, MDB_dbi dbi, MDB_cursor **ret)
{
	MDB_cursor	*mc;
	size_t size = sizeof(MDB_cursor);

	if (txn == NULL || ret == NULL || dbi >= txn->mt_numdbs || !(txn->mt_dbflags[dbi] & DB_VALID))
		return EINVAL;

	/* Allow read access to the freelist */
	if (!dbi && !F_ISSET(txn->mt_flags, MDB_TXN_RDONLY))
		return EINVAL;

	if (txn->mt_dbs[dbi].md_flags & MDB_DUPSORT)
		size += sizeof(MDB_xcursor);

	if ((mc = malloc(size)) != NULL) {
		mdb_cursor_init(mc, txn, dbi, (MDB_xcursor *)(mc + 1));
		if (txn->mt_cursors) {
			mc->mc_next = txn->mt_cursors[dbi];
			txn->mt_cursors[dbi] = mc;
			mc->mc_flags |= C_UNTRACK;
		}
	} else {
		return ENOMEM;
	}

	*ret = mc;

	return MDB_SUCCESS;
}

int
mdb_cursor_renew(MDB_txn *txn, MDB_cursor *mc)
{
	if (txn == NULL || mc == NULL || mc->mc_dbi >= txn->mt_numdbs)
		return EINVAL;

	if ((mc->mc_flags & C_UNTRACK) || txn->mt_cursors)
		return EINVAL;

	mdb_cursor_init(mc, txn, mc->mc_dbi, mc->mc_xcursor);
	return MDB_SUCCESS;
}

/* Return the count of duplicate data items for the current key */
int
mdb_cursor_count(MDB_cursor *mc, size_t *countp)
{
	MDB_node	*leaf;

	if (mc == NULL || countp == NULL)
		return EINVAL;

	if (!(mc->mc_db->md_flags & MDB_DUPSORT))
		return EINVAL;

	leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
	if (!F_ISSET(leaf->mn_flags, F_DUPDATA)) {
		*countp = 1;
	} else {
		if (!(mc->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED))
			return EINVAL;

		*countp = mc->mc_xcursor->mx_db.md_entries;
	}
	return MDB_SUCCESS;
}

void
mdb_cursor_close(MDB_cursor *mc)
{
	if (mc && !mc->mc_backup) {
		/* remove from txn, if tracked */
		if ((mc->mc_flags & C_UNTRACK) && mc->mc_txn->mt_cursors) {
			MDB_cursor **prev = &mc->mc_txn->mt_cursors[mc->mc_dbi];
			while (*prev && *prev != mc) prev = &(*prev)->mc_next;
			if (*prev == mc)
				*prev = mc->mc_next;
		}
		free(mc);
	}
}

MDB_txn *
mdb_cursor_txn(MDB_cursor *mc)
{
	if (!mc) return NULL;
	return mc->mc_txn;
}

MDB_dbi
mdb_cursor_dbi(MDB_cursor *mc)
{
	assert(mc != NULL);
	return mc->mc_dbi;
}

/** Replace the key for a node with a new key.
 * @param[in] mc Cursor pointing to the node to operate on.
 * @param[in] key The new key to use.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_update_key(MDB_cursor *mc, MDB_val *key)
{
	MDB_page		*mp;
	MDB_node		*node;
	char			*base;
	size_t			 len;
	int			 delta, delta0;
	indx_t			 ptr, i, numkeys, indx;
	DKBUF;

	indx = mc->mc_ki[mc->mc_top];
	mp = mc->mc_pg[mc->mc_top];
	node = NODEPTR(mp, indx);
	ptr = mp->mp_ptrs[indx];
#if MDB_DEBUG
	{
		MDB_val	k2;
		char kbuf2[(MDB_MAXKEYSIZE*2+1)];
		k2.mv_data = NODEKEY(node);
		k2.mv_size = node->mn_ksize;
		DPRINTF("update key %u (ofs %u) [%s] to [%s] on page %zu",
			indx, ptr,
			mdb_dkey(&k2, kbuf2),
			DKEY(key),
			mp->mp_pgno);
	}
#endif

	delta0 = delta = key->mv_size - node->mn_ksize;

	/* Must be 2-byte aligned. If new key is
	 * shorter by 1, the shift will be skipped.
	 */
	delta += (delta & 1);
	if (delta) {
		if (delta > 0 && SIZELEFT(mp) < delta) {
			pgno_t pgno;
			/* not enough space left, do a delete and split */
			DPRINTF("Not enough room, delta = %d, splitting...", delta);
			pgno = NODEPGNO(node);
			mdb_node_del(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top], 0);
			return mdb_page_split(mc, key, NULL, pgno, MDB_SPLIT_REPLACE);
		}

		numkeys = NUMKEYS(mp);
		for (i = 0; i < numkeys; i++) {
			if (mp->mp_ptrs[i] <= ptr)
				mp->mp_ptrs[i] -= delta;
		}

		base = (char *)mp + mp->mp_upper;
		len = ptr - mp->mp_upper + NODESIZE;
		memmove(base - delta, base, len);
		mp->mp_upper -= delta;

		node = NODEPTR(mp, indx);
	}

	/* But even if no shift was needed, update ksize */
	if (delta0)
		node->mn_ksize = key->mv_size;

	if (key->mv_size)
		memcpy(NODEKEY(node), key->mv_data, key->mv_size);

	return MDB_SUCCESS;
}

static void
mdb_cursor_copy(const MDB_cursor *csrc, MDB_cursor *cdst);

/** Move a node from csrc to cdst.
 */
static int
mdb_node_move(MDB_cursor *csrc, MDB_cursor *cdst)
{
	MDB_node		*srcnode;
	MDB_val		 key, data;
	pgno_t	srcpg;
	MDB_cursor mn;
	int			 rc;
	unsigned short flags;

	DKBUF;

	/* Mark src and dst as dirty. */
	if ((rc = mdb_page_touch(csrc)) ||
	    (rc = mdb_page_touch(cdst)))
		return rc;

	if (IS_LEAF2(csrc->mc_pg[csrc->mc_top])) {
		srcnode = NODEPTR(csrc->mc_pg[csrc->mc_top], 0);	/* fake */
		key.mv_size = csrc->mc_db->md_pad;
		key.mv_data = LEAF2KEY(csrc->mc_pg[csrc->mc_top], csrc->mc_ki[csrc->mc_top], key.mv_size);
		data.mv_size = 0;
		data.mv_data = NULL;
		srcpg = 0;
		flags = 0;
	} else {
		srcnode = NODEPTR(csrc->mc_pg[csrc->mc_top], csrc->mc_ki[csrc->mc_top]);
		assert(!((long)srcnode&1));
		srcpg = NODEPGNO(srcnode);
		flags = srcnode->mn_flags;
		if (csrc->mc_ki[csrc->mc_top] == 0 && IS_BRANCH(csrc->mc_pg[csrc->mc_top])) {
			unsigned int snum = csrc->mc_snum;
			MDB_node *s2;
			/* must find the lowest key below src */
			mdb_page_search_lowest(csrc);
			if (IS_LEAF2(csrc->mc_pg[csrc->mc_top])) {
				key.mv_size = csrc->mc_db->md_pad;
				key.mv_data = LEAF2KEY(csrc->mc_pg[csrc->mc_top], 0, key.mv_size);
			} else {
				s2 = NODEPTR(csrc->mc_pg[csrc->mc_top], 0);
				key.mv_size = NODEKSZ(s2);
				key.mv_data = NODEKEY(s2);
			}
			csrc->mc_snum = snum--;
			csrc->mc_top = snum;
		} else {
			key.mv_size = NODEKSZ(srcnode);
			key.mv_data = NODEKEY(srcnode);
		}
		data.mv_size = NODEDSZ(srcnode);
		data.mv_data = NODEDATA(srcnode);
	}
	if (IS_BRANCH(cdst->mc_pg[cdst->mc_top]) && cdst->mc_ki[cdst->mc_top] == 0) {
		unsigned int snum = cdst->mc_snum;
		MDB_node *s2;
		MDB_val bkey;
		/* must find the lowest key below dst */
		mdb_page_search_lowest(cdst);
		if (IS_LEAF2(cdst->mc_pg[cdst->mc_top])) {
			bkey.mv_size = cdst->mc_db->md_pad;
			bkey.mv_data = LEAF2KEY(cdst->mc_pg[cdst->mc_top], 0, bkey.mv_size);
		} else {
			s2 = NODEPTR(cdst->mc_pg[cdst->mc_top], 0);
			bkey.mv_size = NODEKSZ(s2);
			bkey.mv_data = NODEKEY(s2);
		}
		cdst->mc_snum = snum--;
		cdst->mc_top = snum;
		mdb_cursor_copy(cdst, &mn);
		mn.mc_ki[snum] = 0;
		rc = mdb_update_key(&mn, &bkey);
		if (rc)
			return rc;
	}

	DPRINTF("moving %s node %u [%s] on page %zu to node %u on page %zu",
	    IS_LEAF(csrc->mc_pg[csrc->mc_top]) ? "leaf" : "branch",
	    csrc->mc_ki[csrc->mc_top],
		DKEY(&key),
	    csrc->mc_pg[csrc->mc_top]->mp_pgno,
	    cdst->mc_ki[cdst->mc_top], cdst->mc_pg[cdst->mc_top]->mp_pgno);

	/* Add the node to the destination page.
	 */
	rc = mdb_node_add(cdst, cdst->mc_ki[cdst->mc_top], &key, &data, srcpg, flags);
	if (rc != MDB_SUCCESS)
		return rc;

	/* Delete the node from the source page.
	 */
	mdb_node_del(csrc->mc_pg[csrc->mc_top], csrc->mc_ki[csrc->mc_top], key.mv_size);

	{
		/* Adjust other cursors pointing to mp */
		MDB_cursor *m2, *m3;
		MDB_dbi dbi = csrc->mc_dbi;
		MDB_page *mp = csrc->mc_pg[csrc->mc_top];

		if (csrc->mc_flags & C_SUB)
			dbi--;

		for (m2 = csrc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
			if (csrc->mc_flags & C_SUB)
				m3 = &m2->mc_xcursor->mx_cursor;
			else
				m3 = m2;
			if (m3 == csrc) continue;
			if (m3->mc_pg[csrc->mc_top] == mp && m3->mc_ki[csrc->mc_top] ==
				csrc->mc_ki[csrc->mc_top]) {
				m3->mc_pg[csrc->mc_top] = cdst->mc_pg[cdst->mc_top];
				m3->mc_ki[csrc->mc_top] = cdst->mc_ki[cdst->mc_top];
			}
		}
	}

	/* Update the parent separators.
	 */
	if (csrc->mc_ki[csrc->mc_top] == 0) {
		if (csrc->mc_ki[csrc->mc_top-1] != 0) {
			if (IS_LEAF2(csrc->mc_pg[csrc->mc_top])) {
				key.mv_data = LEAF2KEY(csrc->mc_pg[csrc->mc_top], 0, key.mv_size);
			} else {
				srcnode = NODEPTR(csrc->mc_pg[csrc->mc_top], 0);
				key.mv_size = NODEKSZ(srcnode);
				key.mv_data = NODEKEY(srcnode);
			}
			DPRINTF("update separator for source page %zu to [%s]",
				csrc->mc_pg[csrc->mc_top]->mp_pgno, DKEY(&key));
			mdb_cursor_copy(csrc, &mn);
			mn.mc_snum--;
			mn.mc_top--;
			if ((rc = mdb_update_key(&mn, &key)) != MDB_SUCCESS)
				return rc;
		}
		if (IS_BRANCH(csrc->mc_pg[csrc->mc_top])) {
			MDB_val	 nullkey;
			indx_t	ix = csrc->mc_ki[csrc->mc_top];
			nullkey.mv_size = 0;
			csrc->mc_ki[csrc->mc_top] = 0;
			rc = mdb_update_key(csrc, &nullkey);
			csrc->mc_ki[csrc->mc_top] = ix;
			assert(rc == MDB_SUCCESS);
		}
	}

	if (cdst->mc_ki[cdst->mc_top] == 0) {
		if (cdst->mc_ki[cdst->mc_top-1] != 0) {
			if (IS_LEAF2(csrc->mc_pg[csrc->mc_top])) {
				key.mv_data = LEAF2KEY(cdst->mc_pg[cdst->mc_top], 0, key.mv_size);
			} else {
				srcnode = NODEPTR(cdst->mc_pg[cdst->mc_top], 0);
				key.mv_size = NODEKSZ(srcnode);
				key.mv_data = NODEKEY(srcnode);
			}
			DPRINTF("update separator for destination page %zu to [%s]",
				cdst->mc_pg[cdst->mc_top]->mp_pgno, DKEY(&key));
			mdb_cursor_copy(cdst, &mn);
			mn.mc_snum--;
			mn.mc_top--;
			if ((rc = mdb_update_key(&mn, &key)) != MDB_SUCCESS)
				return rc;
		}
		if (IS_BRANCH(cdst->mc_pg[cdst->mc_top])) {
			MDB_val	 nullkey;
			indx_t	ix = cdst->mc_ki[cdst->mc_top];
			nullkey.mv_size = 0;
			cdst->mc_ki[cdst->mc_top] = 0;
			rc = mdb_update_key(cdst, &nullkey);
			cdst->mc_ki[cdst->mc_top] = ix;
			assert(rc == MDB_SUCCESS);
		}
	}

	return MDB_SUCCESS;
}

/** Merge one page into another.
 *  The nodes from the page pointed to by \b csrc will
 *	be copied to the page pointed to by \b cdst and then
 *	the \b csrc page will be freed.
 * @param[in] csrc Cursor pointing to the source page.
 * @param[in] cdst Cursor pointing to the destination page.
 */
static int
mdb_page_merge(MDB_cursor *csrc, MDB_cursor *cdst)
{
	int			 rc;
	indx_t			 i, j;
	MDB_node		*srcnode;
	MDB_val		 key, data;
	unsigned	nkeys;

	DPRINTF("merging page %zu into %zu", csrc->mc_pg[csrc->mc_top]->mp_pgno,
		cdst->mc_pg[cdst->mc_top]->mp_pgno);

	assert(csrc->mc_snum > 1);	/* can't merge root page */
	assert(cdst->mc_snum > 1);

	/* Mark dst as dirty. */
	if ((rc = mdb_page_touch(cdst)))
		return rc;

	/* Move all nodes from src to dst.
	 */
	j = nkeys = NUMKEYS(cdst->mc_pg[cdst->mc_top]);
	if (IS_LEAF2(csrc->mc_pg[csrc->mc_top])) {
		key.mv_size = csrc->mc_db->md_pad;
		key.mv_data = METADATA(csrc->mc_pg[csrc->mc_top]);
		for (i = 0; i < NUMKEYS(csrc->mc_pg[csrc->mc_top]); i++, j++) {
			rc = mdb_node_add(cdst, j, &key, NULL, 0, 0);
			if (rc != MDB_SUCCESS)
				return rc;
			key.mv_data = (char *)key.mv_data + key.mv_size;
		}
	} else {
		for (i = 0; i < NUMKEYS(csrc->mc_pg[csrc->mc_top]); i++, j++) {
			srcnode = NODEPTR(csrc->mc_pg[csrc->mc_top], i);
			if (i == 0 && IS_BRANCH(csrc->mc_pg[csrc->mc_top])) {
				unsigned int snum = csrc->mc_snum;
				MDB_node *s2;
				/* must find the lowest key below src */
				mdb_page_search_lowest(csrc);
				if (IS_LEAF2(csrc->mc_pg[csrc->mc_top])) {
					key.mv_size = csrc->mc_db->md_pad;
					key.mv_data = LEAF2KEY(csrc->mc_pg[csrc->mc_top], 0, key.mv_size);
				} else {
					s2 = NODEPTR(csrc->mc_pg[csrc->mc_top], 0);
					key.mv_size = NODEKSZ(s2);
					key.mv_data = NODEKEY(s2);
				}
				csrc->mc_snum = snum--;
				csrc->mc_top = snum;
			} else {
				key.mv_size = srcnode->mn_ksize;
				key.mv_data = NODEKEY(srcnode);
			}

			data.mv_size = NODEDSZ(srcnode);
			data.mv_data = NODEDATA(srcnode);
			rc = mdb_node_add(cdst, j, &key, &data, NODEPGNO(srcnode), srcnode->mn_flags);
			if (rc != MDB_SUCCESS)
				return rc;
		}
	}

	DPRINTF("dst page %zu now has %u keys (%.1f%% filled)",
	    cdst->mc_pg[cdst->mc_top]->mp_pgno, NUMKEYS(cdst->mc_pg[cdst->mc_top]), (float)PAGEFILL(cdst->mc_txn->mt_env, cdst->mc_pg[cdst->mc_top]) / 10);

	/* Unlink the src page from parent and add to free list.
	 */
	mdb_node_del(csrc->mc_pg[csrc->mc_top-1], csrc->mc_ki[csrc->mc_top-1], 0);
	if (csrc->mc_ki[csrc->mc_top-1] == 0) {
		key.mv_size = 0;
		csrc->mc_top--;
		rc = mdb_update_key(csrc, &key);
		csrc->mc_top++;
		if (rc)
			return rc;
	}

	rc = mdb_midl_append(&csrc->mc_txn->mt_free_pgs,
		csrc->mc_pg[csrc->mc_top]->mp_pgno);
	if (rc)
		return rc;
	if (IS_LEAF(csrc->mc_pg[csrc->mc_top]))
		csrc->mc_db->md_leaf_pages--;
	else
		csrc->mc_db->md_branch_pages--;
	{
		/* Adjust other cursors pointing to mp */
		MDB_cursor *m2, *m3;
		MDB_dbi dbi = csrc->mc_dbi;
		MDB_page *mp = cdst->mc_pg[cdst->mc_top];

		if (csrc->mc_flags & C_SUB)
			dbi--;

		for (m2 = csrc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
			if (csrc->mc_flags & C_SUB)
				m3 = &m2->mc_xcursor->mx_cursor;
			else
				m3 = m2;
			if (m3 == csrc) continue;
			if (m3->mc_snum < csrc->mc_snum) continue;
			if (m3->mc_pg[csrc->mc_top] == csrc->mc_pg[csrc->mc_top]) {
				m3->mc_pg[csrc->mc_top] = mp;
				m3->mc_ki[csrc->mc_top] += nkeys;
			}
		}
	}
	mdb_cursor_pop(csrc);

	return mdb_rebalance(csrc);
}

/** Copy the contents of a cursor.
 * @param[in] csrc The cursor to copy from.
 * @param[out] cdst The cursor to copy to.
 */
static void
mdb_cursor_copy(const MDB_cursor *csrc, MDB_cursor *cdst)
{
	unsigned int i;

	cdst->mc_txn = csrc->mc_txn;
	cdst->mc_dbi = csrc->mc_dbi;
	cdst->mc_db  = csrc->mc_db;
	cdst->mc_dbx = csrc->mc_dbx;
	cdst->mc_snum = csrc->mc_snum;
	cdst->mc_top = csrc->mc_top;
	cdst->mc_flags = csrc->mc_flags;

	for (i=0; i<csrc->mc_snum; i++) {
		cdst->mc_pg[i] = csrc->mc_pg[i];
		cdst->mc_ki[i] = csrc->mc_ki[i];
	}
}

/** Rebalance the tree after a delete operation.
 * @param[in] mc Cursor pointing to the page where rebalancing
 * should begin.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_rebalance(MDB_cursor *mc)
{
	MDB_node	*node;
	int rc;
	unsigned int ptop, minkeys;
	MDB_cursor	mn;

	minkeys = 1 + (IS_BRANCH(mc->mc_pg[mc->mc_top]));
#if MDB_DEBUG
	{
	pgno_t pgno;
	COPY_PGNO(pgno, mc->mc_pg[mc->mc_top]->mp_pgno);
	DPRINTF("rebalancing %s page %zu (has %u keys, %.1f%% full)",
	    IS_LEAF(mc->mc_pg[mc->mc_top]) ? "leaf" : "branch",
	    pgno, NUMKEYS(mc->mc_pg[mc->mc_top]), (float)PAGEFILL(mc->mc_txn->mt_env, mc->mc_pg[mc->mc_top]) / 10);
	}
#endif

	if (PAGEFILL(mc->mc_txn->mt_env, mc->mc_pg[mc->mc_top]) >= FILL_THRESHOLD &&
		NUMKEYS(mc->mc_pg[mc->mc_top]) >= minkeys) {
#if MDB_DEBUG
		pgno_t pgno;
		COPY_PGNO(pgno, mc->mc_pg[mc->mc_top]->mp_pgno);
		DPRINTF("no need to rebalance page %zu, above fill threshold",
		    pgno);
#endif
		return MDB_SUCCESS;
	}

	if (mc->mc_snum < 2) {
		MDB_page *mp = mc->mc_pg[0];
		if (IS_SUBP(mp)) {
			DPUTS("Can't rebalance a subpage, ignoring");
			return MDB_SUCCESS;
		}
		if (NUMKEYS(mp) == 0) {
			DPUTS("tree is completely empty");
			mc->mc_db->md_root = P_INVALID;
			mc->mc_db->md_depth = 0;
			mc->mc_db->md_leaf_pages = 0;
			rc = mdb_midl_append(&mc->mc_txn->mt_free_pgs, mp->mp_pgno);
			if (rc)
				return rc;
			/* Adjust cursors pointing to mp */
			mc->mc_snum = 0;
			mc->mc_top = 0;
			{
				MDB_cursor *m2, *m3;
				MDB_dbi dbi = mc->mc_dbi;

				if (mc->mc_flags & C_SUB)
					dbi--;

				for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
					if (mc->mc_flags & C_SUB)
						m3 = &m2->mc_xcursor->mx_cursor;
					else
						m3 = m2;
					if (m3->mc_snum < mc->mc_snum) continue;
					if (m3->mc_pg[0] == mp) {
						m3->mc_snum = 0;
						m3->mc_top = 0;
					}
				}
			}
		} else if (IS_BRANCH(mp) && NUMKEYS(mp) == 1) {
			DPUTS("collapsing root page!");
			rc = mdb_midl_append(&mc->mc_txn->mt_free_pgs, mp->mp_pgno);
			if (rc)
				return rc;
			mc->mc_db->md_root = NODEPGNO(NODEPTR(mp, 0));
			rc = mdb_page_get(mc->mc_txn,mc->mc_db->md_root,&mc->mc_pg[0],NULL);
			if (rc)
				return rc;
			mc->mc_db->md_depth--;
			mc->mc_db->md_branch_pages--;
			mc->mc_ki[0] = mc->mc_ki[1];
			{
				/* Adjust other cursors pointing to mp */
				MDB_cursor *m2, *m3;
				MDB_dbi dbi = mc->mc_dbi;

				if (mc->mc_flags & C_SUB)
					dbi--;

				for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
					if (mc->mc_flags & C_SUB)
						m3 = &m2->mc_xcursor->mx_cursor;
					else
						m3 = m2;
					if (m3 == mc || m3->mc_snum < mc->mc_snum) continue;
					if (m3->mc_pg[0] == mp) {
						m3->mc_pg[0] = mc->mc_pg[0];
						m3->mc_snum = 1;
						m3->mc_top = 0;
						m3->mc_ki[0] = m3->mc_ki[1];
					}
				}
			}
		} else
			DPUTS("root page doesn't need rebalancing");
		return MDB_SUCCESS;
	}

	/* The parent (branch page) must have at least 2 pointers,
	 * otherwise the tree is invalid.
	 */
	ptop = mc->mc_top-1;
	assert(NUMKEYS(mc->mc_pg[ptop]) > 1);

	/* Leaf page fill factor is below the threshold.
	 * Try to move keys from left or right neighbor, or
	 * merge with a neighbor page.
	 */

	/* Find neighbors.
	 */
	mdb_cursor_copy(mc, &mn);
	mn.mc_xcursor = NULL;

	if (mc->mc_ki[ptop] == 0) {
		/* We're the leftmost leaf in our parent.
		 */
		DPUTS("reading right neighbor");
		mn.mc_ki[ptop]++;
		node = NODEPTR(mc->mc_pg[ptop], mn.mc_ki[ptop]);
		rc = mdb_page_get(mc->mc_txn,NODEPGNO(node),&mn.mc_pg[mn.mc_top],NULL);
		if (rc)
			return rc;
		mn.mc_ki[mn.mc_top] = 0;
		mc->mc_ki[mc->mc_top] = NUMKEYS(mc->mc_pg[mc->mc_top]);
	} else {
		/* There is at least one neighbor to the left.
		 */
		DPUTS("reading left neighbor");
		mn.mc_ki[ptop]--;
		node = NODEPTR(mc->mc_pg[ptop], mn.mc_ki[ptop]);
		rc = mdb_page_get(mc->mc_txn,NODEPGNO(node),&mn.mc_pg[mn.mc_top],NULL);
		if (rc)
			return rc;
		mn.mc_ki[mn.mc_top] = NUMKEYS(mn.mc_pg[mn.mc_top]) - 1;
		mc->mc_ki[mc->mc_top] = 0;
	}

	DPRINTF("found neighbor page %zu (%u keys, %.1f%% full)",
	    mn.mc_pg[mn.mc_top]->mp_pgno, NUMKEYS(mn.mc_pg[mn.mc_top]), (float)PAGEFILL(mc->mc_txn->mt_env, mn.mc_pg[mn.mc_top]) / 10);

	/* If the neighbor page is above threshold and has enough keys,
	 * move one key from it. Otherwise we should try to merge them.
	 * (A branch page must never have less than 2 keys.)
	 */
	minkeys = 1 + (IS_BRANCH(mn.mc_pg[mn.mc_top]));
	if (PAGEFILL(mc->mc_txn->mt_env, mn.mc_pg[mn.mc_top]) >= FILL_THRESHOLD && NUMKEYS(mn.mc_pg[mn.mc_top]) > minkeys)
		return mdb_node_move(&mn, mc);
	else {
		if (mc->mc_ki[ptop] == 0)
			rc = mdb_page_merge(&mn, mc);
		else
			rc = mdb_page_merge(mc, &mn);
		mc->mc_flags &= ~(C_INITIALIZED|C_EOF);
	}
	return rc;
}

/** Complete a delete operation started by #mdb_cursor_del(). */
static int
mdb_cursor_del0(MDB_cursor *mc, MDB_node *leaf)
{
	int rc;
	MDB_page *mp;
	indx_t ki;

	mp = mc->mc_pg[mc->mc_top];
	ki = mc->mc_ki[mc->mc_top];

	/* add overflow pages to free list */
	if (!IS_LEAF2(mp) && F_ISSET(leaf->mn_flags, F_BIGDATA)) {
		MDB_page *omp;
		pgno_t pg;

		memcpy(&pg, NODEDATA(leaf), sizeof(pg));
		if ((rc = mdb_page_get(mc->mc_txn, pg, &omp, NULL)) ||
			(rc = mdb_ovpage_free(mc, omp)))
			return rc;
	}
	mdb_node_del(mp, ki, mc->mc_db->md_pad);
	mc->mc_db->md_entries--;
	rc = mdb_rebalance(mc);
	if (rc != MDB_SUCCESS)
		mc->mc_txn->mt_flags |= MDB_TXN_ERROR;
	/* if mc points past last node in page, invalidate */
	else if (mc->mc_ki[mc->mc_top] >= NUMKEYS(mc->mc_pg[mc->mc_top]))
		mc->mc_flags &= ~(C_INITIALIZED|C_EOF);

	{
		/* Adjust other cursors pointing to mp */
		MDB_cursor *m2;
		unsigned int nkeys;
		MDB_dbi dbi = mc->mc_dbi;

		mp = mc->mc_pg[mc->mc_top];
		nkeys = NUMKEYS(mp);
		for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
			if (m2 == mc)
				continue;
			if (!(m2->mc_flags & C_INITIALIZED))
				continue;
			if (m2->mc_pg[mc->mc_top] == mp) {
				if (m2->mc_ki[mc->mc_top] > ki)
					m2->mc_ki[mc->mc_top]--;
				if (m2->mc_ki[mc->mc_top] >= nkeys)
					m2->mc_flags &= ~(C_INITIALIZED|C_EOF);
			}
		}
	}

	return rc;
}

int
mdb_del(MDB_txn *txn, MDB_dbi dbi,
    MDB_val *key, MDB_val *data)
{
	MDB_cursor mc;
	MDB_xcursor mx;
	MDB_cursor_op op;
	MDB_val rdata, *xdata;
	int		 rc, exact;
	DKBUF;

	assert(key != NULL);

	DPRINTF("====> delete db %u key [%s]", dbi, DKEY(key));

	if (txn == NULL || !dbi || dbi >= txn->mt_numdbs || !(txn->mt_dbflags[dbi] & DB_VALID))
		return EINVAL;

	if (F_ISSET(txn->mt_flags, MDB_TXN_RDONLY)) {
		return EACCES;
	}

	if (key->mv_size == 0 || key->mv_size > MDB_MAXKEYSIZE) {
		return EINVAL;
	}

	mdb_cursor_init(&mc, txn, dbi, &mx);

	exact = 0;
	if (data) {
		op = MDB_GET_BOTH;
		rdata = *data;
		xdata = &rdata;
	} else {
		op = MDB_SET;
		xdata = NULL;
	}
	rc = mdb_cursor_set(&mc, key, xdata, op, &exact);
	if (rc == 0) {
		/* let mdb_page_split know about this cursor if needed:
		 * delete will trigger a rebalance; if it needs to move
		 * a node from one page to another, it will have to
		 * update the parent's separator key(s). If the new sepkey
		 * is larger than the current one, the parent page may
		 * run out of space, triggering a split. We need this
		 * cursor to be consistent until the end of the rebalance.
		 */
		mc.mc_flags |= C_UNTRACK;
		mc.mc_next = txn->mt_cursors[dbi];
		txn->mt_cursors[dbi] = &mc;
		rc = mdb_cursor_del(&mc, data ? 0 : MDB_NODUPDATA);
		txn->mt_cursors[dbi] = mc.mc_next;
	}
	return rc;
}

/** Split a page and insert a new node.
 * @param[in,out] mc Cursor pointing to the page and desired insertion index.
 * The cursor will be updated to point to the actual page and index where
 * the node got inserted after the split.
 * @param[in] newkey The key for the newly inserted node.
 * @param[in] newdata The data for the newly inserted node.
 * @param[in] newpgno The page number, if the new node is a branch node.
 * @param[in] nflags The #NODE_ADD_FLAGS for the new node.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_page_split(MDB_cursor *mc, MDB_val *newkey, MDB_val *newdata, pgno_t newpgno,
	unsigned int nflags)
{
	unsigned int flags;
	int		 rc = MDB_SUCCESS, ins_new = 0, new_root = 0, newpos = 1, did_split = 0;
	indx_t		 newindx;
	pgno_t		 pgno = 0;
	unsigned int	 i, j, split_indx, nkeys, pmax;
	MDB_node	*node;
	MDB_val	 sepkey, rkey, xdata, *rdata = &xdata;
	MDB_page	*copy;
	MDB_page	*mp, *rp, *pp;
	unsigned int ptop;
	MDB_cursor	mn;
	DKBUF;

	mp = mc->mc_pg[mc->mc_top];
	newindx = mc->mc_ki[mc->mc_top];

	DPRINTF("-----> splitting %s page %zu and adding [%s] at index %i",
	    IS_LEAF(mp) ? "leaf" : "branch", mp->mp_pgno,
	    DKEY(newkey), mc->mc_ki[mc->mc_top]);

	/* Create a right sibling. */
	if ((rc = mdb_page_new(mc, mp->mp_flags, 1, &rp)))
		return rc;
	DPRINTF("new right sibling: page %zu", rp->mp_pgno);

	if (mc->mc_snum < 2) {
		if ((rc = mdb_page_new(mc, P_BRANCH, 1, &pp)))
			return rc;
		/* shift current top to make room for new parent */
		mc->mc_pg[1] = mc->mc_pg[0];
		mc->mc_ki[1] = mc->mc_ki[0];
		mc->mc_pg[0] = pp;
		mc->mc_ki[0] = 0;
		mc->mc_db->md_root = pp->mp_pgno;
		DPRINTF("root split! new root = %zu", pp->mp_pgno);
		mc->mc_db->md_depth++;
		new_root = 1;

		/* Add left (implicit) pointer. */
		if ((rc = mdb_node_add(mc, 0, NULL, NULL, mp->mp_pgno, 0)) != MDB_SUCCESS) {
			/* undo the pre-push */
			mc->mc_pg[0] = mc->mc_pg[1];
			mc->mc_ki[0] = mc->mc_ki[1];
			mc->mc_db->md_root = mp->mp_pgno;
			mc->mc_db->md_depth--;
			return rc;
		}
		mc->mc_snum = 2;
		mc->mc_top = 1;
		ptop = 0;
	} else {
		ptop = mc->mc_top-1;
		DPRINTF("parent branch page is %zu", mc->mc_pg[ptop]->mp_pgno);
	}

	mc->mc_flags |= C_SPLITTING;
	mdb_cursor_copy(mc, &mn);
	mn.mc_pg[mn.mc_top] = rp;
	mn.mc_ki[ptop] = mc->mc_ki[ptop]+1;

	if (nflags & MDB_APPEND) {
		mn.mc_ki[mn.mc_top] = 0;
		sepkey = *newkey;
		split_indx = newindx;
		nkeys = 0;
		goto newsep;
	}

	nkeys = NUMKEYS(mp);
	split_indx = nkeys / 2;
	if (newindx < split_indx)
		newpos = 0;

	if (IS_LEAF2(rp)) {
		char *split, *ins;
		int x;
		unsigned int lsize, rsize, ksize;
		/* Move half of the keys to the right sibling */
		copy = NULL;
		x = mc->mc_ki[mc->mc_top] - split_indx;
		ksize = mc->mc_db->md_pad;
		split = LEAF2KEY(mp, split_indx, ksize);
		rsize = (nkeys - split_indx) * ksize;
		lsize = (nkeys - split_indx) * sizeof(indx_t);
		mp->mp_lower -= lsize;
		rp->mp_lower += lsize;
		mp->mp_upper += rsize - lsize;
		rp->mp_upper -= rsize - lsize;
		sepkey.mv_size = ksize;
		if (newindx == split_indx) {
			sepkey.mv_data = newkey->mv_data;
		} else {
			sepkey.mv_data = split;
		}
		if (x<0) {
			ins = LEAF2KEY(mp, mc->mc_ki[mc->mc_top], ksize);
			memcpy(rp->mp_ptrs, split, rsize);
			sepkey.mv_data = rp->mp_ptrs;
			memmove(ins+ksize, ins, (split_indx - mc->mc_ki[mc->mc_top]) * ksize);
			memcpy(ins, newkey->mv_data, ksize);
			mp->mp_lower += sizeof(indx_t);
			mp->mp_upper -= ksize - sizeof(indx_t);
		} else {
			if (x)
				memcpy(rp->mp_ptrs, split, x * ksize);
			ins = LEAF2KEY(rp, x, ksize);
			memcpy(ins, newkey->mv_data, ksize);
			memcpy(ins+ksize, split + x * ksize, rsize - x * ksize);
			rp->mp_lower += sizeof(indx_t);
			rp->mp_upper -= ksize - sizeof(indx_t);
			mc->mc_ki[mc->mc_top] = x;
			mc->mc_pg[mc->mc_top] = rp;
		}
		goto newsep;
	}

	/* For leaf pages, check the split point based on what
	 * fits where, since otherwise mdb_node_add can fail.
	 *
	 * This check is only needed when the data items are
	 * relatively large, such that being off by one will
	 * make the difference between success or failure.
	 *
	 * It's also relevant if a page happens to be laid out
	 * such that one half of its nodes are all "small" and
	 * the other half of its nodes are "large." If the new
	 * item is also "large" and falls on the half with
	 * "large" nodes, it also may not fit.
	 */
	if (IS_LEAF(mp)) {
		unsigned int psize, nsize;
		/* Maximum free space in an empty page */
		pmax = mc->mc_txn->mt_env->me_psize - PAGEHDRSZ;
		nsize = mdb_leaf_size(mc->mc_txn->mt_env, newkey, newdata);
		if ((nkeys < 20) || (nsize > pmax/16)) {
			if (newindx <= split_indx) {
				psize = nsize;
				newpos = 0;
				for (i=0; i<split_indx; i++) {
					node = NODEPTR(mp, i);
					psize += NODESIZE + NODEKSZ(node) + sizeof(indx_t);
					if (F_ISSET(node->mn_flags, F_BIGDATA))
						psize += sizeof(pgno_t);
					else
						psize += NODEDSZ(node);
					psize += psize & 1;
					if (psize > pmax) {
						if (i <= newindx) {
							split_indx = newindx;
							if (i < newindx)
								newpos = 1;
						}
						else
							split_indx = i;
						break;
					}
				}
			} else {
				psize = nsize;
				for (i=nkeys-1; i>=split_indx; i--) {
					node = NODEPTR(mp, i);
					psize += NODESIZE + NODEKSZ(node) + sizeof(indx_t);
					if (F_ISSET(node->mn_flags, F_BIGDATA))
						psize += sizeof(pgno_t);
					else
						psize += NODEDSZ(node);
					psize += psize & 1;
					if (psize > pmax) {
						if (i >= newindx) {
							split_indx = newindx;
							newpos = 0;
						} else
							split_indx = i+1;
						break;
					}
				}
			}
		}
	}

	/* First find the separating key between the split pages.
	 * The case where newindx == split_indx is ambiguous; the
	 * new item could go to the new page or stay on the original
	 * page. If newpos == 1 it goes to the new page.
	 */
	if (newindx == split_indx && newpos) {
		sepkey.mv_size = newkey->mv_size;
		sepkey.mv_data = newkey->mv_data;
	} else {
		node = NODEPTR(mp, split_indx);
		sepkey.mv_size = node->mn_ksize;
		sepkey.mv_data = NODEKEY(node);
	}

newsep:
	DPRINTF("separator is [%s]", DKEY(&sepkey));

	/* Copy separator key to the parent.
	 */
	if (SIZELEFT(mn.mc_pg[ptop]) < mdb_branch_size(mc->mc_txn->mt_env, &sepkey)) {
		mn.mc_snum--;
		mn.mc_top--;
		did_split = 1;
		rc = mdb_page_split(&mn, &sepkey, NULL, rp->mp_pgno, 0);

		/* root split? */
		if (mn.mc_snum == mc->mc_snum) {
			mc->mc_pg[mc->mc_snum] = mc->mc_pg[mc->mc_top];
			mc->mc_ki[mc->mc_snum] = mc->mc_ki[mc->mc_top];
			mc->mc_pg[mc->mc_top] = mc->mc_pg[ptop];
			mc->mc_ki[mc->mc_top] = mc->mc_ki[ptop];
			mc->mc_snum++;
			mc->mc_top++;
			ptop++;
		}
		/* Right page might now have changed parent.
		 * Check if left page also changed parent.
		 */
		if (mn.mc_pg[ptop] != mc->mc_pg[ptop] &&
		    mc->mc_ki[ptop] >= NUMKEYS(mc->mc_pg[ptop])) {
			for (i=0; i<ptop; i++) {
				mc->mc_pg[i] = mn.mc_pg[i];
				mc->mc_ki[i] = mn.mc_ki[i];
			}
			mc->mc_pg[ptop] = mn.mc_pg[ptop];
			mc->mc_ki[ptop] = mn.mc_ki[ptop] - 1;
		}
	} else {
		mn.mc_top--;
		rc = mdb_node_add(&mn, mn.mc_ki[ptop], &sepkey, NULL, rp->mp_pgno, 0);
		mn.mc_top++;
	}
	mc->mc_flags ^= C_SPLITTING;
	if (rc != MDB_SUCCESS) {
		return rc;
	}
	if (nflags & MDB_APPEND) {
		mc->mc_pg[mc->mc_top] = rp;
		mc->mc_ki[mc->mc_top] = 0;
		rc = mdb_node_add(mc, 0, newkey, newdata, newpgno, nflags);
		if (rc)
			return rc;
		for (i=0; i<mc->mc_top; i++)
			mc->mc_ki[i] = mn.mc_ki[i];
		goto done;
	}
	if (IS_LEAF2(rp)) {
		goto done;
	}

	/* Move half of the keys to the right sibling. */

	/* grab a page to hold a temporary copy */
	copy = mdb_page_malloc(mc->mc_txn, 1);
	if (copy == NULL)
		return ENOMEM;

	copy->mp_pgno  = mp->mp_pgno;
	copy->mp_flags = mp->mp_flags;
	copy->mp_lower = PAGEHDRSZ;
	copy->mp_upper = mc->mc_txn->mt_env->me_psize;
	mc->mc_pg[mc->mc_top] = copy;
	for (i = j = 0; i <= nkeys; j++) {
		if (i == split_indx) {
		/* Insert in right sibling. */
		/* Reset insert index for right sibling. */
			if (i != newindx || (newpos ^ ins_new)) {
				j = 0;
				mc->mc_pg[mc->mc_top] = rp;
			}
		}

		if (i == newindx && !ins_new) {
			/* Insert the original entry that caused the split. */
			rkey.mv_data = newkey->mv_data;
			rkey.mv_size = newkey->mv_size;
			if (IS_LEAF(mp)) {
				rdata = newdata;
			} else
				pgno = newpgno;
			flags = nflags;

			ins_new = 1;

			/* Update index for the new key. */
			mc->mc_ki[mc->mc_top] = j;
		} else if (i == nkeys) {
			break;
		} else {
			node = NODEPTR(mp, i);
			rkey.mv_data = NODEKEY(node);
			rkey.mv_size = node->mn_ksize;
			if (IS_LEAF(mp)) {
				xdata.mv_data = NODEDATA(node);
				xdata.mv_size = NODEDSZ(node);
				rdata = &xdata;
			} else
				pgno = NODEPGNO(node);
			flags = node->mn_flags;

			i++;
		}

		if (!IS_LEAF(mp) && j == 0) {
			/* First branch index doesn't need key data. */
			rkey.mv_size = 0;
		}

		rc = mdb_node_add(mc, j, &rkey, rdata, pgno, flags);
		if (rc) break;
	}

	nkeys = NUMKEYS(copy);
	for (i=0; i<nkeys; i++)
		mp->mp_ptrs[i] = copy->mp_ptrs[i];
	mp->mp_lower = copy->mp_lower;
	mp->mp_upper = copy->mp_upper;
	memcpy(NODEPTR(mp, nkeys-1), NODEPTR(copy, nkeys-1),
		mc->mc_txn->mt_env->me_psize - copy->mp_upper);

	/* reset back to original page */
	if (newindx < split_indx || (!newpos && newindx == split_indx)) {
		mc->mc_pg[mc->mc_top] = mp;
		if (nflags & MDB_RESERVE) {
			node = NODEPTR(mp, mc->mc_ki[mc->mc_top]);
			if (!(node->mn_flags & F_BIGDATA))
				newdata->mv_data = NODEDATA(node);
		}
	} else {
		mc->mc_ki[ptop]++;
		/* Make sure mc_ki is still valid.
		 */
		if (mn.mc_pg[ptop] != mc->mc_pg[ptop] &&
		    mc->mc_ki[ptop] >= NUMKEYS(mc->mc_pg[ptop])) {
			for (i=0; i<ptop; i++) {
				mc->mc_pg[i] = mn.mc_pg[i];
				mc->mc_ki[i] = mn.mc_ki[i];
			}
			mc->mc_pg[ptop] = mn.mc_pg[ptop];
			mc->mc_ki[ptop] = mn.mc_ki[ptop] - 1;
		}
	}

	/* return tmp page to freelist */
	mdb_page_free(mc->mc_txn->mt_env, copy);
done:
	{
		/* Adjust other cursors pointing to mp */
		MDB_cursor *m2, *m3;
		MDB_dbi dbi = mc->mc_dbi;
		int fixup = NUMKEYS(mp);

		if (mc->mc_flags & C_SUB)
			dbi--;

		for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
			if (mc->mc_flags & C_SUB)
				m3 = &m2->mc_xcursor->mx_cursor;
			else
				m3 = m2;
			if (m3 == mc)
				continue;
			if (!(m2->mc_flags & m3->mc_flags & C_INITIALIZED))
				continue;
			if (m3->mc_flags & C_SPLITTING)
				continue;
			if (new_root) {
				int k;
				/* root split */
				for (k=m3->mc_top; k>=0; k--) {
					m3->mc_ki[k+1] = m3->mc_ki[k];
					m3->mc_pg[k+1] = m3->mc_pg[k];
				}
				if (m3->mc_ki[0] >= split_indx) {
					m3->mc_ki[0] = 1;
				} else {
					m3->mc_ki[0] = 0;
				}
				m3->mc_pg[0] = mc->mc_pg[0];
				m3->mc_snum++;
				m3->mc_top++;
			}
			if (m3->mc_pg[mc->mc_top] == mp) {
				if (m3->mc_ki[mc->mc_top] >= newindx && !(nflags & MDB_SPLIT_REPLACE))
					m3->mc_ki[mc->mc_top]++;
				if (m3->mc_ki[mc->mc_top] >= fixup) {
					m3->mc_pg[mc->mc_top] = rp;
					m3->mc_ki[mc->mc_top] -= fixup;
					m3->mc_ki[ptop] = mn.mc_ki[ptop];
				}
			} else if (!did_split && m3->mc_pg[ptop] == mc->mc_pg[ptop] &&
				m3->mc_ki[ptop] >= mc->mc_ki[ptop]) {
				m3->mc_ki[ptop]++;
			}
		}
	}
	return rc;
}

int
mdb_put(MDB_txn *txn, MDB_dbi dbi,
    MDB_val *key, MDB_val *data, unsigned int flags)
{
	MDB_cursor mc;
	MDB_xcursor mx;

	assert(key != NULL);
	assert(data != NULL);

	if (txn == NULL || !dbi || dbi >= txn->mt_numdbs || !(txn->mt_dbflags[dbi] & DB_VALID))
		return EINVAL;

	if (F_ISSET(txn->mt_flags, MDB_TXN_RDONLY)) {
		return EACCES;
	}

	if (key->mv_size == 0 || key->mv_size > MDB_MAXKEYSIZE) {
		return EINVAL;
	}

	if ((flags & (MDB_NOOVERWRITE|MDB_NODUPDATA|MDB_RESERVE|MDB_APPEND|MDB_APPENDDUP)) != flags)
		return EINVAL;

	mdb_cursor_init(&mc, txn, dbi, &mx);
	return mdb_cursor_put(&mc, key, data, flags);
}

int
mdb_env_set_flags(MDB_env *env, unsigned int flag, int onoff)
{
	if ((flag & CHANGEABLE) != flag)
		return EINVAL;
	if (onoff)
		env->me_flags |= flag;
	else
		env->me_flags &= ~flag;
	return MDB_SUCCESS;
}

int
mdb_env_get_flags(MDB_env *env, unsigned int *arg)
{
	if (!env || !arg)
		return EINVAL;

	*arg = env->me_flags;
	return MDB_SUCCESS;
}

int
mdb_env_get_path(MDB_env *env, const char **arg)
{
	if (!env || !arg)
		return EINVAL;

	*arg = env->me_path;
	return MDB_SUCCESS;
}

/** Common code for #mdb_stat() and #mdb_env_stat().
 * @param[in] env the environment to operate in.
 * @param[in] db the #MDB_db record containing the stats to return.
 * @param[out] arg the address of an #MDB_stat structure to receive the stats.
 * @return 0, this function always succeeds.
 */
static int
mdb_stat0(MDB_env *env, MDB_db *db, MDB_stat *arg)
{
	arg->ms_psize = env->me_psize;
	arg->ms_depth = db->md_depth;
	arg->ms_branch_pages = db->md_branch_pages;
	arg->ms_leaf_pages = db->md_leaf_pages;
	arg->ms_overflow_pages = db->md_overflow_pages;
	arg->ms_entries = db->md_entries;

	return MDB_SUCCESS;
}
int
mdb_env_stat(MDB_env *env, MDB_stat *arg)
{
	int toggle;

	if (env == NULL || arg == NULL)
		return EINVAL;

	toggle = mdb_env_pick_meta(env);

	return mdb_stat0(env, &env->me_metas[toggle]->mm_dbs[MAIN_DBI], arg);
}

int
mdb_env_info(MDB_env *env, MDB_envinfo *arg)
{
	int toggle;

	if (env == NULL || arg == NULL)
		return EINVAL;

	toggle = mdb_env_pick_meta(env);
	arg->me_mapaddr = (env->me_flags & MDB_FIXEDMAP) ? env->me_map : 0;
	arg->me_mapsize = env->me_mapsize;
	arg->me_maxreaders = env->me_maxreaders;
	arg->me_numreaders = env->me_numreaders;
	arg->me_last_pgno = env->me_metas[toggle]->mm_last_pg;
	arg->me_last_txnid = env->me_metas[toggle]->mm_txnid;
	return MDB_SUCCESS;
}

/** Set the default comparison functions for a database.
 * Called immediately after a database is opened to set the defaults.
 * The user can then override them with #mdb_set_compare() or
 * #mdb_set_dupsort().
 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
 * @param[in] dbi A database handle returned by #mdb_dbi_open()
 */
static void
mdb_default_cmp(MDB_txn *txn, MDB_dbi dbi)
{
	uint16_t f = txn->mt_dbs[dbi].md_flags;

	txn->mt_dbxs[dbi].md_cmp =
		(f & MDB_REVERSEKEY) ? mdb_cmp_memnr :
		(f & MDB_INTEGERKEY) ? mdb_cmp_cint  : mdb_cmp_memn;

	txn->mt_dbxs[dbi].md_dcmp =
		!(f & MDB_DUPSORT) ? 0 :
		((f & MDB_INTEGERDUP)
		 ? ((f & MDB_DUPFIXED)   ? mdb_cmp_int   : mdb_cmp_cint)
		 : ((f & MDB_REVERSEDUP) ? mdb_cmp_memnr : mdb_cmp_memn));
}

int mdb_dbi_open(MDB_txn *txn, const char *name, unsigned int flags, MDB_dbi *dbi)
{
	MDB_val key, data;
	MDB_dbi i;
	MDB_cursor mc;
	int rc, dbflag, exact;
	unsigned int unused = 0;
	size_t len;

	if (txn->mt_dbxs[FREE_DBI].md_cmp == NULL) {
		mdb_default_cmp(txn, FREE_DBI);
	}

	if ((flags & VALID_FLAGS) != flags)
		return EINVAL;

	/* main DB? */
	if (!name) {
		*dbi = MAIN_DBI;
		if (flags & PERSISTENT_FLAGS) {
			uint16_t f2 = flags & PERSISTENT_FLAGS;
			/* make sure flag changes get committed */
			if ((txn->mt_dbs[MAIN_DBI].md_flags | f2) != txn->mt_dbs[MAIN_DBI].md_flags) {
				txn->mt_dbs[MAIN_DBI].md_flags |= f2;
				txn->mt_flags |= MDB_TXN_DIRTY;
			}
		}
		mdb_default_cmp(txn, MAIN_DBI);
		return MDB_SUCCESS;
	}

	if (txn->mt_dbxs[MAIN_DBI].md_cmp == NULL) {
		mdb_default_cmp(txn, MAIN_DBI);
	}

	/* Is the DB already open? */
	len = strlen(name);
	for (i=2; i<txn->mt_numdbs; i++) {
		if (!txn->mt_dbxs[i].md_name.mv_size) {
			/* Remember this free slot */
			if (!unused) unused = i;
			continue;
		}
		if (len == txn->mt_dbxs[i].md_name.mv_size &&
			!strncmp(name, txn->mt_dbxs[i].md_name.mv_data, len)) {
			*dbi = i;
			return MDB_SUCCESS;
		}
	}

	/* If no free slot and max hit, fail */
	if (!unused && txn->mt_numdbs >= txn->mt_env->me_maxdbs)
		return MDB_DBS_FULL;

	/* Cannot mix named databases with some mainDB flags */
	if (txn->mt_dbs[MAIN_DBI].md_flags & (MDB_DUPSORT|MDB_INTEGERKEY))
		return (flags & MDB_CREATE) ? MDB_INCOMPATIBLE : MDB_NOTFOUND;

	/* Find the DB info */
	dbflag = DB_NEW|DB_VALID;
	exact = 0;
	key.mv_size = len;
	key.mv_data = (void *)name;
	mdb_cursor_init(&mc, txn, MAIN_DBI, NULL);
	rc = mdb_cursor_set(&mc, &key, &data, MDB_SET, &exact);
	if (rc == MDB_SUCCESS) {
		/* make sure this is actually a DB */
		MDB_node *node = NODEPTR(mc.mc_pg[mc.mc_top], mc.mc_ki[mc.mc_top]);
		if (!(node->mn_flags & F_SUBDATA))
			return EINVAL;
	} else if (rc == MDB_NOTFOUND && (flags & MDB_CREATE)) {
		/* Create if requested */
		MDB_db dummy;
		data.mv_size = sizeof(MDB_db);
		data.mv_data = &dummy;
		memset(&dummy, 0, sizeof(dummy));
		dummy.md_root = P_INVALID;
		dummy.md_flags = flags & PERSISTENT_FLAGS;
		rc = mdb_cursor_put(&mc, &key, &data, F_SUBDATA);
		dbflag |= DB_DIRTY;
	}

	/* OK, got info, add to table */
	if (rc == MDB_SUCCESS) {
		unsigned int slot = unused ? unused : txn->mt_numdbs;
		txn->mt_dbxs[slot].md_name.mv_data = strdup(name);
		txn->mt_dbxs[slot].md_name.mv_size = len;
		txn->mt_dbxs[slot].md_rel = NULL;
		txn->mt_dbflags[slot] = dbflag;
		memcpy(&txn->mt_dbs[slot], data.mv_data, sizeof(MDB_db));
		*dbi = slot;
		txn->mt_env->me_dbflags[slot] = txn->mt_dbs[slot].md_flags;
		mdb_default_cmp(txn, slot);
		if (!unused) {
			txn->mt_numdbs++;
		}
	}

	return rc;
}

int mdb_stat(MDB_txn *txn, MDB_dbi dbi, MDB_stat *arg)
{
	if (txn == NULL || arg == NULL || dbi >= txn->mt_numdbs)
		return EINVAL;

	if (txn->mt_dbflags[dbi] & DB_STALE) {
		MDB_cursor mc;
		MDB_xcursor mx;
		/* Stale, must read the DB's root. cursor_init does it for us. */
		mdb_cursor_init(&mc, txn, dbi, &mx);
	}
	return mdb_stat0(txn->mt_env, &txn->mt_dbs[dbi], arg);
}

void mdb_dbi_close(MDB_env *env, MDB_dbi dbi)
{
	char *ptr;
	if (dbi <= MAIN_DBI || dbi >= env->me_maxdbs)
		return;
	ptr = env->me_dbxs[dbi].md_name.mv_data;
	env->me_dbxs[dbi].md_name.mv_data = NULL;
	env->me_dbxs[dbi].md_name.mv_size = 0;
	env->me_dbflags[dbi] = 0;
	free(ptr);
}

int mdb_dbi_flags(MDB_env *env, MDB_dbi dbi, unsigned int *flags)
{
	/* We could return the flags for the FREE_DBI too but what's the point? */
	if (dbi <= MAIN_DBI || dbi >= env->me_numdbs)
		return EINVAL;
	*flags = env->me_dbflags[dbi];
	return MDB_SUCCESS;
}

/** Add all the DB's pages to the free list.
 * @param[in] mc Cursor on the DB to free.
 * @param[in] subs non-Zero to check for sub-DBs in this DB.
 * @return 0 on success, non-zero on failure.
 */
static int
mdb_drop0(MDB_cursor *mc, int subs)
{
	int rc;

	rc = mdb_page_search(mc, NULL, 0);
	if (rc == MDB_SUCCESS) {
		MDB_txn *txn = mc->mc_txn;
		MDB_node *ni;
		MDB_cursor mx;
		unsigned int i;

		/* LEAF2 pages have no nodes, cannot have sub-DBs */
		if (IS_LEAF2(mc->mc_pg[mc->mc_top]))
			mdb_cursor_pop(mc);

		mdb_cursor_copy(mc, &mx);
		while (mc->mc_snum > 0) {
			MDB_page *mp = mc->mc_pg[mc->mc_top];
			unsigned n = NUMKEYS(mp);
			if (IS_LEAF(mp)) {
				for (i=0; i<n; i++) {
					ni = NODEPTR(mp, i);
					if (ni->mn_flags & F_BIGDATA) {
						MDB_page *omp;
						pgno_t pg;
						memcpy(&pg, NODEDATA(ni), sizeof(pg));
						rc = mdb_page_get(txn, pg, &omp, NULL);
						if (rc != 0)
							return rc;
						assert(IS_OVERFLOW(omp));
						rc = mdb_midl_append_range(&txn->mt_free_pgs,
							pg, omp->mp_pages);
						if (rc)
							return rc;
					} else if (subs && (ni->mn_flags & F_SUBDATA)) {
						mdb_xcursor_init1(mc, ni);
						rc = mdb_drop0(&mc->mc_xcursor->mx_cursor, 0);
						if (rc)
							return rc;
					}
				}
			} else {
				if ((rc = mdb_midl_need(&txn->mt_free_pgs, n)) != 0)
					return rc;
				for (i=0; i<n; i++) {
					pgno_t pg;
					ni = NODEPTR(mp, i);
					pg = NODEPGNO(ni);
					/* free it */
					mdb_midl_xappend(txn->mt_free_pgs, pg);
				}
			}
			if (!mc->mc_top)
				break;
			mc->mc_ki[mc->mc_top] = i;
			rc = mdb_cursor_sibling(mc, 1);
			if (rc) {
				/* no more siblings, go back to beginning
				 * of previous level.
				 */
				mdb_cursor_pop(mc);
				mc->mc_ki[0] = 0;
				for (i=1; i<mc->mc_snum; i++) {
					mc->mc_ki[i] = 0;
					mc->mc_pg[i] = mx.mc_pg[i];
				}
			}
		}
		/* free it */
		rc = mdb_midl_append(&txn->mt_free_pgs, mc->mc_db->md_root);
	} else if (rc == MDB_NOTFOUND) {
		rc = MDB_SUCCESS;
	}
	return rc;
}

int mdb_drop(MDB_txn *txn, MDB_dbi dbi, int del)
{
	MDB_cursor *mc, *m2;
	int rc;

	if (!txn || !dbi || dbi >= txn->mt_numdbs || (unsigned)del > 1 || !(txn->mt_dbflags[dbi] & DB_VALID))
		return EINVAL;

	if (F_ISSET(txn->mt_flags, MDB_TXN_RDONLY))
		return EACCES;

	rc = mdb_cursor_open(txn, dbi, &mc);
	if (rc)
		return rc;

	rc = mdb_drop0(mc, mc->mc_db->md_flags & MDB_DUPSORT);
	/* Invalidate the dropped DB's cursors */
	for (m2 = txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
		m2->mc_flags &= ~(C_INITIALIZED|C_EOF);
	if (rc)
		goto leave;

	/* Can't delete the main DB */
	if (del && dbi > MAIN_DBI) {
		rc = mdb_del(txn, MAIN_DBI, &mc->mc_dbx->md_name, NULL);
		if (!rc) {
			txn->mt_dbflags[dbi] = DB_STALE;
			mdb_dbi_close(txn->mt_env, dbi);
		}
	} else {
		/* reset the DB record, mark it dirty */
		txn->mt_dbflags[dbi] |= DB_DIRTY;
		txn->mt_dbs[dbi].md_depth = 0;
		txn->mt_dbs[dbi].md_branch_pages = 0;
		txn->mt_dbs[dbi].md_leaf_pages = 0;
		txn->mt_dbs[dbi].md_overflow_pages = 0;
		txn->mt_dbs[dbi].md_entries = 0;
		txn->mt_dbs[dbi].md_root = P_INVALID;

		txn->mt_flags |= MDB_TXN_DIRTY;
	}
leave:
	mdb_cursor_close(mc);
	return rc;
}

int mdb_set_compare(MDB_txn *txn, MDB_dbi dbi, MDB_cmp_func *cmp)
{
	if (txn == NULL || !dbi || dbi >= txn->mt_numdbs || !(txn->mt_dbflags[dbi] & DB_VALID))
		return EINVAL;

	txn->mt_dbxs[dbi].md_cmp = cmp;
	return MDB_SUCCESS;
}

int mdb_set_dupsort(MDB_txn *txn, MDB_dbi dbi, MDB_cmp_func *cmp)
{
	if (txn == NULL || !dbi || dbi >= txn->mt_numdbs || !(txn->mt_dbflags[dbi] & DB_VALID))
		return EINVAL;

	txn->mt_dbxs[dbi].md_dcmp = cmp;
	return MDB_SUCCESS;
}

int mdb_set_relfunc(MDB_txn *txn, MDB_dbi dbi, MDB_rel_func *rel)
{
	if (txn == NULL || !dbi || dbi >= txn->mt_numdbs || !(txn->mt_dbflags[dbi] & DB_VALID))
		return EINVAL;

	txn->mt_dbxs[dbi].md_rel = rel;
	return MDB_SUCCESS;
}

int mdb_set_relctx(MDB_txn *txn, MDB_dbi dbi, void *ctx)
{
	if (txn == NULL || !dbi || dbi >= txn->mt_numdbs || !(txn->mt_dbflags[dbi] & DB_VALID))
		return EINVAL;

	txn->mt_dbxs[dbi].md_relctx = ctx;
	return MDB_SUCCESS;
}

int mdb_reader_list(MDB_env *env, MDB_msg_func *func, void *ctx)
{
	unsigned int i, rdrs;
	MDB_reader *mr;
	char buf[64];
	int first = 1;

	if (!env || !func)
		return -1;
	if (!env->me_txns) {
		return func("(no reader locks)\n", ctx);
	}
	rdrs = env->me_txns->mti_numreaders;
	mr = env->me_txns->mti_readers;
	for (i=0; i<rdrs; i++) {
		if (mr[i].mr_pid) {
			size_t tid;
			int rc;
			tid = mr[i].mr_tid;
			if (mr[i].mr_txnid == (txnid_t)-1) {
				sprintf(buf, "%10d %zx -\n", mr[i].mr_pid, tid);
			} else {
				sprintf(buf, "%10d %zx %zu\n", mr[i].mr_pid, tid, mr[i].mr_txnid);
			}
			if (first) {
				first = 0;
				func("    pid     thread     txnid\n", ctx);
			}
			rc = func(buf, ctx);
			if (rc < 0)
				return rc;
		}
	}
	if (first) {
		func("(no active readers)\n", ctx);
	}
	return 0;
}

/* insert pid into list if not already present.
 * return -1 if already present.
 */
static int mdb_pid_insert(pid_t *ids, pid_t pid)
{
	/* binary search of pid in list */
	unsigned base = 0;
	unsigned cursor = 1;
	int val = 0;
	unsigned n = ids[0];

	while( 0 < n ) {
		unsigned pivot = n >> 1;
		cursor = base + pivot + 1;
		val = pid - ids[cursor];

		if( val < 0 ) {
			n = pivot;

		} else if ( val > 0 ) {
			base = cursor;
			n -= pivot + 1;

		} else {
			/* found, so it's a duplicate */
			return -1;
		}
	}
	
	if( val > 0 ) {
		++cursor;
	}
	ids[0]++;
	for (n = ids[0]; n > cursor; n--)
		ids[n] = ids[n-1];
	ids[n] = pid;
	return 0;
}

int mdb_reader_check(MDB_env *env, int *dead)
{
	unsigned int i, j, rdrs;
	MDB_reader *mr;
	pid_t *pids, pid;
	int count = 0;

	if (!env)
		return EINVAL;
	if (dead)
		*dead = 0;
	if (!env->me_txns)
		return MDB_SUCCESS;
	rdrs = env->me_txns->mti_numreaders;
	pids = malloc((rdrs+1) * sizeof(pid_t));
	if (!pids)
		return ENOMEM;
	pids[0] = 0;
	mr = env->me_txns->mti_readers;
	j = 0;
	for (i=0; i<rdrs; i++) {
		if (mr[i].mr_pid && mr[i].mr_pid != env->me_pid) {
			pid = mr[i].mr_pid;
			if (mdb_pid_insert(pids, pid) == 0) {
				if (mdb_reader_pid(env, Pidcheck, pid)) {
					LOCK_MUTEX_R(env);
					if (mdb_reader_pid(env, Pidcheck, pid)) {
						for (j=i; j<rdrs; j++)
							if (mr[j].mr_pid == pid) {
								mr[j].mr_pid = 0;
								count++;
							}
					}
					UNLOCK_MUTEX_R(env);
				}
			}
		}
	}
	free(pids);
	if (dead)
		*dead = count;
	return MDB_SUCCESS;
}
/** @} */
