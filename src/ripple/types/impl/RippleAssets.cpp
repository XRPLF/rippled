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
#include <typeinfo>
#include <unordered_set>
#include <boost/unordered_set.hpp>

#if BEAST_MSVC
# define STL_SET_HAS_EMPLACE 1
#else
# define STL_SET_HAS_EMPLACE 0
#endif

#ifndef RIPPLE_ASSETS_ENABLE_STD_HASH
# if BEAST_MAC || BEAST_IOS
#  define RIPPLE_ASSETS_ENABLE_STD_HASH 0
# else
#  define RIPPLE_ASSETS_ENABLE_STD_HASH 1
# endif
#endif



namespace ripple {

class RippleAssetTests : public UnitTest
{
public:
    // Comparison, hash tests for uint60 (via base_uint)
    template <typename Unsigned>
    void testUnsigned ()
    {
        Unsigned const u1 (1);
        Unsigned const u2 (2);
        Unsigned const u3 (3);

        expect (u1 != u2);
        expect (u1 <  u2);
        expect (u1 <= u2);
        expect (u2 <= u2);
        expect (u2 == u2);
        expect (u2 >= u2);
        expect (u3 >= u2);
        expect (u3 >  u2);

        std::hash <Unsigned> hash;

        expect (hash (u1) == hash (u1));
        expect (hash (u2) == hash (u2));
        expect (hash (u3) == hash (u3));
        expect (hash (u1) != hash (u2));
        expect (hash (u1) != hash (u3));
        expect (hash (u2) != hash (u3));
    }

    //--------------------------------------------------------------------------

    // Comparison, hash tests for RippleAssetType
    template <class Asset>
    void testAssetType ()
    {
        RippleCurrency const c1 (1); RippleIssuer const i1 (1);
        RippleCurrency const c2 (2); RippleIssuer const i2 (2);
        RippleCurrency const c3 (3); RippleIssuer const i3 (3);

        expect (Asset (c1, i1) != Asset (c2, i1));
        expect (Asset (c1, i1) <  Asset (c2, i1));
        expect (Asset (c1, i1) <= Asset (c2, i1));
        expect (Asset (c2, i1) <= Asset (c2, i1));
        expect (Asset (c2, i1) == Asset (c2, i1));
        expect (Asset (c2, i1) >= Asset (c2, i1));
        expect (Asset (c3, i1) >= Asset (c2, i1));
        expect (Asset (c3, i1) >  Asset (c2, i1));
        expect (Asset (c1, i1) != Asset (c1, i2));
        expect (Asset (c1, i1) <  Asset (c1, i2));
        expect (Asset (c1, i1) <= Asset (c1, i2));
        expect (Asset (c1, i2) <= Asset (c1, i2));
        expect (Asset (c1, i2) == Asset (c1, i2));
        expect (Asset (c1, i2) >= Asset (c1, i2));
        expect (Asset (c1, i3) >= Asset (c1, i2));
        expect (Asset (c1, i3) >  Asset (c1, i2));

        std::hash <Asset> hash;

        expect (hash (Asset (c1, i1)) == hash (Asset (c1, i1)));
        expect (hash (Asset (c1, i2)) == hash (Asset (c1, i2)));
        expect (hash (Asset (c1, i3)) == hash (Asset (c1, i3)));
        expect (hash (Asset (c2, i1)) == hash (Asset (c2, i1)));
        expect (hash (Asset (c2, i2)) == hash (Asset (c2, i2)));
        expect (hash (Asset (c2, i3)) == hash (Asset (c2, i3)));
        expect (hash (Asset (c3, i1)) == hash (Asset (c3, i1)));
        expect (hash (Asset (c3, i2)) == hash (Asset (c3, i2)));
        expect (hash (Asset (c3, i3)) == hash (Asset (c3, i3)));
        expect (hash (Asset (c1, i1)) != hash (Asset (c1, i2)));
        expect (hash (Asset (c1, i1)) != hash (Asset (c1, i3)));
        expect (hash (Asset (c1, i1)) != hash (Asset (c2, i1)));
        expect (hash (Asset (c1, i1)) != hash (Asset (c2, i2)));
        expect (hash (Asset (c1, i1)) != hash (Asset (c2, i3)));
        expect (hash (Asset (c1, i1)) != hash (Asset (c3, i1)));
        expect (hash (Asset (c1, i1)) != hash (Asset (c3, i2)));
        expect (hash (Asset (c1, i1)) != hash (Asset (c3, i3)));
    }

    template <class Set>
    void testAssetSet ()
    {
        RippleCurrency const c1 (1);
        RippleIssuer   const i1 (1);
        RippleCurrency const c2 (2);
        RippleIssuer   const i2 (2);
        RippleAssetRef const a1 (c1, i1);
        RippleAssetRef const a2 (c2, i2);

        {
            Set c;

            c.insert (a1);
            if (! expect (c.size () == 1)) return;
            c.insert (a2);
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (RippleAsset (c1, i2)) == 0)) return;
            if (! expect (c.erase (RippleAsset (c1, i1)) == 1)) return;
            if (! expect (c.erase (RippleAsset (c2, i2)) == 1)) return;
            if (! expect (c.empty ())) return;
        }

        {
            Set c;

            c.insert (a1);
            if (! expect (c.size () == 1)) return;
            c.insert (a2);
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (RippleAssetRef (c1, i2)) == 0)) return;
            if (! expect (c.erase (RippleAssetRef (c1, i1)) == 1)) return;
            if (! expect (c.erase (RippleAssetRef (c2, i2)) == 1)) return;
            if (! expect (c.empty ())) return;

    #if STL_SET_HAS_EMPLACE
            c.emplace (c1, i1);
            if (! expect (c.size() == 1)) return;
            c.emplace (c2, i2);
            if (! expect (c.size() == 2)) return;
    #endif
        }
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

        {
            Map c;

            c.insert (std::make_pair (a1, 1));
            if (! expect (c.size () == 1)) return;
            c.insert (std::make_pair (a2, 2));
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (RippleAsset (c1, i2)) == 0)) return;
            if (! expect (c.erase (RippleAsset (c1, i1)) == 1)) return;
            if (! expect (c.erase (RippleAsset (c2, i2)) == 1)) return;
            if (! expect (c.empty ())) return;
        }

        {
            Map c;
        
            c.insert (std::make_pair (a1, 1));
            if (! expect (c.size () == 1)) return;
            c.insert (std::make_pair (a2, 2));
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (RippleAssetRef (c1, i2)) == 0)) return;
            if (! expect (c.erase (RippleAssetRef (c1, i1)) == 1)) return;
            if (! expect (c.erase (RippleAssetRef (c2, i2)) == 1)) return;
            if (! expect (c.empty ())) return;
        }
    }

    void testAssetSets ()
    {
        beginTestCase ("std::set <RippleAsset>");
        testAssetSet <std::set <RippleAsset>> ();

        beginTestCase ("std::set <RippleAssetRef>");
        testAssetSet <std::set <RippleAssetRef>> ();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        beginTestCase ("std::unordered_set <RippleAsset>");
        testAssetSet <std::unordered_set <RippleAsset>> ();

        beginTestCase ("std::unordered_set <RippleAssetRef>");
        testAssetSet <std::unordered_set <RippleAssetRef>> ();
#endif

        beginTestCase ("boost::unordered_set <RippleAsset>");
        testAssetSet <boost::unordered_set <RippleAsset>> ();

        beginTestCase ("boost::unordered_set <RippleAssetRef>");
        testAssetSet <boost::unordered_set <RippleAssetRef>> ();
    }

    void testAssetMaps ()
    {
        beginTestCase ("std::map <RippleAsset, int>");
        testAssetMap <std::map <RippleAsset, int>> ();

        beginTestCase ("std::map <RippleAssetRef, int>");
        testAssetMap <std::map <RippleAssetRef, int>> ();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        beginTestCase ("std::unordered_map <RippleAsset, int>");
        testAssetMap <std::unordered_map <RippleAsset, int>> ();

        beginTestCase ("std::unordered_map <RippleAssetRef, int>");
        testAssetMap <std::unordered_map <RippleAssetRef, int>> ();

        beginTestCase ("boost::unordered_map <RippleAsset, int>");
        testAssetMap <boost::unordered_map <RippleAsset, int>> ();

        beginTestCase ("boost::unordered_map <RippleAssetRef, int>");
        testAssetMap <boost::unordered_map <RippleAssetRef, int>> ();

#endif        
    }

    //--------------------------------------------------------------------------

    // Comparison, hash tests for RippleBookType
    template <class Book>
    void testBook ()
    {
        RippleCurrency const c1 (1); RippleIssuer const i1 (1);
        RippleCurrency const c2 (2); RippleIssuer const i2 (2);
        RippleCurrency const c3 (3); RippleIssuer const i3 (3);

        RippleAsset a1 (c1, i1);
        RippleAsset a2 (c1, i2);
        RippleAsset a3 (c2, i2);
        RippleAsset a4 (c3, i2);

        expect (Book (a1, a2) != Book (a2, a3));
        expect (Book (a1, a2) <  Book (a2, a3));
        expect (Book (a1, a2) <= Book (a2, a3));
        expect (Book (a2, a3) <= Book (a2, a3));
        expect (Book (a2, a3) == Book (a2, a3));
        expect (Book (a2, a3) >= Book (a2, a3));
        expect (Book (a3, a4) >= Book (a2, a3));
        expect (Book (a3, a4) >  Book (a2, a3));

        std::hash <Book> hash;

        expect (hash (Book (a1, a2)) == hash (Book (a1, a2)));
        expect (hash (Book (a1, a3)) == hash (Book (a1, a3)));
        expect (hash (Book (a1, a4)) == hash (Book (a1, a4)));
        expect (hash (Book (a2, a3)) == hash (Book (a2, a3)));
        expect (hash (Book (a2, a4)) == hash (Book (a2, a4)));
        expect (hash (Book (a3, a4)) == hash (Book (a3, a4)));

        expect (hash (Book (a1, a2)) != hash (Book (a1, a3)));
        expect (hash (Book (a1, a2)) != hash (Book (a1, a4)));
        expect (hash (Book (a1, a2)) != hash (Book (a2, a3)));
        expect (hash (Book (a1, a2)) != hash (Book (a2, a4)));
        expect (hash (Book (a1, a2)) != hash (Book (a3, a4)));
    }

    //--------------------------------------------------------------------------

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

        {
            Set c;

            c.insert (b1);
            if (! expect (c.size () == 1)) return;
            c.insert (b2);
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (RippleBook (a1, a1)) == 0)) return;
            if (! expect (c.erase (RippleBook (a1, a2)) == 1)) return;
            if (! expect (c.erase (RippleBook (a2, a1)) == 1)) return;
            if (! expect (c.empty ())) return;
        }

        {
            Set c;

            c.insert (b1);
            if (! expect (c.size () == 1)) return;
            c.insert (b2);
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (RippleBookRef (a1, a1)) == 0)) return;
            if (! expect (c.erase (RippleBookRef (a1, a2)) == 1)) return;
            if (! expect (c.erase (RippleBookRef (a2, a1)) == 1)) return;
            if (! expect (c.empty ())) return;

    #if STL_SET_HAS_EMPLACE
            c.emplace (a1, a2);
            if (! expect (c.size() == 1)) return;
            c.emplace (a2, a1);
            if (! expect (c.size() == 2)) return;
    #endif
        }
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

        //typename Map::value_type value_type;
        //std::pair <RippleBookRef const, int> value_type;

        {
            Map c;

            //c.insert (value_type (b1, 1));
            c.insert (std::make_pair (b1, 1));
            if (! expect (c.size () == 1)) return;
            //c.insert (value_type (b2, 2));
            c.insert (std::make_pair (b2, 1));
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (RippleBook (a1, a1)) == 0)) return;
            if (! expect (c.erase (RippleBook (a1, a2)) == 1)) return;
            if (! expect (c.erase (RippleBook (a2, a1)) == 1)) return;
            if (! expect (c.empty ())) return;
        }

        {
            Map c;

            //c.insert (value_type (b1, 1));
            c.insert (std::make_pair (b1, 1));
            if (! expect (c.size () == 1)) return;
            //c.insert (value_type (b2, 2));
            c.insert (std::make_pair (b2, 1));
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (RippleBookRef (a1, a1)) == 0)) return;
            if (! expect (c.erase (RippleBookRef (a1, a2)) == 1)) return;
            if (! expect (c.erase (RippleBookRef (a2, a1)) == 1)) return;
            if (! expect (c.empty ())) return;
        }
    }

    void testBookSets ()
    {
        beginTestCase ("std::set <RippleBook>");
        testBookSet <std::set <RippleBook>> ();

        beginTestCase ("std::set <RippleBookRef>");
        testBookSet <std::set <RippleBookRef>> ();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        beginTestCase ("std::unordered_set <RippleBook>");
        testBookSet <std::unordered_set <RippleBook>> ();

        beginTestCase ("std::unordered_set <RippleBookRef>");
        testBookSet <std::unordered_set <RippleBookRef>> ();
#endif

        beginTestCase ("boost::unordered_set <RippleBook>");
        testBookSet <boost::unordered_set <RippleBook>> ();

        beginTestCase ("boost::unordered_set <RippleBookRef>");
        testBookSet <boost::unordered_set <RippleBookRef>> ();
    }

    void testBookMaps ()
    {
        beginTestCase ("std::map <RippleBook, int>");
        testBookMap <std::map <RippleBook, int>> ();

        beginTestCase ("std::map <RippleBookRef, int>");
        testBookMap <std::map <RippleBookRef, int>> ();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        beginTestCase ("std::unordered_map <RippleBook, int>");
        testBookMap <std::unordered_map <RippleBook, int>> ();

        beginTestCase ("std::unordered_map <RippleBookRef, int>");
        testBookMap <std::unordered_map <RippleBookRef, int>> ();

        beginTestCase ("boost::unordered_map <RippleBook, int>");
        testBookMap <boost::unordered_map <RippleBook, int>> ();

        beginTestCase ("boost::unordered_map <RippleBookRef, int>");
        testBookMap <boost::unordered_map <RippleBookRef, int>> ();
#endif
    }

    //--------------------------------------------------------------------------

    void runTest ()
    {
        beginTestCase ("RippleCurrency");
        testUnsigned <RippleCurrency> ();

        beginTestCase ("RippleIssuer");
        testUnsigned <RippleIssuer> ();

        // ---

        beginTestCase ("RippleAsset");
        testAssetType <RippleAsset> ();

        beginTestCase ("RippleAssetRef");
        testAssetType <RippleAssetRef> ();

        testAssetSets ();
        testAssetMaps ();

        // ---

        beginTestCase ("RippleBook");
        testBook <RippleBook> ();

        beginTestCase ("RippleBookRef");
        testBook <RippleBookRef> ();

        testBookSets ();
        testBookMaps ();
    }

    RippleAssetTests () : UnitTest ("RippleAsset", "ripple")
    {
    }
};

static RippleAssetTests rippleAssetTests;

}
