#ifndef __BITCOIN_UTIL__
#define __BITCOIN_UTIL__

// TODO: these things should all go somewhere

#include <string>
#include "types.h"
#include "uint256.h"
#include <openssl/ripemd.h>
#include <openssl/sha.h>

std::string strprintf(const char* format, ...);
std::string FormatFullVersion();
void RandAddSeedPerfmon();

static const unsigned int MAX_SIZE = 0x02000000;

#define loop                for (;;)
#define PAIR(t1, t2)        pair<t1, t2>

#if !defined(WIN32) && !defined(WIN64)
#define _vsnprintf(a,b,c,d) vsnprintf(a,b,c,d)
#endif


template<typename T1>
inline uint256 SHA256Hash(const T1 pbegin, const T1 pend)
{
	static unsigned char pblank[1];
	uint256 hash1;
	SHA256((pbegin == pend ? pblank : (unsigned char*)&pbegin[0]), (pend - pbegin) * sizeof(pbegin[0]), (unsigned char*)&hash1);
	uint256 hash2;
	SHA256((unsigned char*)&hash1, sizeof(hash1), (unsigned char*)&hash2);
	return hash2;
}

template<typename T1, typename T2>
inline uint256 SHA256Hash(const T1 p1begin, const T1 p1end,
	const T2 p2begin, const T2 p2end)
{
	static unsigned char pblank[1];
	uint256 hash1;
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, (p1begin == p1end ? pblank : (unsigned char*)&p1begin[0]), (p1end - p1begin) * sizeof(p1begin[0]));
	SHA256_Update(&ctx, (p2begin == p2end ? pblank : (unsigned char*)&p2begin[0]), (p2end - p2begin) * sizeof(p2begin[0]));
	SHA256_Final((unsigned char*)&hash1, &ctx);
	uint256 hash2;
	SHA256((unsigned char*)&hash1, sizeof(hash1), (unsigned char*)&hash2);
	return hash2;
}

template<typename T1, typename T2, typename T3>
inline uint256 SHA256Hash(const T1 p1begin, const T1 p1end,
	const T2 p2begin, const T2 p2end,
	const T3 p3begin, const T3 p3end)
{
	static unsigned char pblank[1];
	uint256 hash1;
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, (p1begin == p1end ? pblank : (unsigned char*)&p1begin[0]), (p1end - p1begin) * sizeof(p1begin[0]));
	SHA256_Update(&ctx, (p2begin == p2end ? pblank : (unsigned char*)&p2begin[0]), (p2end - p2begin) * sizeof(p2begin[0]));
	SHA256_Update(&ctx, (p3begin == p3end ? pblank : (unsigned char*)&p3begin[0]), (p3end - p3begin) * sizeof(p3begin[0]));
	SHA256_Final((unsigned char*)&hash1, &ctx);
	uint256 hash2;
	SHA256((unsigned char*)&hash1, sizeof(hash1), (unsigned char*)&hash2);
	return hash2;
}

inline uint160 Hash160(const std::vector<unsigned char>& vch)
{
	uint256 hash1;
	SHA256(&vch[0], vch.size(), (unsigned char*)&hash1);
	uint160 hash2;
	RIPEMD160((unsigned char*)&hash1, sizeof(hash1), (unsigned char*)&hash2);
	return hash2;
}

/*
#ifdef WIN32
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

#endif
