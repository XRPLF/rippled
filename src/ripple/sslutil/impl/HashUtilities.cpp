//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {

uint160 Hash160 (Blob const& vch)
{
    uint256 hash1;
    SHA256 (&vch[0], vch.size (), (unsigned char*)&hash1);
    uint160 hash2;
    RIPEMD160 ((unsigned char*)&hash1, sizeof (hash1), (unsigned char*)&hash2);
    return hash2;
}

/*
#if BEAST_WIN32
// This is used to attempt to keep keying material out of swap
// Note that VirtualLock does not provide this as a guarantee on Windows,
// but, in practice, memory that has been VirtualLock'd almost never gets written to
// the pagefile except in rare circumstances where memory is extremely low.
#include <windows.h>
#define mlock(p, n) VirtualLock((p), (n));
#define munlock(p, n) VirtualUnlock((p), (n));
#else
#include <sys/mman.h>
#include <limits.h>
// This comes from limits.h if it's not defined there set a sane default
#ifndef PAGESIZE
#include <unistd.h>
#define PAGESIZE sysconf(_SC_PAGESIZE)
#endif
#define mlock(a,b) \
    mlock(((void *)(((size_t)(a)) & (~((PAGESIZE)-1)))),\
    (((((size_t)(a)) + (b) - 1) | ((PAGESIZE) - 1)) + 1) - (((size_t)(a)) & (~((PAGESIZE) - 1))))
#define munlock(a,b) \
    munlock(((void *)(((size_t)(a)) & (~((PAGESIZE)-1)))),\
    (((((size_t)(a)) + (b) - 1) | ((PAGESIZE) - 1)) + 1) - (((size_t)(a)) & (~((PAGESIZE) - 1))))
#endif
*/

}
