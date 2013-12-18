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

#include <set>
#include <unordered_set>
#include <boost/unordered_set.hpp>

#if BEAST_MSVC
# define STL_SET_HAS_EMPLACE 1
#else
# define STL_SET_HAS_EMPLACE 0
#endif

namespace ripple {

class RippleAssetTests : public UnitTest
{
public:
    template <class Set>
    void testAssetSet ()
    {
        RippleCurrency const c1 (1);
        RippleIssuer   const i1 (1);
        RippleCurrency const c2 (2);
        RippleIssuer   const i2 (2);
        RippleAssetRef const a1 (c1, i1);
        RippleAssetRef const a2 (c2, i2);

        Set c;

        c.insert (a1);
        c.insert (a2);
        expect (c.erase (RippleAsset (c1, i2)) == 0);
        expect (c.erase (RippleAsset (c1, i1)) == 1);
        expect (c.erase (RippleAsset (c2, i2)) == 1);
        expect (c.empty ());

        c.insert (a1);
        c.insert (a2);
        expect (c.erase (RippleAssetRef (c1, i2)) == 0);
        expect (c.erase (RippleAssetRef (c1, i1)) == 1);
        expect (c.erase (RippleAssetRef (c2, i2)) == 1);
        expect (c.empty ());

#if STL_SET_HAS_EMPLACE
        c.emplace (c1, i1);
        c.emplace (c2, i2);
        expect (c.size() == 2);
#endif
    }

    template <class Map>
    void testAssetMap ()
    {
        RippleCurrency const c1 (1);
        RippleIssuer   const i1 (1);
        RippleCurrency const c2 (2);
        RippleIssuer   const i2 (2);
        RippleAssetRef const a1 (c1, i1);
        RippleAssetRef const a2 (c2, i2);

        Map c;

        c.insert (std::make_pair (a1, 1));
        c.insert (std::make_pair (a2, 2));
        expect (c.erase (RippleAsset (c1, i2)) == 0);
        expect (c.erase (RippleAsset (c1, i1)) == 1);
        expect (c.erase (RippleAsset (c2, i2)) == 1);
        expect (c.empty ());

        c.insert (std::make_pair (a1, 1));
        c.insert (std::make_pair (a2, 2));
        expect (c.erase (RippleAssetRef (c1, i2)) == 0);
        expect (c.erase (RippleAssetRef (c1, i1)) == 1);
        expect (c.erase (RippleAssetRef (c2, i2)) == 1);
        expect (c.empty ());
    }

    void testAssets ()
    {
        beginTestCase ("assets");

        RippleCurrency const c1 (1);
        RippleIssuer   const i1 (1);
        RippleCurrency const c2 (2);
        RippleIssuer   const i2 (2);

        {
            RippleAssetRef a0 (xrp_asset ());
            expect (a0 == xrp_asset());

            RippleAssetRef a1 (c1, i1);
            RippleAssetRef a2 (a1);

            expect (a1 == a2);
        }

        {
            // VFALCO NOTE this should be uninitialized
            RippleAsset uninitialized_asset;
        }

        {
            RippleAsset a1 (c1, i1);
            RippleAsset a2 (a1);

            expect (a1 == a2);
            expect (a1 != xrp_asset());

            a2 = RippleAsset (c2, i2);

            expect (a1 < a2);
        }

        testAssetSet <std::set <RippleAsset>> ();
        testAssetSet <std::set <RippleAssetRef>> ();
        testAssetSet <std::unordered_set <RippleAsset>> ();
        testAssetSet <std::unordered_set <RippleAssetRef>> ();
        testAssetSet <boost::unordered_set <RippleAsset>> ();
        testAssetSet <boost::unordered_set <RippleAssetRef>> ();

        testAssetMap <std::map <RippleAsset, int>> ();
        testAssetMap <std::map <RippleAssetRef, int>> ();
        testAssetMap <std::unordered_map <RippleAsset, int>> ();
        testAssetMap <std::unordered_map <RippleAssetRef, int>> ();
        testAssetMap <boost::unordered_map <RippleAsset, int>> ();
        testAssetMap <boost::unordered_map <RippleAssetRef, int>> ();
    }

    template <class Set>
    void testBookSet ()
    {
        RippleCurrency const c1 (1);
        RippleIssuer   const i1 (1);
        RippleCurrency const c2 (2);
        RippleIssuer   const i2 (2);
        RippleAssetRef const a1 (c1, i1);
        RippleAssetRef const a2 (c2, i2);
        RippleBookRef  const b1 (a1, a2);
        RippleBookRef  const b2 (a2, a1);

        Set c;

        c.insert (b1);
        c.insert (b2);
        expect (c.erase (RippleBook (a1, a1)) == 0);
        expect (c.erase (RippleBook (a1, a2)) == 1);
        expect (c.erase (RippleBook (a2, a1)) == 1);
        expect (c.empty ());

        c.insert (b1);
        c.insert (b2);
        expect (c.erase (RippleBookRef (a1, a1)) == 0);
        expect (c.erase (RippleBookRef (a1, a2)) == 1);
        expect (c.erase (RippleBookRef (a2, a1)) == 1);
        expect (c.empty ());

#if STL_SET_HAS_EMPLACE
        c.emplace (a1, a2);
        c.emplace (a2, a1);
        expect (c.size() == 2);
#endif
    }

    template <class Map>
    void testBookMap ()
    {
        RippleCurrency const c1 (1);
        RippleIssuer   const i1 (1);
        RippleCurrency const c2 (2);
        RippleIssuer   const i2 (2);
        RippleAssetRef const a1 (c1, i1);
        RippleAssetRef const a2 (c2, i2);
        RippleBookRef  const b1 (a1, a2);
        RippleBookRef  const b2 (a2, a1);

        Map c;

        c.insert (std::make_pair (b1, 1));
        c.insert (std::make_pair (b2, 2));
        expect (c.erase (RippleBook (a1, a1)) == 0);
        expect (c.erase (RippleBook (a1, a2)) == 1);
        expect (c.erase (RippleBook (a2, a1)) == 1);
        expect (c.empty ());

        c.insert (std::make_pair (b1, 1));
        c.insert (std::make_pair (b2, 2));
        expect (c.erase (RippleBookRef (a1, a1)) == 0);
        expect (c.erase (RippleBookRef (a1, a2)) == 1);
        expect (c.erase (RippleBookRef (a2, a1)) == 1);
        expect (c.empty ());
    }

    void testBooks ()
    {
        beginTestCase ("books");

        RippleAsset a1 (RippleCurrency (1), RippleIssuer (1));
        RippleAsset a2 (RippleCurrency (2), RippleIssuer (2));

        RippleBook b1 (a1, a2);
        RippleBook b2 (a2, a1);

        expect (b1 != b2);
        expect (b1 < b2);

        testBookSet <std::set <RippleBook>> ();
        testBookSet <std::set <RippleBookRef>> ();
        testBookSet <std::unordered_set <RippleBook>> ();
        testBookSet <std::unordered_set <RippleBookRef>> ();
        testBookSet <boost::unordered_set <RippleBook>> ();
        testBookSet <boost::unordered_set <RippleBookRef>> ();

        testBookMap <std::map <RippleBook, int>> ();
        testBookMap <std::map <RippleBookRef, int>> ();
        testBookMap <std::unordered_map <RippleBook, int>> ();
        testBookMap <std::unordered_map <RippleBookRef, int>> ();
        testBookMap <boost::unordered_map <RippleBook, int>> ();
        testBookMap <boost::unordered_map <RippleBookRef, int>> ();
    }

    void runTest ()
    {
        testAssets ();
        testBooks ();
    }

    RippleAssetTests () : UnitTest ("RippleAsset", "ripple")
    {
    }
};

static RippleAssetTests rippleAssetTests;

}
