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

#ifndef RIPPLE_CRYPTO_RFC1751_H_INCLUDED
#define RIPPLE_CRYPTO_RFC1751_H_INCLUDED

#include <string>
#include <vector>

namespace ripple {

class RFC1751
{
public:
    static int
    getKeyFromEnglish(std::string& strKey, std::string const& strHuman);

    static void
    getEnglishFromKey(std::string& strHuman, std::string const& strKey);

    /** Chooses a single dictionary word from the data.

        This is not particularly secure but it can be useful to provide
        a unique name for something given a GUID or fixed data. We use
        it to turn the pubkey_node into an easily remembered and identified
        4 character string.
    */
    static std::string
    getWordFromBlob(void const* blob, size_t bytes);

private:
    static unsigned long
    extract(char const* s, int start, int length);
    static void
    btoe(std::string& strHuman, std::string const& strData);
    static void
    insert(char* s, int x, int start, int length);
    static void
    standard(std::string& strWord);
    static int
    wsrch(std::string const& strWord, int iMin, int iMax);
    static int
    etob(std::string& strData, std::vector<std::string> vsHuman);

    static char const* s_dictionary[];
};

}  // namespace ripple

#endif
