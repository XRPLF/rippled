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

#include <ripple/common/UnorderedContainers.h>

#include <beast/unit_test/suite.h>

#include <boost/unordered_set.hpp>

#include <set>
#include <typeinfo>
#include <unordered_set>

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

class Issue_test : public beast::unit_test::suite
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

    // Comparison, hash tests for IssueType
    template <class Asset>
    void testAssetType ()
    {
        Currency const c1 (1); Account const i1 (1);
        Currency const c2 (2); Account const i2 (2);
        Currency const c3 (3); Account const i3 (3);

        expect (Asset (i1, c1) != Asset (i1, c2));
        expect (Asset (i1, c1) <  Asset (i1, c2));
        expect (Asset (i1, c1) <= Asset (i1, c2));
        expect (Asset (i1, c2) <= Asset (i1, c2));
        expect (Asset (i1, c2) == Asset (i1, c2));
        expect (Asset (i1, c2) >= Asset (i1, c2));
        expect (Asset (i1, c3) >= Asset (i1, c2));
        expect (Asset (i1, c3) >  Asset (i1, c2));
        expect (Asset (i1, c1) != Asset (i2, c1));
        expect (Asset (i1, c1) <  Asset (i2, c1));
        expect (Asset (i1, c1) <= Asset (i2, c1));
        expect (Asset (i2, c1) <= Asset (i2, c1));
        expect (Asset (i2, c1) == Asset (i2, c1));
        expect (Asset (i2, c1) >= Asset (i2, c1));
        expect (Asset (i3, c1) >= Asset (i2, c1));
        expect (Asset (i3, c1) >  Asset (i2, c1));

        std::hash <Asset> hash;

        expect (hash (Asset (i1, c1)) == hash (Asset (i1, c1)));
        expect (hash (Asset (i2, c1)) == hash (Asset (i2, c1)));
        expect (hash (Asset (i3, c1)) == hash (Asset (i3, c1)));
        expect (hash (Asset (i1, c2)) == hash (Asset (i1, c2)));
        expect (hash (Asset (i2, c2)) == hash (Asset (i2, c2)));
        expect (hash (Asset (i3, c2)) == hash (Asset (i3, c2)));
        expect (hash (Asset (i1, c3)) == hash (Asset (i1, c3)));
        expect (hash (Asset (i2, c3)) == hash (Asset (i2, c3)));
        expect (hash (Asset (i3, c3)) == hash (Asset (i3, c3)));
        expect (hash (Asset (i1, c1)) != hash (Asset (i2, c1)));
        expect (hash (Asset (i1, c1)) != hash (Asset (i3, c1)));
        expect (hash (Asset (i1, c1)) != hash (Asset (i1, c2)));
        expect (hash (Asset (i1, c1)) != hash (Asset (i2, c2)));
        expect (hash (Asset (i1, c1)) != hash (Asset (i3, c2)));
        expect (hash (Asset (i1, c1)) != hash (Asset (i1, c3)));
        expect (hash (Asset (i1, c1)) != hash (Asset (i2, c3)));
        expect (hash (Asset (i1, c1)) != hash (Asset (i3, c3)));
    }

    template <class Set>
    void testAssetSet ()
    {
        Currency const c1 (1);
        Account   const i1 (1);
        Currency const c2 (2);
        Account   const i2 (2);
        IssueRef const a1 (i1, c1);
        IssueRef const a2 (i2, c2);

        {
            Set c;

            c.insert (a1);
            if (! expect (c.size () == 1)) return;
            c.insert (a2);
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (Issue (i2, c1)) == 0)) return;
            if (! expect (c.erase (Issue (i1, c1)) == 1)) return;
            if (! expect (c.erase (Issue (i2, c2)) == 1)) return;
            if (! expect (c.empty ())) return;
        }

        {
            Set c;

            c.insert (a1);
            if (! expect (c.size () == 1)) return;
            c.insert (a2);
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (IssueRef (i2, c1)) == 0)) return;
            if (! expect (c.erase (IssueRef (i1, c1)) == 1)) return;
            if (! expect (c.erase (IssueRef (i2, c2)) == 1)) return;
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
        Currency const c1 (1);
        Account   const i1 (1);
        Currency const c2 (2);
        Account   const i2 (2);
        IssueRef const a1 (i1, c1);
        IssueRef const a2 (i2, c2);

        {
            Map c;

            c.insert (std::make_pair (a1, 1));
            if (! expect (c.size () == 1)) return;
            c.insert (std::make_pair (a2, 2));
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (Issue (i2, c1)) == 0)) return;
            if (! expect (c.erase (Issue (i1, c1)) == 1)) return;
            if (! expect (c.erase (Issue (i2, c2)) == 1)) return;
            if (! expect (c.empty ())) return;
        }

        {
            Map c;

            c.insert (std::make_pair (a1, 1));
            if (! expect (c.size () == 1)) return;
            c.insert (std::make_pair (a2, 2));
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (IssueRef (i2, c1)) == 0)) return;
            if (! expect (c.erase (IssueRef (i1, c1)) == 1)) return;
            if (! expect (c.erase (IssueRef (i2, c2)) == 1)) return;
            if (! expect (c.empty ())) return;
        }
    }

    void testAssetSets ()
    {
        testcase ("std::set <Issue>");
        testAssetSet <std::set <Issue>> ();

        testcase ("std::set <IssueRef>");
        testAssetSet <std::set <IssueRef>> ();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        testcase ("std::unordered_set <Issue>");
        testAssetSet <std::unordered_set <Issue>> ();

        testcase ("std::unordered_set <IssueRef>");
        testAssetSet <std::unordered_set <IssueRef>> ();
#endif

        testcase ("ripple::unordered_set <Issue>");
        testAssetSet <ripple::unordered_set <Issue>> ();

        testcase ("ripple::unordered_set <IssueRef>");
        testAssetSet <ripple::unordered_set <IssueRef>> ();
    }

    void testAssetMaps ()
    {
        testcase ("std::map <Issue, int>");
        testAssetMap <std::map <Issue, int>> ();

        testcase ("std::map <IssueRef, int>");
        testAssetMap <std::map <IssueRef, int>> ();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        testcase ("std::unordered_map <Issue, int>");
        testAssetMap <std::unordered_map <Issue, int>> ();

        testcase ("std::unordered_map <IssueRef, int>");
        testAssetMap <std::unordered_map <IssueRef, int>> ();

        testcase ("ripple::unordered_map <Issue, int>");
        testAssetMap <ripple::unordered_map <Issue, int>> ();

        testcase ("ripple::unordered_map <IssueRef, int>");
        testAssetMap <ripple::unordered_map <IssueRef, int>> ();

#endif
    }

    //--------------------------------------------------------------------------

    // Comparison, hash tests for BookType
    template <class Book>
    void testBook ()
    {
        Currency const c1 (1); Account const i1 (1);
        Currency const c2 (2); Account const i2 (2);
        Currency const c3 (3); Account const i3 (3);

        Issue a1 (i1, c1);
        Issue a2 (i2, c1);
        Issue a3 (i2, c2);
        Issue a4 (i2, c3);

        expect (Book (a1, a2) != Book (a2, a3));
        expect (Book (a1, a2) <  Book (a2, a3));
        expect (Book (a1, a2) <= Book (a2, a3));
        expect (Book (a2, a3) <= Book (a2, a3));
        expect (Book (a2, a3) == Book (a2, a3));
        expect (Book (a2, a3) >= Book (a2, a3));
        expect (Book (a3, a4) >= Book (a2, a3));
        expect (Book (a3, a4) >  Book (a2, a3));

        std::hash <Book> hash;

//         log << std::hex << hash (Book (a1, a2));
//         log << std::hex << hash (Book (a1, a2));
//
//         log << std::hex << hash (Book (a1, a3));
//         log << std::hex << hash (Book (a1, a3));
//
//         log << std::hex << hash (Book (a1, a4));
//         log << std::hex << hash (Book (a1, a4));
//
//         log << std::hex << hash (Book (a2, a3));
//         log << std::hex << hash (Book (a2, a3));
//
//         log << std::hex << hash (Book (a2, a4));
//         log << std::hex << hash (Book (a2, a4));
//
//         log << std::hex << hash (Book (a3, a4));
//         log << std::hex << hash (Book (a3, a4));

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
        Currency const c1 (1);
        Account   const i1 (1);
        Currency const c2 (2);
        Account   const i2 (2);
        IssueRef const a1 (i1, c1);
        IssueRef const a2 (i2, c2);
        BookRef  const b1 (a1, a2);
        BookRef  const b2 (a2, a1);

        {
            Set c;

            c.insert (b1);
            if (! expect (c.size () == 1)) return;
            c.insert (b2);
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (Book (a1, a1)) == 0)) return;
            if (! expect (c.erase (Book (a1, a2)) == 1)) return;
            if (! expect (c.erase (Book (a2, a1)) == 1)) return;
            if (! expect (c.empty ())) return;
        }

        {
            Set c;

            c.insert (b1);
            if (! expect (c.size () == 1)) return;
            c.insert (b2);
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (BookRef (a1, a1)) == 0)) return;
            if (! expect (c.erase (BookRef (a1, a2)) == 1)) return;
            if (! expect (c.erase (BookRef (a2, a1)) == 1)) return;
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
        Currency const c1 (1);
        Account   const i1 (1);
        Currency const c2 (2);
        Account   const i2 (2);
        IssueRef const a1 (i1, c1);
        IssueRef const a2 (i2, c2);
        BookRef  const b1 (a1, a2);
        BookRef  const b2 (a2, a1);

        //typename Map::value_type value_type;
        //std::pair <BookRef const, int> value_type;

        {
            Map c;

            //c.insert (value_type (b1, 1));
            c.insert (std::make_pair (b1, 1));
            if (! expect (c.size () == 1)) return;
            //c.insert (value_type (b2, 2));
            c.insert (std::make_pair (b2, 1));
            if (! expect (c.size () == 2)) return;

            if (! expect (c.erase (Book (a1, a1)) == 0)) return;
            if (! expect (c.erase (Book (a1, a2)) == 1)) return;
            if (! expect (c.erase (Book (a2, a1)) == 1)) return;
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

            if (! expect (c.erase (BookRef (a1, a1)) == 0)) return;
            if (! expect (c.erase (BookRef (a1, a2)) == 1)) return;
            if (! expect (c.erase (BookRef (a2, a1)) == 1)) return;
            if (! expect (c.empty ())) return;
        }
    }

    void testBookSets ()
    {
        testcase ("std::set <Book>");
        testBookSet <std::set <Book>> ();

        testcase ("std::set <BookRef>");
        testBookSet <std::set <BookRef>> ();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        testcase ("std::unordered_set <Book>");
        testBookSet <std::unordered_set <Book>> ();

        testcase ("std::unordered_set <BookRef>");
        testBookSet <std::unordered_set <BookRef>> ();
#endif

        testcase ("ripple::unordered_set <Book>");
        testBookSet <ripple::unordered_set <Book>> ();

        testcase ("ripple::unordered_set <BookRef>");
        testBookSet <ripple::unordered_set <BookRef>> ();
    }

    void testBookMaps ()
    {
        testcase ("std::map <Book, int>");
        testBookMap <std::map <Book, int>> ();

        testcase ("std::map <BookRef, int>");
        testBookMap <std::map <BookRef, int>> ();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        testcase ("std::unordered_map <Book, int>");
        testBookMap <std::unordered_map <Book, int>> ();

        testcase ("std::unordered_map <BookRef, int>");
        testBookMap <std::unordered_map <BookRef, int>> ();

        testcase ("ripple::unordered_map <Book, int>");
        testBookMap <ripple::unordered_map <Book, int>> ();

        testcase ("ripple::unordered_map <BookRef, int>");
        testBookMap <ripple::unordered_map <BookRef, int>> ();
#endif
    }

    //--------------------------------------------------------------------------

    void run()
    {
        testcase ("Currency");
        testUnsigned <Currency> ();

        testcase ("Account");
        testUnsigned <Account> ();

        // ---

        testcase ("Issue");
        testAssetType <Issue> ();

        testcase ("IssueRef");
        testAssetType <IssueRef> ();

        testAssetSets ();
        testAssetMaps ();

        // ---

        testcase ("Book");
        testBook <Book> ();

        testcase ("BookRef");
        testBook <BookRef> ();

        testBookSets ();
        testBookMaps ();
    }
};

BEAST_DEFINE_TESTSUITE(Issue,types,ripple);

}
