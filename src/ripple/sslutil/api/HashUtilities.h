//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SSLUTIL_HASHUTILITIES_H_INCLUDED
#define RIPPLE_SSLUTIL_HASHUTILITIES_H_INCLUDED

namespace ripple {

// VFALCO NOTE these came from BitcoinUtil.h

// VFALCO TODO Rewrite the callers so we don't need templates,
//         then define these in a .cpp so they are no longer inline.
//
template<typename T1>
uint256 SHA256Hash (const T1 pbegin, const T1 pend)
{
    static unsigned char pblank[1];
    uint256 hash1;
    SHA256 ((pbegin == pend ? pblank : (unsigned char*)&pbegin[0]), (pend - pbegin) * sizeof (pbegin[0]), (unsigned char*)&hash1);
    uint256 hash2;
    SHA256 ((unsigned char*)&hash1, sizeof (hash1), (unsigned char*)&hash2);
    return hash2;
}

template<typename T1, typename T2>
uint256 SHA256Hash (const T1 p1begin, const T1 p1end,
                           const T2 p2begin, const T2 p2end)
{
    static unsigned char pblank[1];
    uint256 hash1;
    SHA256_CTX ctx;
    SHA256_Init (&ctx);
    SHA256_Update (&ctx, (p1begin == p1end ? pblank : (unsigned char*)&p1begin[0]), (p1end - p1begin) * sizeof (p1begin[0]));
    SHA256_Update (&ctx, (p2begin == p2end ? pblank : (unsigned char*)&p2begin[0]), (p2end - p2begin) * sizeof (p2begin[0]));
    SHA256_Final ((unsigned char*)&hash1, &ctx);
    uint256 hash2;
    SHA256 ((unsigned char*)&hash1, sizeof (hash1), (unsigned char*)&hash2);
    return hash2;
}

template<typename T1, typename T2, typename T3>
uint256 SHA256Hash (const T1 p1begin, const T1 p1end,
                    const T2 p2begin, const T2 p2end,
                    const T3 p3begin, const T3 p3end)
{
    static unsigned char pblank[1];
    uint256 hash1;
    SHA256_CTX ctx;
    SHA256_Init (&ctx);
    SHA256_Update (&ctx, (p1begin == p1end ? pblank : (unsigned char*)&p1begin[0]), (p1end - p1begin) * sizeof (p1begin[0]));
    SHA256_Update (&ctx, (p2begin == p2end ? pblank : (unsigned char*)&p2begin[0]), (p2end - p2begin) * sizeof (p2begin[0]));
    SHA256_Update (&ctx, (p3begin == p3end ? pblank : (unsigned char*)&p3begin[0]), (p3end - p3begin) * sizeof (p3begin[0]));
    SHA256_Final ((unsigned char*)&hash1, &ctx);
    uint256 hash2;
    SHA256 ((unsigned char*)&hash1, sizeof (hash1), (unsigned char*)&hash2);
    return hash2;
}

uint160 Hash160 (Blob const& vch);

}

#endif
