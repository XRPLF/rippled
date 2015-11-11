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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/crypto/RandomNumbers.h>
#include <openssl/rand.h>
#include <cassert>
#include <random>
#include <stdexcept>

namespace ripple {

bool stir_entropy (std::string file)
{
    // First, we attempt to stir any existing saved entropy
    // into the pool: no use letting it go to waste.
    RAND_load_file (file.c_str (), 1024);

    // And now, we extract some entropy out, and save it for
    // the future. If the quality of the entropy isn't great
    // then we let the user know.
    return RAND_write_file (file.c_str ()) != -1;
}

void add_entropy (void* buffer, int count)
{
    assert (buffer == nullptr || count != 0);

    // If we are passed data in we use it but conservatively estimate that it
    // contains only around 2 bits of entropy per byte.
    if (buffer != nullptr && count != 0)
        RAND_add (buffer, count, count / 4.0);

    // And try to add some entropy from the system
    unsigned int rdbuf[32];

    std::random_device rd;

    for (auto& x : rdbuf)
        x = rd ();

    // In all our supported platforms, std::random_device is non-deterministic
    // but we conservatively estimate it has around 4 bits of entropy per byte.
    RAND_add (rdbuf, sizeof (rdbuf), sizeof (rdbuf) / 2.0);
}

void random_fill (void* buffer, int count)
{
    assert (count > 0);

    if (RAND_bytes (reinterpret_cast <unsigned char*> (buffer), count) != 1)
        Throw<std::runtime_error> ("Insufficient entropy in pool.");
}

}
