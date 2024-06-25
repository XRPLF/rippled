//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_CRYPTO_SECURE_ERASE_H_INCLUDED
#define RIPPLE_CRYPTO_SECURE_ERASE_H_INCLUDED

#include <cstddef>

namespace ripple {

/** Attempts to clear the given blob of memory.

    The underlying implementation of this function takes pains to
    attempt to outsmart the compiler from optimizing the clearing
    away. Please note that, despite that, remnants of content may
    remain floating around in memory as well as registers, caches
    and more.

    For a more in-depth discussion of the subject please see the
    below posts by Colin Percival:

    http://www.daemonology.net/blog/2014-09-04-how-to-zero-a-buffer.html
    http://www.daemonology.net/blog/2014-09-06-zeroing-buffers-is-insufficient.html
*/
void
secure_erase(void* dest, std::size_t bytes);

}  // namespace ripple

#endif
