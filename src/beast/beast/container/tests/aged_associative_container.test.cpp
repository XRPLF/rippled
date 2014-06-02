//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#include <beast/chrono/manual_clock.h>
#include <beast/unit_test/suite.h>

#include <beast/container/aged_set.h>
#include <beast/container/aged_map.h>
#include <beast/container/aged_multiset.h>
#include <beast/container/aged_multimap.h>
#include <beast/container/aged_unordered_set.h>
#include <beast/container/aged_unordered_map.h>
#include <beast/container/aged_unordered_multiset.h>
#include <beast/container/aged_unordered_multimap.h>

#include <vector>
#include <list>

#ifndef BEAST_AGED_UNORDERED_NO_ALLOC_DEFAULTCTOR
# ifdef _MSC_VER
#  define BEAST_AGED_UNORDERED_NO_ALLOC_DEFAULTCTOR 0
# else
#  define BEAST_AGED_UNORDERED_NO_ALLOC_DEFAULTCTOR 1
# endif
#endif

#ifndef BEAST_CONTAINER_EXTRACT_NOREF
# ifdef _MSC_VER
#  define BEAST_CONTAINER_EXTRACT_NOREF 1
# else
#  define BEAST_CONTAINER_EXTRACT_NOREF 1
# endif
#endif

namespace beast {

class aged_associative_container_test_base : public unit_test::suite
{
public:
    template <class T>
    struct CompT
    {
        explicit CompT (int)
        {
        }

        CompT (CompT const&)
        {
        }

        bool operator() (T const& lhs, T const& rhs) const
        {
            return m_less (lhs, rhs);
        }

    private:
        CompT () = delete;
        std::less <T> m_less;
    };

    template <class T>
    class HashT
    {
    public:
        explicit HashT (int)
        {
        }

        std::size_t operator() (T const& t) const
        {
            return m_hash (t);
        }

    private:
        HashT() = delete;
        std::hash <T> m_hash;
    };

    template <class T>
    struct EqualT
    {
    public:
        explicit EqualT (int)
        {
        }

        bool operator() (T const& lhs, T const& rhs) const
        {
            return m_eq (lhs, rhs);
        }

    private:
        EqualT() = delete;
        std::equal_to <T> m_eq;
    };

    template <class T>
    struct AllocT
    {
        typedef T value_type;

        //typedef propagate_on_container_swap : std::true_type::type;

        template <class U>
        struct rebind
        {
            typedef AllocT <U> other;
        };

        explicit AllocT (int)
        {
        }

        AllocT (AllocT const&)
        {
        }

        template <class U>
        AllocT (AllocT <U> const&)
        {
        }

        template <class U>
        bool operator== (AllocT <U> const&) const
        {
            return true;
        }

        T* allocate (std::size_t n, T const* = 0)
        {
            return static_cast <T*> (
                ::operator new (n * sizeof(T)));
        }
        
        void deallocate (T* p, std::size_t)
        {
            ::operator delete (p);
        }

#if ! BEAST_AGED_UNORDERED_NO_ALLOC_DEFAULTCTOR
        AllocT ()
        {
        }
#else
    private:
        AllocT() = delete;
#endif
    };

    //--------------------------------------------------------------------------

    // ordered
    template <class Base, bool IsUnordered>
    class MaybeUnordered : public Base
    {
    public:
        typedef std::less <typename Base::Key> Comp;
        typedef CompT <typename Base::Key> MyComp;

    protected:
        static std::string name_ordered_part()
        {
            return "";
        }
    };

    // unordered
    template <class Base>
    class MaybeUnordered <Base, true> : public Base
    {
    public:
        typedef std::hash <typename Base::Key> Hash;
        typedef std::equal_to <typename Base::Key> Equal;
        typedef HashT <typename Base::Key> MyHash;
        typedef EqualT <typename Base::Key> MyEqual;

    protected:
        static std::string name_ordered_part()
        {
            return "unordered_";
        }
    };

    // unique
    template <class Base, bool IsMulti>
    class MaybeMulti : public Base
    {
    public:
    protected:
        static std::string name_multi_part()
        {
            return "";
        }
    };

    // multi
    template <class Base>
    class MaybeMulti <Base, true> : public Base
    {
    public:
    protected:
        static std::string name_multi_part()
        {
            return "multi";
        }
    };

    // set
    template <class Base, bool IsMap>
    class MaybeMap : public Base
    {
    public:
        typedef void T;
        typedef typename Base::Key Value;
        typedef std::vector <Value> Values;

        static typename Base::Key const& extract (Value const& value)
        {
            return value;
        }

        static Values values()
        {
            Values v {
                "apple",
                "banana",
                "cherry",
                "grape",
                "orange",
            };
            return v;
        };

    protected:
        static std::string name_map_part()
        {
            return "set";
        }
    };

    // map
    template <class Base>
    class MaybeMap <Base, true> : public Base
    {
    public:
        typedef int T;
        typedef std::pair <typename Base::Key, T> Value;
        typedef std::vector <Value> Values;

        static typename Base::Key const& extract (Value const& value)
        {
            return value.first;
        }

        static Values values()
        {
            Values v {
                std::make_pair ("apple",  1),
                std::make_pair ("banana", 2),
                std::make_pair ("cherry", 3),
                std::make_pair ("grape",  4),
                std::make_pair ("orange", 5)
            };
            return v;
        };

    protected:
        static std::string name_map_part()
        {
            return "map";
        }
    };

    //--------------------------------------------------------------------------

    // ordered
    template <
        class Base,
        bool IsUnordered = Base::is_unordered::value
    >
    struct ContType
    {
        template <
            class Compare = std::less <typename Base::Key>,
            class Allocator = std::allocator <typename Base::Value>
        >
        using Cont = detail::aged_ordered_container <
            Base::is_multi::value, Base::is_map::value, typename Base::Key,
                typename Base::T, typename Base::Dur, Compare, Allocator>;
    };

    // unordered
    template <
        class Base
    >
    struct ContType <Base, true>
    {
        template <
            class Hash = std::hash <typename Base::Key>,
            class KeyEqual = std::equal_to <typename Base::Key>,
            class Allocator = std::allocator <typename Base::Value>
        >
        using Cont = detail::aged_unordered_container <
            Base::is_multi::value, Base::is_map::value,
                typename Base::Key, typename Base::T, typename Base::Dur,
                    Hash, KeyEqual, Allocator>;
    };

    //--------------------------------------------------------------------------

    struct TestTraitsBase
    {
        typedef std::string Key;
        typedef std::chrono::seconds Dur;
        typedef manual_clock <Dur> Clock;
    };

    template <bool IsUnordered, bool IsMulti, bool IsMap>
    struct TestTraitsHelper
        : MaybeUnordered <MaybeMulti <MaybeMap <
            TestTraitsBase, IsMap>, IsMulti>, IsUnordered>
    {
    private:
        typedef MaybeUnordered <MaybeMulti <MaybeMap <
            TestTraitsBase, IsMap>, IsMulti>, IsUnordered> Base;

    public:
        using typename Base::Key;

        typedef std::integral_constant <bool, IsUnordered> is_unordered;
        typedef std::integral_constant <bool, IsMulti> is_multi;
        typedef std::integral_constant <bool, IsMap> is_map;

        typedef std::allocator <typename Base::Value> Alloc;
        typedef AllocT <typename Base::Value> MyAlloc;

        static std::string name()
        {
            return std::string ("aged_") +
                Base::name_ordered_part() +
                    Base::name_multi_part() +
                        Base::name_map_part();
        }
    };

    template <bool IsUnordered, bool IsMulti, bool IsMap>
    struct TestTraits
        : TestTraitsHelper <IsUnordered, IsMulti, IsMap>
        , ContType <TestTraitsHelper <IsUnordered, IsMulti, IsMap>>
    {
    };

    template <class Cont>
    static std::string name (Cont const&)
    {
        return TestTraits <
            Cont::is_unordered,
            Cont::is_multi,
            Cont::is_map>::name();
    }

    template <class Traits>
    struct equal_value
    {
        bool operator() (typename Traits::Value const& lhs,
            typename Traits::Value const& rhs)
        {
            return Traits::extract (lhs) == Traits::extract (rhs);
        }
    };

    template <class Cont>
    static
    std::list <typename Cont::value_type>
    make_list (Cont const& c)
    {
        return std::list <typename Cont::value_type> (
            c.begin(), c.end());
    }

    //--------------------------------------------------------------------------

    template <
        class Container,
        class Values
    >
    typename std::enable_if <
        Container::is_map::value && ! Container::is_multi::value>::type
    checkMapContents (Container& c, Values const& v);

    template <
        class Container,
        class Values
    >
    typename std::enable_if <!
        (Container::is_map::value && ! Container::is_multi::value)>::type
    checkMapContents (Container, Values const&)
    {
    }

    // unordered
    template <
        class C,
        class Values
    >
    typename std::enable_if <
        std::remove_reference <C>::type::is_unordered::value>::type
    checkUnorderedContentsRefRef (C&& c, Values const& v);

    template <
        class C,
        class Values
    >
    typename std::enable_if <!
        std::remove_reference <C>::type::is_unordered::value>::type
    checkUnorderedContentsRefRef (C&&, Values const&)
    {
    }

    template <class C, class Values>
    void checkContentsRefRef (C&& c, Values const& v);

    template <class Cont, class Values>
    void checkContents (Cont& c, Values const& v);

    template <class Cont>
    void checkContents (Cont& c);

    //--------------------------------------------------------------------------

    // ordered
    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <! IsUnordered>::type
    testConstructEmpty ();

    // unordered
    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <IsUnordered>::type
    testConstructEmpty ();

    // ordered
    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <! IsUnordered>::type
    testConstructRange ();

    // unordered
    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <IsUnordered>::type
    testConstructRange ();

    // ordered
    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <! IsUnordered>::type
    testConstructInitList ();

    // unordered
    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <IsUnordered>::type
    testConstructInitList ();

    //--------------------------------------------------------------------------

    template <bool IsUnordered, bool IsMulti, bool IsMap>
    void
    testCopyMove ();

    //--------------------------------------------------------------------------

    template <class Container, class Values>
    void checkInsertCopy (Container& c, Values const& v);

    template <class Container, class Values>
    void checkInsertMove (Container& c, Values const& v);

    template <class Container, class Values>
    void checkInsertHintCopy (Container& c, Values const& v);

    template <class Container, class Values>
    void checkInsertHintMove (Container& c, Values const& v);

    template <class Container, class Values>
    void checkEmplace (Container& c, Values const& v);

    template <class Container, class Values>
    void checkEmplaceHint (Container& c, Values const& v);

    template <bool IsUnordered, bool IsMulti, bool IsMap>
    void testModifiers();

    //--------------------------------------------------------------------------

    template <bool IsUnordered, bool IsMulti, bool IsMap>
    void
    testChronological ();

    //--------------------------------------------------------------------------

    // map, unordered_map
    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <IsMap && ! IsMulti>::type
    testArrayCreate();

    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <! (IsMap && ! IsMulti)>::type
    testArrayCreate()
    {
    }

    //--------------------------------------------------------------------------

    // ordered
    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <! IsUnordered>::type
    testCompare ();

    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <IsUnordered>::type
    testCompare ()
    {
    }

    //--------------------------------------------------------------------------

    // ordered
    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <! IsUnordered>::type
    testObservers();

    // unordered
    template <bool IsUnordered, bool IsMulti, bool IsMap>
    typename std::enable_if <IsUnordered>::type
    testObservers();

    //--------------------------------------------------------------------------

    template <bool IsUnordered, bool IsMulti, bool IsMap>
    void testMaybeUnorderedMultiMap ();

    template <bool IsUnordered, bool IsMulti>
    void testMaybeUnorderedMulti();

    template <bool IsUnordered>
    void testMaybeUnordered();
};

//------------------------------------------------------------------------------

// Check contents via at() and operator[]
// map, unordered_map
template <
    class Container,
    class Values
>
typename std::enable_if <
    Container::is_map::value && ! Container::is_multi::value>::type
aged_associative_container_test_base::
checkMapContents (Container& c, Values const& v)
{
    if (v.empty())
    {
        expect (c.empty());
        expect (c.size() == 0);
        return;
    }

    try
    {
        // Make sure no exception is thrown
        for (auto const& e : v)
            c.at (e.first);
        for (auto const& e : v)
            expect (c.operator[](e.first) == e.second);
    }
    catch (std::out_of_range const&)
    {
        fail ("caught exception");
    }
}

// unordered
template <
    class C,
    class Values
>
typename std::enable_if <
    std::remove_reference <C>::type::is_unordered::value>::type
aged_associative_container_test_base::
checkUnorderedContentsRefRef (C&& c, Values const& v)
{
    typedef typename std::remove_reference <C>::type Cont;
    typedef TestTraits <
        Cont::is_unordered::value,
            Cont::is_multi::value,
                Cont::is_map::value
                > Traits;
    typedef typename Cont::size_type size_type;
    auto const hash (c.hash_function());
    auto const key_eq (c.key_eq());
    for (size_type i (0); i < c.bucket_count(); ++i)
    {
        auto const last (c.end(i));
        for (auto iter (c.begin (i)); iter != last; ++iter)
        {
            auto const match (std::find_if (v.begin(), v.end(),
                [iter](typename Values::value_type const& e)
                {
                    return Traits::extract (*iter) ==
                        Traits::extract (e);
                }));
            expect (match != v.end());
            expect (key_eq (Traits::extract (*iter),
                Traits::extract (*match)));
            expect (hash (Traits::extract (*iter)) ==
                hash (Traits::extract (*match)));
        }
    }
}

template <class C, class Values>
void
aged_associative_container_test_base::
checkContentsRefRef (C&& c, Values const& v)
{
    typedef typename std::remove_reference <C>::type Cont;
    typedef TestTraits <
        Cont::is_unordered::value,
            Cont::is_multi::value,
                Cont::is_map::value
                > Traits;
    typedef typename Cont::size_type size_type;

    expect (c.size() == v.size());
    expect (size_type (std::distance (
        c.begin(), c.end())) == v.size());
    expect (size_type (std::distance (
        c.cbegin(), c.cend())) == v.size());
    expect (size_type (std::distance (
        c.chronological.begin(), c.chronological.end())) == v.size());
    expect (size_type (std::distance (
        c.chronological.cbegin(), c.chronological.cend())) == v.size());
    expect (size_type (std::distance (
        c.chronological.rbegin(), c.chronological.rend())) == v.size());
    expect (size_type (std::distance (
        c.chronological.crbegin(), c.chronological.crend())) == v.size());

    checkUnorderedContentsRefRef (c, v);
}

template <class Cont, class Values>
void
aged_associative_container_test_base::
checkContents (Cont& c, Values const& v)
{
    checkContentsRefRef (c, v);
    checkContentsRefRef (const_cast <Cont const&> (c), v);
    checkMapContents (c, v);
}

template <class Cont>
void
aged_associative_container_test_base::
checkContents (Cont& c)
{
    typedef TestTraits <
        Cont::is_unordered::value,
            Cont::is_multi::value,
                Cont::is_map::value
                > Traits;
    typedef typename Traits::Values Values;
    checkContents (c, Values());
}

//------------------------------------------------------------------------------
//
// Construction
//
//------------------------------------------------------------------------------

// ordered
template <bool IsUnordered, bool IsMulti, bool IsMap>
typename std::enable_if <! IsUnordered>::type
aged_associative_container_test_base::
testConstructEmpty ()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typedef typename Traits::Value Value;
    typedef typename Traits::Key Key;
    typedef typename Traits::T T;
    typedef typename Traits::Dur Dur;
    typedef typename Traits::Comp Comp;
    typedef typename Traits::Alloc Alloc;
    typedef typename Traits::MyComp MyComp;
    typedef typename Traits::MyAlloc MyAlloc;
    typename Traits::Clock clock;

    //testcase (Traits::name() + " empty");
    testcase ("empty");

    {
        typename Traits::template Cont <Comp, Alloc> c (
            clock);
        checkContents (c);
    }

    {
        typename Traits::template Cont <MyComp, Alloc> c (
            clock, MyComp(1));
        checkContents (c);
    }

    {
        typename Traits::template Cont <Comp, MyAlloc> c (
            clock, MyAlloc(1));
        checkContents (c);
    }

    {
        typename Traits::template Cont <MyComp, MyAlloc> c (
            clock, MyComp(1), MyAlloc(1));
        checkContents (c);
    }
}

// unordered
template <bool IsUnordered, bool IsMulti, bool IsMap>
typename std::enable_if <IsUnordered>::type
aged_associative_container_test_base::
testConstructEmpty ()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typedef typename Traits::Value Value;
    typedef typename Traits::Key Key;
    typedef typename Traits::T T;
    typedef typename Traits::Dur Dur;
    typedef typename Traits::Hash Hash;
    typedef typename Traits::Equal Equal;
    typedef typename Traits::Alloc Alloc;
    typedef typename Traits::MyHash MyHash;
    typedef typename Traits::MyEqual MyEqual;
    typedef typename Traits::MyAlloc MyAlloc;
    typename Traits::Clock clock;

    //testcase (Traits::name() + " empty");
    testcase ("empty");
    {
        typename Traits::template Cont <Hash, Equal, Alloc> c (
            clock);
        checkContents (c);
    }

    {
        typename Traits::template Cont <MyHash, Equal, Alloc> c (
            clock, MyHash(1));
        checkContents (c);
    }

    {
        typename Traits::template Cont <Hash, MyEqual, Alloc> c (
            clock, MyEqual (1));
        checkContents (c);
    }

    {
        typename Traits::template Cont <Hash, Equal, MyAlloc> c (
            clock, MyAlloc (1));
        checkContents (c);
    }

    {
        typename Traits::template Cont <MyHash, MyEqual, Alloc> c (
            clock, MyHash(1), MyEqual(1));
        checkContents (c);
    }

    {
        typename Traits::template Cont <MyHash, Equal, MyAlloc> c (
            clock, MyHash(1), MyAlloc(1));
        checkContents (c);
    }

    {
        typename Traits::template Cont <Hash, MyEqual, MyAlloc> c (
            clock, MyEqual(1), MyAlloc(1));
        checkContents (c);
    }

    {
        typename Traits::template Cont <MyHash, MyEqual, MyAlloc> c (
            clock, MyHash(1), MyEqual(1), MyAlloc(1));
        checkContents (c);
    }
}

// ordered
template <bool IsUnordered, bool IsMulti, bool IsMap>
typename std::enable_if <! IsUnordered>::type
aged_associative_container_test_base::
testConstructRange ()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typedef typename Traits::Value Value;
    typedef typename Traits::Key Key;
    typedef typename Traits::T T;
    typedef typename Traits::Dur Dur;
    typedef typename Traits::Comp Comp;
    typedef typename Traits::Alloc Alloc;
    typedef typename Traits::MyComp MyComp;
    typedef typename Traits::MyAlloc MyAlloc;
    typename Traits::Clock clock;
    auto const v (Traits::values());

    //testcase (Traits::name() + " range");
    testcase ("range");

    {
        typename Traits::template Cont <Comp, Alloc> c (
            v.begin(), v.end(),
            clock);
        checkContents (c, v);
    }

    {
        typename Traits::template Cont <MyComp, Alloc> c (
            v.begin(), v.end(),
            clock, MyComp(1));
        checkContents (c, v);
    }

    {
        typename Traits::template Cont <Comp, MyAlloc> c (
            v.begin(), v.end(),
            clock, MyAlloc(1));
        checkContents (c, v);
    }

    {
        typename Traits::template Cont <MyComp, MyAlloc> c (
            v.begin(), v.end(),
            clock, MyComp(1), MyAlloc(1));
        checkContents (c, v);

    }

    // swap

    {
        typename Traits::template Cont <Comp, Alloc> c1 (
            v.begin(), v.end(),
            clock);
        typename Traits::template Cont <Comp, Alloc> c2 (
            clock);
        std::swap (c1, c2);
        checkContents (c2, v);
    }
}

// unordered
template <bool IsUnordered, bool IsMulti, bool IsMap>
typename std::enable_if <IsUnordered>::type
aged_associative_container_test_base::
testConstructRange ()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typedef typename Traits::Value Value;
    typedef typename Traits::Key Key;
    typedef typename Traits::T T;
    typedef typename Traits::Dur Dur;
    typedef typename Traits::Hash Hash;
    typedef typename Traits::Equal Equal;
    typedef typename Traits::Alloc Alloc;
    typedef typename Traits::MyHash MyHash;
    typedef typename Traits::MyEqual MyEqual;
    typedef typename Traits::MyAlloc MyAlloc;
    typename Traits::Clock clock;
    auto const v (Traits::values());

    //testcase (Traits::name() + " range");
    testcase ("range");

    {
        typename Traits::template Cont <Hash, Equal, Alloc> c (
            v.begin(), v.end(),
            clock);
        checkContents (c, v);
    }

    {
        typename Traits::template Cont <MyHash, Equal, Alloc> c (
            v.begin(), v.end(),
            clock, MyHash(1));
        checkContents (c, v);
    }

    {
        typename Traits::template Cont <Hash, MyEqual, Alloc> c (
            v.begin(), v.end(),
            clock, MyEqual (1));
        checkContents (c, v);
    }

    {
        typename Traits::template Cont <Hash, Equal, MyAlloc> c (
            v.begin(), v.end(),
            clock, MyAlloc (1));
        checkContents (c, v);
    }

    {
        typename Traits::template Cont <MyHash, MyEqual, Alloc> c (
            v.begin(), v.end(),
            clock, MyHash(1), MyEqual(1));
        checkContents (c, v);
    }

    {
        typename Traits::template Cont <MyHash, Equal, MyAlloc> c (
            v.begin(), v.end(),
            clock, MyHash(1), MyAlloc(1));
        checkContents (c, v);
    }

    {
        typename Traits::template Cont <Hash, MyEqual, MyAlloc> c (
            v.begin(), v.end(),
            clock, MyEqual(1), MyAlloc(1));
        checkContents (c, v);
    }

    {
        typename Traits::template Cont <MyHash, MyEqual, MyAlloc> c (
            v.begin(), v.end(),
            clock, MyHash(1), MyEqual(1), MyAlloc(1));
        checkContents (c, v);
    }
}

// ordered
template <bool IsUnordered, bool IsMulti, bool IsMap>
typename std::enable_if <! IsUnordered>::type
aged_associative_container_test_base::
testConstructInitList ()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typedef typename Traits::Value Value;
    typedef typename Traits::Key Key;
    typedef typename Traits::T T;
    typedef typename Traits::Dur Dur;
    typedef typename Traits::Comp Comp;
    typedef typename Traits::Alloc Alloc;
    typedef typename Traits::MyComp MyComp;
    typedef typename Traits::MyAlloc MyAlloc;
    typename Traits::Clock clock;

    //testcase (Traits::name() + " init-list");
    testcase ("init-list");

    // VFALCO TODO

    pass();
}

// unordered
template <bool IsUnordered, bool IsMulti, bool IsMap>
typename std::enable_if <IsUnordered>::type
aged_associative_container_test_base::
testConstructInitList ()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typedef typename Traits::Value Value;
    typedef typename Traits::Key Key;
    typedef typename Traits::T T;
    typedef typename Traits::Dur Dur;
    typedef typename Traits::Hash Hash;
    typedef typename Traits::Equal Equal;
    typedef typename Traits::Alloc Alloc;
    typedef typename Traits::MyHash MyHash;
    typedef typename Traits::MyEqual MyEqual;
    typedef typename Traits::MyAlloc MyAlloc;
    typename Traits::Clock clock;

    //testcase (Traits::name() + " init-list");
    testcase ("init-list");

    // VFALCO TODO
    pass();
}

//------------------------------------------------------------------------------
//
// Copy/Move construction and assign
//
//------------------------------------------------------------------------------

template <bool IsUnordered, bool IsMulti, bool IsMap>
void
aged_associative_container_test_base::
testCopyMove ()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typedef typename Traits::Value Value;
    typedef typename Traits::Alloc Alloc;
    typename Traits::Clock clock;
    auto const v (Traits::values());

    //testcase (Traits::name() + " copy/move");
    testcase ("copy/move");

    // copy

    {
        typename Traits::template Cont <> c (
            v.begin(), v.end(), clock);
        typename Traits::template Cont <> c2 (c);
        checkContents (c, v);
        checkContents (c2, v);
        expect (c == c2);
        unexpected (c != c2);
    }

    {
        typename Traits::template Cont <> c (
            v.begin(), v.end(), clock);
        typename Traits::template Cont <> c2 (c, Alloc());
        checkContents (c, v);
        checkContents (c2, v);
        expect (c == c2);
        unexpected (c != c2);
    }

    {
        typename Traits::template Cont <> c (
            v.begin(), v.end(), clock);
        typename Traits::template Cont <> c2 (
            clock);
        c2 = c;
        checkContents (c, v);
        checkContents (c2, v);
        expect (c == c2);
        unexpected (c != c2);
    }

    // move

    {
        typename Traits::template Cont <> c (
            v.begin(), v.end(), clock);
        typename Traits::template Cont <> c2 (
            std::move (c));
        checkContents (c2, v);
    }

    {
        typename Traits::template Cont <> c (
            v.begin(), v.end(), clock);
        typename Traits::template Cont <> c2 (
            std::move (c), Alloc());
        checkContents (c2, v);
    }

    {
        typename Traits::template Cont <> c (
            v.begin(), v.end(), clock);
        typename Traits::template Cont <> c2 (
            clock);
        c2 = std::move (c);
        checkContents (c2, v);
    }
}

//------------------------------------------------------------------------------
//
// Modifiers
//
//------------------------------------------------------------------------------


template <class Container, class Values>
void
aged_associative_container_test_base::
checkInsertCopy (Container& c, Values const& v)
{
    for (auto const& e : v)
        c.insert (e);
    checkContents (c, v);
}

template <class Container, class Values>
void
aged_associative_container_test_base::
checkInsertMove (Container& c, Values const& v)
{
    Values v2 (v);
    for (auto& e : v2)
        c.insert (std::move (e));
    checkContents (c, v);
}

template <class Container, class Values>
void
aged_associative_container_test_base::
checkInsertHintCopy (Container& c, Values const& v)
{
    for (auto const& e : v)
        c.insert (c.cend(), e);
    checkContents (c, v);
}

template <class Container, class Values>
void
aged_associative_container_test_base::
checkInsertHintMove (Container& c, Values const& v)
{
    Values v2 (v);
    for (auto& e : v2)
        c.insert (c.cend(), std::move (e));
    checkContents (c, v);
}

template <class Container, class Values>
void
aged_associative_container_test_base::
checkEmplace (Container& c, Values const& v)
{
    for (auto const& e : v)
        c.emplace (e);
    checkContents (c, v);
}

template <class Container, class Values>
void
aged_associative_container_test_base::
checkEmplaceHint (Container& c, Values const& v)
{
    for (auto const& e : v)
        c.emplace_hint (c.cend(), e);
    checkContents (c, v);
}

template <bool IsUnordered, bool IsMulti, bool IsMap>
void
aged_associative_container_test_base::
testModifiers()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typename Traits::Clock clock;
    auto const v (Traits::values());
    auto const l (make_list (v));

    //testcase (Traits::name() + " modify");
    testcase ("modify");

    {
        typename Traits::template Cont <> c (clock);
        checkInsertCopy (c, v);
    }

    {
        typename Traits::template Cont <> c (clock);
        checkInsertCopy (c, l);
    }

    {
        typename Traits::template Cont <> c (clock);
        checkInsertMove (c, v);
    }

    {
        typename Traits::template Cont <> c (clock);
        checkInsertMove (c, l);
    }

    {
        typename Traits::template Cont <> c (clock);
        checkInsertHintCopy (c, v);
    }

    {
        typename Traits::template Cont <> c (clock);
        checkInsertHintCopy (c, l);
    }

    {
        typename Traits::template Cont <> c (clock);
        checkInsertHintMove (c, v);
    }

    {
        typename Traits::template Cont <> c (clock);
        checkInsertHintMove (c, l);
    }
}

//------------------------------------------------------------------------------
//
// Chronological ordering
//
//------------------------------------------------------------------------------

template <bool IsUnordered, bool IsMulti, bool IsMap>
void
aged_associative_container_test_base::
testChronological ()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typedef typename Traits::Value Value;
    typename Traits::Clock clock;
    auto const v (Traits::values());

    //testcase (Traits::name() + " chronological");
    testcase ("chronological");

    typename Traits::template Cont <> c (
        v.begin(), v.end(), clock);

    expect (std::equal (
        c.chronological.cbegin(), c.chronological.cend(),
            v.begin(), v.end(), equal_value <Traits> ()));

    for (auto iter (v.rbegin()); iter != v.rend(); ++iter)
    {
        auto found (c.find (Traits::extract (*iter)));
        expect (found != c.cend());
        if (found == c.cend())
            return;
        c.touch (found);
    }

    expect (std::equal (
        c.chronological.cbegin(), c.chronological.cend(),
            v.rbegin(), v.rend(), equal_value <Traits> ()));
}

//------------------------------------------------------------------------------
//
// Element creation via operator[]
//
//------------------------------------------------------------------------------

// map, unordered_map
template <bool IsUnordered, bool IsMulti, bool IsMap>
typename std::enable_if <IsMap && ! IsMulti>::type
aged_associative_container_test_base::
testArrayCreate()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typename Traits::Clock clock;
    auto v (Traits::values());

    //testcase (Traits::name() + " array create");
    testcase ("array create");

    {
        // Copy construct key
        typename Traits::template Cont <> c (clock);
        for (auto e : v)
            c [e.first] = e.second;
        checkContents (c, v);
    }

    {
        // Move construct key
        typename Traits::template Cont <> c (clock);
        for (auto e : v)
            c [std::move (e.first)] = e.second;
        checkContents (c, v);
    }
}

//------------------------------------------------------------------------------
//
// Container-wide comparison
//
//------------------------------------------------------------------------------

// ordered
template <bool IsUnordered, bool IsMulti, bool IsMap>
typename std::enable_if <! IsUnordered>::type
aged_associative_container_test_base::
testCompare ()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typedef typename Traits::Value Value;
    typename Traits::Clock clock;
    auto const v (Traits::values());

    //testcase (Traits::name() + " array create");
    testcase ("array create");

    typename Traits::template Cont <> c1 (
        v.begin(), v.end(), clock);

    typename Traits::template Cont <> c2 (
        v.begin(), v.end(), clock);
    c2.erase (c2.cbegin());

    expect      (c1 != c2);
    unexpected  (c1 == c2);
    expect      (c1 <  c2);
    expect      (c1 <= c2);
    unexpected  (c1 >  c2);
    unexpected  (c1 >= c2);
}

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

// ordered
template <bool IsUnordered, bool IsMulti, bool IsMap>
typename std::enable_if <! IsUnordered>::type
aged_associative_container_test_base::
testObservers()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typename Traits::Clock clock;

    //testcase (Traits::name() + " observers");
    testcase ("observers");

    typename Traits::template Cont <> c (clock);
    c.key_comp();
    c.value_comp();

    pass();
}

// unordered
template <bool IsUnordered, bool IsMulti, bool IsMap>
typename std::enable_if <IsUnordered>::type
aged_associative_container_test_base::
testObservers()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;
    typename Traits::Clock clock;

    //testcase (Traits::name() + " observers");
    testcase ("observers");

    typename Traits::template Cont <> c (clock);
    c.hash_function();
    c.key_eq();

    pass();
}

//------------------------------------------------------------------------------
//
// Matrix
//
//------------------------------------------------------------------------------

template <bool IsUnordered, bool IsMulti, bool IsMap>
void
aged_associative_container_test_base::
testMaybeUnorderedMultiMap ()
{
    typedef TestTraits <IsUnordered, IsMulti, IsMap> Traits;

    testConstructEmpty      <IsUnordered, IsMulti, IsMap> ();
    testConstructRange      <IsUnordered, IsMulti, IsMap> ();
    testConstructInitList   <IsUnordered, IsMulti, IsMap> ();
    testCopyMove            <IsUnordered, IsMulti, IsMap> ();
    testModifiers           <IsUnordered, IsMulti, IsMap> ();
    testChronological       <IsUnordered, IsMulti, IsMap> ();
    testArrayCreate         <IsUnordered, IsMulti, IsMap> ();
    testCompare             <IsUnordered, IsMulti, IsMap> ();
    testObservers           <IsUnordered, IsMulti, IsMap> ();
}

//------------------------------------------------------------------------------

class aged_set_test : public aged_associative_container_test_base
{
public:
    // Compile time checks

    typedef std::string Key;
    typedef int T;

    static_assert (std::is_same <
        aged_set <Key>,
        detail::aged_ordered_container <false, false, Key, void>>::value,
            "bad alias: aged_set");

    static_assert (std::is_same <
        aged_multiset <Key>,
        detail::aged_ordered_container <true, false, Key, void>>::value,
            "bad alias: aged_multiset");

    static_assert (std::is_same <
        aged_map <Key, T>,
        detail::aged_ordered_container <false, true, Key, T>>::value,
            "bad alias: aged_map");

    static_assert (std::is_same <
        aged_multimap <Key, T>,
        detail::aged_ordered_container <true, true, Key, T>>::value,
            "bad alias: aged_multimap");

    static_assert (std::is_same <
        aged_unordered_set <Key>,
        detail::aged_unordered_container <false, false, Key, void>>::value,
            "bad alias: aged_unordered_set");

    static_assert (std::is_same <
        aged_unordered_multiset <Key>,
        detail::aged_unordered_container <true, false, Key, void>>::value,
            "bad alias: aged_unordered_multiset");

    static_assert (std::is_same <
        aged_unordered_map <Key, T>,
        detail::aged_unordered_container <false, true, Key, T>>::value,
            "bad alias: aged_unordered_map");

    static_assert (std::is_same <
        aged_unordered_multimap <Key, T>,
        detail::aged_unordered_container <true, true, Key, T>>::value,
            "bad alias: aged_unordered_multimap");

    void run ()
    {
        testMaybeUnorderedMultiMap <false, false, false>();
    }
};

class aged_map_test : public aged_associative_container_test_base
{
public:
    void run ()
    {
        testMaybeUnorderedMultiMap <false, false, true>();
    }
};

class aged_multiset_test : public aged_associative_container_test_base
{
public:
    void run ()
    {
        testMaybeUnorderedMultiMap <false, true, false>();
    }
};

class aged_multimap_test : public aged_associative_container_test_base
{
public:
    void run ()
    {
        testMaybeUnorderedMultiMap <false, true, true>();
    }
};


class aged_unordered_set_test : public aged_associative_container_test_base
{
public:
    void run ()
    {
        testMaybeUnorderedMultiMap <true, false, false>();
    }
};

class aged_unordered_map_test : public aged_associative_container_test_base
{
public:
    void run ()
    {
        testMaybeUnorderedMultiMap <true, false, true>();
    }
};

class aged_unordered_multiset_test : public aged_associative_container_test_base
{
public:
    void run ()
    {
        testMaybeUnorderedMultiMap <true, true, false>();
    }
};

class aged_unordered_multimap_test : public aged_associative_container_test_base
{
public:
    void run ()
    {
        testMaybeUnorderedMultiMap <true, true, true>();
    }
};

BEAST_DEFINE_TESTSUITE(aged_set,container,beast);
BEAST_DEFINE_TESTSUITE(aged_map,container,beast);
BEAST_DEFINE_TESTSUITE(aged_multiset,container,beast);
BEAST_DEFINE_TESTSUITE(aged_multimap,container,beast);
BEAST_DEFINE_TESTSUITE(aged_unordered_set,container,beast);
BEAST_DEFINE_TESTSUITE(aged_unordered_map,container,beast);
BEAST_DEFINE_TESTSUITE(aged_unordered_multiset,container,beast);
BEAST_DEFINE_TESTSUITE(aged_unordered_multimap,container,beast);

}
