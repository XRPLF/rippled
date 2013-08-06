# Makefile for liblmdb (Lightning memory-mapped database library).

########################################################################
# Configuration. The compiler options must enable threaded compilation.
#
# Preprocessor macros (for CPPFLAGS) of interest:
#
# To compile successfully if the default does not:
# - MDB_USE_POSIX_SEM	(enabled by default on BSD, Apple)
#	Define if shared mutexes are unsupported.  Note that Posix
#	semaphores and shared mutexes have different behaviors and
#	different problems, see the Caveats section in lmdb.h.
#
# For best performence or to compile successfully:
# - MDB_DSYNC = "O_DSYNC" (default) or "O_SYNC" (less efficient)
#	If O_DSYNC is undefined but exists in /usr/include,
#	preferably set some compiler flag to get the definition.
# - MDB_FDATASYNC = "fdatasync" or "fsync"
#	Function for flushing the data of a file. Define this to
#	"fsync" if fdatasync() is not supported. fdatasync is
#	default except on BSD, Apple, Android which use fsync.
# - MDB_USE_PWRITEV
#	Define if the pwritev() function is supported.
#
# Data format:
# - MDB_MAXKEYSIZE
#	Controls data packing and limits, see mdb.c.
#
# Debugging:
# - MDB_DEBUG, MDB_PARANOID.
#
CC	= gcc
W	= -W -Wall -Wno-unused-parameter -Wbad-function-cast
OPT = -O2 -g
CFLAGS	= -pthread $(OPT) $(W) $(XCFLAGS)
LDLIBS	=
SOLIBS	=
prefix	= /usr/local

########################################################################

IHDRS	= lmdb.h
ILIBS	= liblmdb.a liblmdb.so
IPROGS	= mdb_stat mdb_copy
IDOCS	= mdb_stat.1 mdb_copy.1
PROGS	= $(IPROGS) mtest mtest2 mtest3 mtest4 mtest5
all:	$(ILIBS) $(PROGS)

install: $(ILIBS) $(IPROGS) $(IHDRS)
	cp $(IPROGS) $(DESTDIR)$(prefix)/bin
	cp $(ILIBS) $(DESTDIR)$(prefix)/lib
	cp $(IHDRS) $(DESTDIR)$(prefix)/include
	cp $(IDOCS) $(DESTDIR)$(prefix)/man/man1

clean:
	rm -rf $(PROGS) *.[ao] *.so *~ testdb

test:	all
	mkdir testdb
	./mtest && ./mdb_stat testdb

liblmdb.a:	mdb.o midl.o
	ar rs $@ mdb.o midl.o

liblmdb.so:	mdb.o midl.o
	$(CC) $(LDFLAGS) -pthread -shared -o $@ mdb.o midl.o $(SOLIBS)

mdb_stat: mdb_stat.o liblmdb.a
mdb_copy: mdb_copy.o liblmdb.a
mtest:    mtest.o    liblmdb.a
mtest2:	mtest2.o liblmdb.a
mtest3:	mtest3.o liblmdb.a
mtest4:	mtest4.o liblmdb.a
mtest5:	mtest5.o liblmdb.a
mtest6:	mtest6.o liblmdb.a

mdb.o: mdb.c lmdb.h midl.h
	$(CC) $(CFLAGS) -fPIC $(CPPFLAGS) -c mdb.c

midl.o: midl.c midl.h
	$(CC) $(CFLAGS) -fPIC $(CPPFLAGS) -c midl.c

%:	%.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o:	%.c lmdb.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<
