//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpl/crypto/RFC1751.h>
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/copied.hpp>
#include <cassert>
#include <cstdint>
#include <string>
#include <stdexcept> // Include for std::invalid_argument

namespace ripple {

char const* RFC1751::s_dictionary[2048] = {
    "A", "ABE", "ACE", "ACT", "AD", "ADA", "ADD", "AGO", "AID",
    // ... (rest of the dictionary remains unchanged)
};

unsigned long RFC1751::extract(char const* s, int start, int length) {
    unsigned char cl;
    unsigned char cc;
    unsigned char cr;
    unsigned long x;

    assert(length <= 11);
    assert(start >= 0);
    assert(length >= 0);
    assert(start + length <= 66);

    int const shiftR = 24 - (length + (start % 8));
    cl = s[start / 8];
    cc = (shiftR < 16) ? s[start / 8 + 1] : 0;
    cr = (shiftR < 8) ? s[start / 8 + 2] : 0;

    x = ((long)(cl << 8 | cc) << 8 | cr);
    x = x >> shiftR;
    x = (x & (0xffff >> (16 - length)));

    return x;
}

void RFC1751::btoe(std::string& strHuman, std::string const& strData) {
    if (strData.size() < 8) {
        throw std::invalid_argument("strData must be at least 8 bytes long");
    }

    char caBuffer[9];
    int p, i;

    memcpy(caBuffer, strData.c_str(), 8);

    for (p = 0, i = 0; i < 64; i += 2) {
        p += extract(caBuffer, i, 2);
    }

    caBuffer[8] = char(p) << 6;

    strHuman = std::string() + s_dictionary[extract(caBuffer, 0, 11)] + " " +
               s_dictionary[extract(caBuffer, 11, 11)] + " " +
               s_dictionary[extract(caBuffer, 22, 11)] + " " +
               s_dictionary[extract(caBuffer, 33, 11)] + " " +
               s_dictionary[extract(caBuffer, 44, 11)] + " " +
               s_dictionary[extract(caBuffer, 55, 11)];
}

void RFC1751::insert(char* s, int x, int start, int length) {
    unsigned char cl;
    unsigned char cc;
    unsigned char cr;
    unsigned long y;
    int shift;

    assert(length <= 11);
    assert(start >= 0);
    assert(length >= 0);
    assert(start + length <= 66);

    shift = ((8 - ((start + length) % 8)) % 8);
    y = (long)x << shift;
    cl = (y >> 16) & 0xff;
    cc = (y >> 8) & 0xff;
    cr = y & 0xff;

    if (shift + length > 16) {
        s[start / 8] |= cl;
        s[start / 8 + 1] |= cc;
        s[start / 8 + 2] |= cr;
    } else if (shift + length > 8) {
        s[start / 8] |= cc;
        s[start / 8 + 1] |= cr;
    } else {
        s[start / 8] |=
 cr;
    }
}

void RFC1751::standard(std::string& strWord) {
    for (auto& letter : strWord) {
        if (islower(static_cast<unsigned char>(letter)))
            letter = toupper(static_cast<unsigned char>(letter));
        else if (letter == '1')
            letter = 'L';
        else if (letter == '0')
            letter = 'O';
        else if (letter == '5')
            letter = 'S';
    }
}

int RFC1751::wsrch(std::string const& strWord, int iMin, int iMax) {
    int iResult = -1;

    while (iResult < 0 && iMin != iMax) {
        int iMid = iMin + (iMax - iMin) / 2;
        int iDir = strWord.compare(s_dictionary[iMid]);

        if (!iDir) {
            iResult = iMid;
        } else if (iDir < 0) {
            iMax = iMid;
        } else {
            iMin = iMid + 1;
        }
    }

    return iResult;
}

int RFC1751::etob(std::string& strData, std::vector<std::string> vsHuman) {
    if (6 != vsHuman.size())
        return -1;

    int i, p = 0;
    char b[9] = {0};

    for (auto& strWord : vsHuman) {
        int l = strWord.length();

        if (l > 4 || l < 1)
            return -1;

        standard(strWord);

        auto v = wsrch(strWord, l < 4 ? 0 : 571, l < 4 ? 570 : 2048);

        if (v < 0)
            return 0;

        insert(b, v, p, 11);
        p += 11;
    }

    for (p = 0, i = 0; i < 64; i += 2)
        p += extract(b, i, 2);

    if ((p & 3) != extract(b, 64, 2))
        return -2;

    strData.assign(b, 8);

    return 1;
}

int RFC1751::getKeyFromEnglish(std::string& strKey, std::string const& strHuman) {
    std::vector<std::string> vWords;
    std::string strFirst, strSecond;
    int rc = 0;

    std::string strTrimmed(strHuman);

    boost::algorithm::trim(strTrimmed);

    boost::algorithm::split(
        vWords,
        strTrimmed,
        boost::algorithm::is_space(),
        boost::algorithm::token_compress_on);

    rc = 12 == vWords.size() ? 1 : -1;

    if (1 == rc)
        rc = etob(strFirst, vWords | boost::adaptors::copied(0, 6));

    if (1 == rc)
        rc = etob(strSecond, vWords | boost::adaptors::copied(6, 12));

    if (1 == rc)
        strKey = strFirst + strSecond;

    return rc;
}

void RFC1751::getEnglishFromKey(std::string& strHuman, std::string const& strKey) {
    std::string strFirst, strSecond;

    btoe(strFirst, strKey.substr(0, 8));
    btoe(strSecond, strKey.substr(8, 8));

    strHuman = strFirst + " " + strSecond;
}

std::string RFC1751::getWordFromBlob(void const* blob, size_t bytes) {
    unsigned char const* data = static_cast<unsigned char const*>(blob);
    std::uint32_t hash = 0;for (size_t i = 0; i < bytes; ++i) {
        hash += data[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }

    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);

    return s_dictionary[hash % (sizeof(s_dictionary) / sizeof(s_dictionary[0]))];
}

} // namespace ripple
