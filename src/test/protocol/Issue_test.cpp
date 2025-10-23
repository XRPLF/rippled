#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Issue.h>

#include <sys/types.h>

#include <map>
#include <optional>
#include <set>
#include <typeinfo>
#include <unordered_set>

#if BEAST_MSVC
#define STL_SET_HAS_EMPLACE 1
#else
#define STL_SET_HAS_EMPLACE 0
#endif

#ifndef XRPL_ASSETS_ENABLE_STD_HASH
#if BEAST_MAC || BEAST_IOS
#define XRPL_ASSETS_ENABLE_STD_HASH 0
#else
#define XRPL_ASSETS_ENABLE_STD_HASH 1
#endif
#endif

namespace ripple {

class Issue_test : public beast::unit_test::suite
{
public:
    using Domain = uint256;

    // Comparison, hash tests for uint60 (via base_uint)
    template <typename Unsigned>
    void
    testUnsigned()
    {
        Unsigned const u1(1);
        Unsigned const u2(2);
        Unsigned const u3(3);

        BEAST_EXPECT(u1 != u2);
        BEAST_EXPECT(u1 < u2);
        BEAST_EXPECT(u1 <= u2);
        BEAST_EXPECT(u2 <= u2);
        BEAST_EXPECT(u2 == u2);
        BEAST_EXPECT(u2 >= u2);
        BEAST_EXPECT(u3 >= u2);
        BEAST_EXPECT(u3 > u2);

        std::hash<Unsigned> hash;

        BEAST_EXPECT(hash(u1) == hash(u1));
        BEAST_EXPECT(hash(u2) == hash(u2));
        BEAST_EXPECT(hash(u3) == hash(u3));
        BEAST_EXPECT(hash(u1) != hash(u2));
        BEAST_EXPECT(hash(u1) != hash(u3));
        BEAST_EXPECT(hash(u2) != hash(u3));
    }

    //--------------------------------------------------------------------------

    // Comparison, hash tests for Issue
    template <class Issue>
    void
    testIssue()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        Currency const c3(3);
        AccountID const i3(3);

        BEAST_EXPECT(Issue(c1, i1) != Issue(c2, i1));
        BEAST_EXPECT(Issue(c1, i1) < Issue(c2, i1));
        BEAST_EXPECT(Issue(c1, i1) <= Issue(c2, i1));
        BEAST_EXPECT(Issue(c2, i1) <= Issue(c2, i1));
        BEAST_EXPECT(Issue(c2, i1) == Issue(c2, i1));
        BEAST_EXPECT(Issue(c2, i1) >= Issue(c2, i1));
        BEAST_EXPECT(Issue(c3, i1) >= Issue(c2, i1));
        BEAST_EXPECT(Issue(c3, i1) > Issue(c2, i1));
        BEAST_EXPECT(Issue(c1, i1) != Issue(c1, i2));
        BEAST_EXPECT(Issue(c1, i1) < Issue(c1, i2));
        BEAST_EXPECT(Issue(c1, i1) <= Issue(c1, i2));
        BEAST_EXPECT(Issue(c1, i2) <= Issue(c1, i2));
        BEAST_EXPECT(Issue(c1, i2) == Issue(c1, i2));
        BEAST_EXPECT(Issue(c1, i2) >= Issue(c1, i2));
        BEAST_EXPECT(Issue(c1, i3) >= Issue(c1, i2));
        BEAST_EXPECT(Issue(c1, i3) > Issue(c1, i2));

        std::hash<Issue> hash;

        BEAST_EXPECT(hash(Issue(c1, i1)) == hash(Issue(c1, i1)));
        BEAST_EXPECT(hash(Issue(c1, i2)) == hash(Issue(c1, i2)));
        BEAST_EXPECT(hash(Issue(c1, i3)) == hash(Issue(c1, i3)));
        BEAST_EXPECT(hash(Issue(c2, i1)) == hash(Issue(c2, i1)));
        BEAST_EXPECT(hash(Issue(c2, i2)) == hash(Issue(c2, i2)));
        BEAST_EXPECT(hash(Issue(c2, i3)) == hash(Issue(c2, i3)));
        BEAST_EXPECT(hash(Issue(c3, i1)) == hash(Issue(c3, i1)));
        BEAST_EXPECT(hash(Issue(c3, i2)) == hash(Issue(c3, i2)));
        BEAST_EXPECT(hash(Issue(c3, i3)) == hash(Issue(c3, i3)));
        BEAST_EXPECT(hash(Issue(c1, i1)) != hash(Issue(c1, i2)));
        BEAST_EXPECT(hash(Issue(c1, i1)) != hash(Issue(c1, i3)));
        BEAST_EXPECT(hash(Issue(c1, i1)) != hash(Issue(c2, i1)));
        BEAST_EXPECT(hash(Issue(c1, i1)) != hash(Issue(c2, i2)));
        BEAST_EXPECT(hash(Issue(c1, i1)) != hash(Issue(c2, i3)));
        BEAST_EXPECT(hash(Issue(c1, i1)) != hash(Issue(c3, i1)));
        BEAST_EXPECT(hash(Issue(c1, i1)) != hash(Issue(c3, i2)));
        BEAST_EXPECT(hash(Issue(c1, i1)) != hash(Issue(c3, i3)));
    }

    template <class Set>
    void
    testIssueSet()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        Issue const a1(c1, i1);
        Issue const a2(c2, i2);

        {
            Set c;

            c.insert(a1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(a2);
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Issue(c1, i2)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Issue(c1, i1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Issue(c2, i2)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Set c;

            c.insert(a1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(a2);
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Issue(c1, i2)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Issue(c1, i1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Issue(c2, i2)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;

#if STL_SET_HAS_EMPLACE
            c.emplace(c1, i1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.emplace(c2, i2);
            if (!BEAST_EXPECT(c.size() == 2))
                return;
#endif
        }
    }

    template <class Map>
    void
    testIssueMap()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        Issue const a1(c1, i1);
        Issue const a2(c2, i2);

        {
            Map c;

            c.insert(std::make_pair(a1, 1));
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(std::make_pair(a2, 2));
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Issue(c1, i2)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Issue(c1, i1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Issue(c2, i2)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Map c;

            c.insert(std::make_pair(a1, 1));
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(std::make_pair(a2, 2));
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Issue(c1, i2)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Issue(c1, i1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Issue(c2, i2)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }
    }

    template <class Set>
    void
    testIssueDomainSet()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        Issue const a1(c1, i1);
        Issue const a2(c2, i2);
        uint256 const domain1{1};
        uint256 const domain2{2};

        Set c;

        c.insert(std::make_pair(a1, domain1));
        if (!BEAST_EXPECT(c.size() == 1))
            return;
        c.insert(std::make_pair(a2, domain1));
        if (!BEAST_EXPECT(c.size() == 2))
            return;
        c.insert(std::make_pair(a2, domain2));
        if (!BEAST_EXPECT(c.size() == 3))
            return;

        if (!BEAST_EXPECT(c.erase(std::make_pair(Issue(c1, i2), domain1)) == 0))
            return;
        if (!BEAST_EXPECT(c.erase(std::make_pair(a1, domain1)) == 1))
            return;
        if (!BEAST_EXPECT(c.erase(std::make_pair(a2, domain1)) == 1))
            return;
        if (!BEAST_EXPECT(c.erase(std::make_pair(a2, domain2)) == 1))
            return;
        if (!BEAST_EXPECT(c.empty()))
            return;
    }

    template <class Map>
    void
    testIssueDomainMap()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        Issue const a1(c1, i1);
        Issue const a2(c2, i2);
        uint256 const domain1{1};
        uint256 const domain2{2};

        Map c;

        c.insert(std::make_pair(std::make_pair(a1, domain1), 1));
        if (!BEAST_EXPECT(c.size() == 1))
            return;
        c.insert(std::make_pair(std::make_pair(a2, domain1), 2));
        if (!BEAST_EXPECT(c.size() == 2))
            return;
        c.insert(std::make_pair(std::make_pair(a2, domain2), 2));
        if (!BEAST_EXPECT(c.size() == 3))
            return;

        if (!BEAST_EXPECT(c.erase(std::make_pair(Issue(c1, i2), domain1)) == 0))
            return;
        if (!BEAST_EXPECT(c.erase(std::make_pair(a1, domain1)) == 1))
            return;
        if (!BEAST_EXPECT(c.erase(std::make_pair(a2, domain1)) == 1))
            return;
        if (!BEAST_EXPECT(c.erase(std::make_pair(a2, domain2)) == 1))
            return;
        if (!BEAST_EXPECT(c.empty()))
            return;
    }

    void
    testIssueDomainSets()
    {
        testcase("std::set <std::pair<Issue, Domain>>");
        testIssueDomainSet<std::set<std::pair<Issue, Domain>>>();

        testcase("std::set <std::pair<Issue, Domain>>");
        testIssueDomainSet<std::set<std::pair<Issue, Domain>>>();

        testcase("hash_set <std::pair<Issue, Domain>>");
        testIssueDomainSet<hash_set<std::pair<Issue, Domain>>>();

        testcase("hash_set <std::pair<Issue, Domain>>");
        testIssueDomainSet<hash_set<std::pair<Issue, Domain>>>();
    }

    void
    testIssueDomainMaps()
    {
        testcase("std::map <std::pair<Issue, Domain>, int>");
        testIssueDomainMap<std::map<std::pair<Issue, Domain>, int>>();

        testcase("std::map <std::pair<Issue, Domain>, int>");
        testIssueDomainMap<std::map<std::pair<Issue, Domain>, int>>();

#if XRPL_ASSETS_ENABLE_STD_HASH
        testcase("hash_map <std::pair<Issue, Domain>, int>");
        testIssueDomainMap<hash_map<std::pair<Issue, Domain>, int>>();

        testcase("hash_map <std::pair<Issue, Domain>, int>");
        testIssueDomainMap<hash_map<std::pair<Issue, Domain>, int>>();

        testcase("hardened_hash_map <std::pair<Issue, Domain>, int>");
        testIssueDomainMap<hardened_hash_map<std::pair<Issue, Domain>, int>>();

        testcase("hardened_hash_map <std::pair<Issue, Domain>, int>");
        testIssueDomainMap<hardened_hash_map<std::pair<Issue, Domain>, int>>();
#endif
    }

    void
    testIssueSets()
    {
        testcase("std::set <Issue>");
        testIssueSet<std::set<Issue>>();

        testcase("std::set <Issue>");
        testIssueSet<std::set<Issue>>();

#if XRPL_ASSETS_ENABLE_STD_HASH
        testcase("std::unordered_set <Issue>");
        testIssueSet<std::unordered_set<Issue>>();

        testcase("std::unordered_set <Issue>");
        testIssueSet<std::unordered_set<Issue>>();
#endif

        testcase("hash_set <Issue>");
        testIssueSet<hash_set<Issue>>();

        testcase("hash_set <Issue>");
        testIssueSet<hash_set<Issue>>();
    }

    void
    testIssueMaps()
    {
        testcase("std::map <Issue, int>");
        testIssueMap<std::map<Issue, int>>();

        testcase("std::map <Issue, int>");
        testIssueMap<std::map<Issue, int>>();

#if XRPL_ASSETS_ENABLE_STD_HASH
        testcase("std::unordered_map <Issue, int>");
        testIssueMap<std::unordered_map<Issue, int>>();

        testcase("std::unordered_map <Issue, int>");
        testIssueMap<std::unordered_map<Issue, int>>();

        testcase("hash_map <Issue, int>");
        testIssueMap<hash_map<Issue, int>>();

        testcase("hash_map <Issue, int>");
        testIssueMap<hash_map<Issue, int>>();

#endif
    }

    //--------------------------------------------------------------------------

    // Comparison, hash tests for Book
    template <class Book>
    void
    testBook()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        Currency const c3(3);
        AccountID const i3(3);

        Issue a1(c1, i1);
        Issue a2(c1, i2);
        Issue a3(c2, i2);
        Issue a4(c3, i2);
        uint256 const domain1{1};
        uint256 const domain2{2};

        // Books without domains
        BEAST_EXPECT(Book(a1, a2, std::nullopt) != Book(a2, a3, std::nullopt));
        BEAST_EXPECT(Book(a1, a2, std::nullopt) < Book(a2, a3, std::nullopt));
        BEAST_EXPECT(Book(a1, a2, std::nullopt) <= Book(a2, a3, std::nullopt));
        BEAST_EXPECT(Book(a2, a3, std::nullopt) <= Book(a2, a3, std::nullopt));
        BEAST_EXPECT(Book(a2, a3, std::nullopt) == Book(a2, a3, std::nullopt));
        BEAST_EXPECT(Book(a2, a3, std::nullopt) >= Book(a2, a3, std::nullopt));
        BEAST_EXPECT(Book(a3, a4, std::nullopt) >= Book(a2, a3, std::nullopt));
        BEAST_EXPECT(Book(a3, a4, std::nullopt) > Book(a2, a3, std::nullopt));

        // test domain books
        {
            // Books with different domains
            BEAST_EXPECT(Book(a2, a3, domain1) != Book(a2, a3, domain2));
            BEAST_EXPECT(Book(a2, a3, domain1) < Book(a2, a3, domain2));
            BEAST_EXPECT(Book(a2, a3, domain2) > Book(a2, a3, domain1));

            // One Book has a domain, the other does not
            BEAST_EXPECT(Book(a2, a3, domain1) != Book(a2, a3, std::nullopt));
            BEAST_EXPECT(Book(a2, a3, std::nullopt) < Book(a2, a3, domain1));
            BEAST_EXPECT(Book(a2, a3, domain1) > Book(a2, a3, std::nullopt));

            // Both Books have the same domain
            BEAST_EXPECT(Book(a2, a3, domain1) == Book(a2, a3, domain1));
            BEAST_EXPECT(Book(a2, a3, domain2) == Book(a2, a3, domain2));
            BEAST_EXPECT(
                Book(a2, a3, std::nullopt) == Book(a2, a3, std::nullopt));

            // Both Books have no domain
            BEAST_EXPECT(
                Book(a2, a3, std::nullopt) == Book(a2, a3, std::nullopt));

            // Testing comparisons with >= and <=

            // When comparing books with domain1 vs domain2
            BEAST_EXPECT(Book(a2, a3, domain1) <= Book(a2, a3, domain2));
            BEAST_EXPECT(Book(a2, a3, domain2) >= Book(a2, a3, domain1));
            BEAST_EXPECT(Book(a2, a3, domain1) >= Book(a2, a3, domain1));
            BEAST_EXPECT(Book(a2, a3, domain2) <= Book(a2, a3, domain2));

            // One Book has domain1 and the other has no domain
            BEAST_EXPECT(Book(a2, a3, domain1) > Book(a2, a3, std::nullopt));
            BEAST_EXPECT(Book(a2, a3, std::nullopt) < Book(a2, a3, domain1));

            // One Book has domain2 and the other has no domain
            BEAST_EXPECT(Book(a2, a3, domain2) > Book(a2, a3, std::nullopt));
            BEAST_EXPECT(Book(a2, a3, std::nullopt) < Book(a2, a3, domain2));

            // Comparing two Books with no domains
            BEAST_EXPECT(
                Book(a2, a3, std::nullopt) <= Book(a2, a3, std::nullopt));
            BEAST_EXPECT(
                Book(a2, a3, std::nullopt) >= Book(a2, a3, std::nullopt));

            // Test case where domain1 is less than domain2
            BEAST_EXPECT(Book(a2, a3, domain1) <= Book(a2, a3, domain2));
            BEAST_EXPECT(Book(a2, a3, domain2) >= Book(a2, a3, domain1));

            // Test case where domain2 is equal to domain1
            BEAST_EXPECT(Book(a2, a3, domain1) >= Book(a2, a3, domain1));
            BEAST_EXPECT(Book(a2, a3, domain1) <= Book(a2, a3, domain1));

            // More test cases involving a4 (with domain2)

            // Comparing Book with domain2 (a4) to a Book with domain1
            BEAST_EXPECT(Book(a2, a3, domain1) < Book(a3, a4, domain2));
            BEAST_EXPECT(Book(a3, a4, domain2) > Book(a2, a3, domain1));

            // Comparing Book with domain2 (a4) to a Book with no domain
            BEAST_EXPECT(Book(a3, a4, domain2) > Book(a2, a3, std::nullopt));
            BEAST_EXPECT(Book(a2, a3, std::nullopt) < Book(a3, a4, domain2));

            // Comparing Book with domain2 (a4) to a Book with the same domain
            BEAST_EXPECT(Book(a3, a4, domain2) == Book(a3, a4, domain2));

            // Comparing Book with domain2 (a4) to a Book with domain1
            BEAST_EXPECT(Book(a2, a3, domain1) < Book(a3, a4, domain2));
            BEAST_EXPECT(Book(a3, a4, domain2) > Book(a2, a3, domain1));
        }

        std::hash<Book> hash;

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

        BEAST_EXPECT(
            hash(Book(a1, a2, std::nullopt)) ==
            hash(Book(a1, a2, std::nullopt)));
        BEAST_EXPECT(
            hash(Book(a1, a3, std::nullopt)) ==
            hash(Book(a1, a3, std::nullopt)));
        BEAST_EXPECT(
            hash(Book(a1, a4, std::nullopt)) ==
            hash(Book(a1, a4, std::nullopt)));
        BEAST_EXPECT(
            hash(Book(a2, a3, std::nullopt)) ==
            hash(Book(a2, a3, std::nullopt)));
        BEAST_EXPECT(
            hash(Book(a2, a4, std::nullopt)) ==
            hash(Book(a2, a4, std::nullopt)));
        BEAST_EXPECT(
            hash(Book(a3, a4, std::nullopt)) ==
            hash(Book(a3, a4, std::nullopt)));

        BEAST_EXPECT(
            hash(Book(a1, a2, std::nullopt)) !=
            hash(Book(a1, a3, std::nullopt)));
        BEAST_EXPECT(
            hash(Book(a1, a2, std::nullopt)) !=
            hash(Book(a1, a4, std::nullopt)));
        BEAST_EXPECT(
            hash(Book(a1, a2, std::nullopt)) !=
            hash(Book(a2, a3, std::nullopt)));
        BEAST_EXPECT(
            hash(Book(a1, a2, std::nullopt)) !=
            hash(Book(a2, a4, std::nullopt)));
        BEAST_EXPECT(
            hash(Book(a1, a2, std::nullopt)) !=
            hash(Book(a3, a4, std::nullopt)));

        // Books with domain
        BEAST_EXPECT(
            hash(Book(a1, a2, domain1)) == hash(Book(a1, a2, domain1)));
        BEAST_EXPECT(
            hash(Book(a1, a3, domain1)) == hash(Book(a1, a3, domain1)));
        BEAST_EXPECT(
            hash(Book(a1, a4, domain1)) == hash(Book(a1, a4, domain1)));
        BEAST_EXPECT(
            hash(Book(a2, a3, domain1)) == hash(Book(a2, a3, domain1)));
        BEAST_EXPECT(
            hash(Book(a2, a4, domain1)) == hash(Book(a2, a4, domain1)));
        BEAST_EXPECT(
            hash(Book(a3, a4, domain1)) == hash(Book(a3, a4, domain1)));
        BEAST_EXPECT(
            hash(Book(a1, a2, std::nullopt)) ==
            hash(Book(a1, a2, std::nullopt)));

        // Comparing Books with domain1 vs no domain
        BEAST_EXPECT(
            hash(Book(a1, a2, std::nullopt)) != hash(Book(a1, a2, domain1)));
        BEAST_EXPECT(
            hash(Book(a1, a3, std::nullopt)) != hash(Book(a1, a3, domain1)));
        BEAST_EXPECT(
            hash(Book(a1, a4, std::nullopt)) != hash(Book(a1, a4, domain1)));
        BEAST_EXPECT(
            hash(Book(a2, a3, std::nullopt)) != hash(Book(a2, a3, domain1)));
        BEAST_EXPECT(
            hash(Book(a2, a4, std::nullopt)) != hash(Book(a2, a4, domain1)));
        BEAST_EXPECT(
            hash(Book(a3, a4, std::nullopt)) != hash(Book(a3, a4, domain1)));

        // Books with domain1 but different Issues
        BEAST_EXPECT(
            hash(Book(a1, a2, domain1)) != hash(Book(a1, a3, domain1)));
        BEAST_EXPECT(
            hash(Book(a1, a2, domain1)) != hash(Book(a1, a4, domain1)));
        BEAST_EXPECT(
            hash(Book(a2, a3, domain1)) != hash(Book(a2, a4, domain1)));
        BEAST_EXPECT(
            hash(Book(a1, a2, domain1)) != hash(Book(a2, a3, domain1)));
        BEAST_EXPECT(
            hash(Book(a2, a4, domain1)) != hash(Book(a3, a4, domain1)));
        BEAST_EXPECT(
            hash(Book(a3, a4, domain1)) != hash(Book(a1, a4, domain1)));

        // Books with domain1 and domain2
        BEAST_EXPECT(
            hash(Book(a1, a2, domain1)) != hash(Book(a1, a2, domain2)));
        BEAST_EXPECT(
            hash(Book(a1, a3, domain1)) != hash(Book(a1, a3, domain2)));
        BEAST_EXPECT(
            hash(Book(a1, a4, domain1)) != hash(Book(a1, a4, domain2)));
        BEAST_EXPECT(
            hash(Book(a2, a3, domain1)) != hash(Book(a2, a3, domain2)));
        BEAST_EXPECT(
            hash(Book(a2, a4, domain1)) != hash(Book(a2, a4, domain2)));
        BEAST_EXPECT(
            hash(Book(a3, a4, domain1)) != hash(Book(a3, a4, domain2)));
    }

    //--------------------------------------------------------------------------

    template <class Set>
    void
    testBookSet()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        Issue const a1(c1, i1);
        Issue const a2(c2, i2);
        Book const b1(a1, a2, std::nullopt);
        Book const b2(a2, a1, std::nullopt);

        uint256 const domain1{1};
        uint256 const domain2{2};

        Book const b1_d1(a1, a2, domain1);
        Book const b2_d1(a2, a1, domain1);
        Book const b1_d2(a1, a2, domain2);
        Book const b2_d2(a2, a1, domain2);

        {
            Set c;

            c.insert(b1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(b2);
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a1, std::nullopt)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a1, a2, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Set c;

            c.insert(b1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(b2);
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a1, std::nullopt)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a1, a2, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;

#if STL_SET_HAS_EMPLACE
            c.emplace(a1, a2);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.emplace(a2, a1);
            if (!BEAST_EXPECT(c.size() == 2))
                return;
#endif
        }

        {
            Set c;

            c.insert(b1_d1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(b2_d1);
            if (!BEAST_EXPECT(c.size() == 2))
                return;
            c.insert(b1_d2);
            if (!BEAST_EXPECT(c.size() == 3))
                return;
            c.insert(b2_d2);
            if (!BEAST_EXPECT(c.size() == 4))
                return;

            // Try removing non-existent elements
            if (!BEAST_EXPECT(c.erase(Book(a2, a2, domain1)) == 0))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a2, domain1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, domain1)) == 1))
                return;
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a2, domain2)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, domain2)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Set c;

            c.insert(b1);
            c.insert(b2);
            c.insert(b1_d1);
            c.insert(b2_d1);
            if (!BEAST_EXPECT(c.size() == 4))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a2, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a2, domain1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, domain1)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }
    }

    template <class Map>
    void
    testBookMap()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        Issue const a1(c1, i1);
        Issue const a2(c2, i2);
        Book const b1(a1, a2, std::nullopt);
        Book const b2(a2, a1, std::nullopt);

        uint256 const domain1{1};
        uint256 const domain2{2};

        Book const b1_d1(a1, a2, domain1);
        Book const b2_d1(a2, a1, domain1);
        Book const b1_d2(a1, a2, domain2);
        Book const b2_d2(a2, a1, domain2);

        // typename Map::value_type value_type;
        // std::pair <Book const, int> value_type;

        {
            Map c;

            // c.insert (value_type (b1, 1));
            c.insert(std::make_pair(b1, 1));
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            // c.insert (value_type (b2, 2));
            c.insert(std::make_pair(b2, 1));
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a1, std::nullopt)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a1, a2, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Map c;

            // c.insert (value_type (b1, 1));
            c.insert(std::make_pair(b1, 1));
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            // c.insert (value_type (b2, 2));
            c.insert(std::make_pair(b2, 1));
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a1, std::nullopt)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a1, a2, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Map c;

            c.insert(std::make_pair(b1_d1, 10));
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(std::make_pair(b2_d1, 20));
            if (!BEAST_EXPECT(c.size() == 2))
                return;
            c.insert(std::make_pair(b1_d2, 30));
            if (!BEAST_EXPECT(c.size() == 3))
                return;
            c.insert(std::make_pair(b2_d2, 40));
            if (!BEAST_EXPECT(c.size() == 4))
                return;

            // Try removing non-existent elements
            if (!BEAST_EXPECT(c.erase(Book(a2, a2, domain1)) == 0))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a2, domain1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, domain1)) == 1))
                return;
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a2, domain2)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, domain2)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Map c;

            c.insert(std::make_pair(b1, 1));
            c.insert(std::make_pair(b2, 2));
            c.insert(std::make_pair(b1_d1, 3));
            c.insert(std::make_pair(b2_d1, 4));
            if (!BEAST_EXPECT(c.size() == 4))
                return;

            // Try removing non-existent elements
            if (!BEAST_EXPECT(c.erase(Book(a1, a1, domain1)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a2, domain2)) == 0))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a2, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, std::nullopt)) == 1))
                return;
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a2, domain1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1, domain1)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }
    }

    void
    testBookSets()
    {
        testcase("std::set <Book>");
        testBookSet<std::set<Book>>();

        testcase("std::set <Book>");
        testBookSet<std::set<Book>>();

#if XRPL_ASSETS_ENABLE_STD_HASH
        testcase("std::unordered_set <Book>");
        testBookSet<std::unordered_set<Book>>();

        testcase("std::unordered_set <Book>");
        testBookSet<std::unordered_set<Book>>();
#endif

        testcase("hash_set <Book>");
        testBookSet<hash_set<Book>>();

        testcase("hash_set <Book>");
        testBookSet<hash_set<Book>>();
    }

    void
    testBookMaps()
    {
        testcase("std::map <Book, int>");
        testBookMap<std::map<Book, int>>();

        testcase("std::map <Book, int>");
        testBookMap<std::map<Book, int>>();

#if XRPL_ASSETS_ENABLE_STD_HASH
        testcase("std::unordered_map <Book, int>");
        testBookMap<std::unordered_map<Book, int>>();

        testcase("std::unordered_map <Book, int>");
        testBookMap<std::unordered_map<Book, int>>();

        testcase("hash_map <Book, int>");
        testBookMap<hash_map<Book, int>>();

        testcase("hash_map <Book, int>");
        testBookMap<hash_map<Book, int>>();
#endif
    }

    //--------------------------------------------------------------------------

    void
    run() override
    {
        testcase("Currency");
        testUnsigned<Currency>();

        testcase("AccountID");
        testUnsigned<AccountID>();

        // ---

        testcase("Issue");
        testIssue<Issue>();

        testcase("Issue");
        testIssue<Issue>();

        testIssueSets();
        testIssueMaps();

        // ---

        testcase("Book");
        testBook<Book>();

        testcase("Book");
        testBook<Book>();

        testBookSets();
        testBookMaps();

        // ---
        testIssueDomainSets();
        testIssueDomainMaps();
    }
};

BEAST_DEFINE_TESTSUITE(Issue, protocol, ripple);

}  // namespace ripple
