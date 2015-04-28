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
#include <ripple/crypto/GenerateDeterministicKey.h>
#include <ripple/basics/base_uint.h>
#include <beast/unit_test/suite.h>

namespace ripple {

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.


class CKey_test : public beast::unit_test::suite
{
public:
    void
    run ()
    {
        uint128 seed1, seed2;
        seed1.SetHex ("71ED064155FFADFA38782C5E0158CB26");
        seed2.SetHex ("CF0C3BE4485961858C4198515AE5B965");

        uint256 const priv1 = generateRootDeterministicPrivateKey (seed1);
        uint256 const priv2 = generateRootDeterministicPrivateKey (seed2);

        unexpected (to_string (priv1) != "7CFBA64F771E93E817E15039215430B53F74"
                    "01C34931D111EAB3510B22DBB0D8",
                    "Incorrect private key for generator");

        unexpected (to_string (priv2) != "98BC2EACB26EB021D1A6293C044D88BA2F0B"
                    "6729A2772DEEBF2E21A263C1740B",
                    "Incorrect private key for generator");
    }
};

BEAST_DEFINE_TESTSUITE(CKey,ripple_data,ripple);

} // ripple
