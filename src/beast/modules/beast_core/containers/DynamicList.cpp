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

namespace ContainerTests
{

//------------------------------------------------------------------------------

/** Counts the number of occurrences of each type of operation. */
class Counts
{
public:
    typedef std::size_t count_type;

    Counts ()
        : default_ctor (0)
        , copy_ctor    (0)
        , copy_assign  (0)
    #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
        , move_ctor    (0)
        , move_assign  (0)
    #endif
    {
    }

    Counts (Counts const& other)
        : default_ctor (other.default_ctor)
        , copy_ctor    (other.copy_ctor)
        , copy_assign  (other.copy_assign)
    #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
        , move_ctor    (other.move_ctor)
        , move_assign  (other.move_assign)
    #endif
    {
    }

    Counts& operator= (Counts const& other)
    {
        default_ctor = other.default_ctor;
        copy_ctor    = other.copy_ctor;
        copy_assign  = other.copy_assign;
    #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
        move_ctor    = other.move_ctor;
        move_assign  = other.move_assign;
    #endif
        return *this;
    }

    friend inline Counts operator- (Counts const& lhs, Counts const& rhs)
    {
        Counts result;
        result.default_ctor = lhs.default_ctor  - rhs.default_ctor;
        result.copy_ctor    = lhs.copy_ctor     - rhs.copy_ctor;
        result.copy_assign  = lhs.copy_assign   - rhs.copy_assign;
    #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
        result.move_ctor    = lhs.move_ctor     - rhs.move_ctor;
        result.move_assign  = lhs.move_assign   - rhs.move_assign;
    #endif
        return result;
    }

    String toString () const
    {
        return String()
            + "default_ctor(" +    String::fromNumber (default_ctor) + ") "
            + ", copy_ctor(" +   String::fromNumber (copy_ctor) + ")"
            + ", copy_assign(" + String::fromNumber (copy_assign) + ")"
    #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
            + ", move_ctor(" +   String::fromNumber (move_ctor) + ")"
            + ", move_assign(" + String::fromNumber (move_assign) + ")"
    #endif
            ;
    }

    count_type default_ctor;
    count_type copy_ctor;
    count_type copy_assign;
#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    count_type move_ctor;
    count_type move_assign;
#endif
};

//------------------------------------------------------------------------------

/** Counts the number of element operations performed in a scope. */
class ScopedCounts : public Uncopyable
{
public:
    ScopedCounts (Counts const& counts)
        : m_start (counts)
        , m_counts (counts)
    {
    }

    Counts get () const
    {
        return m_counts - m_start;
    }

private:
    Counts const m_start;
    Counts const& m_counts;
};

//------------------------------------------------------------------------------

/* Base for element configurations. */
class ElementConfigBase : public Uncopyable
{
public:
    typedef std::size_t IdType;
};

/** Provides the element-specific configuration members. */
template <class Params>
class ElementConfig : public ElementConfigBase
{
public:
    static Counts& getCounts ()
    {
        static Counts counts;
        return counts;
    }
};

//------------------------------------------------------------------------------

/** Base for elements used in container unit tests. */
class ElementBase
{
public:
};

/** An object placed into a container for unit testing. */
template <class Config>
class Element : public ElementBase
{
public:
    typedef typename Config::IdType   IdType;

    Element ()
        : m_id (0)
        , m_msg (String::empty)
    {
        ++Config::getCounts ().default_ctor;
    }

    explicit Element (IdType id)
        : m_id (id)
        , m_msg (String::fromNumber (id))
    {
    }

    Element (Element const& other)
        : m_id (other.m_id)
        , m_msg (other.m_msg)
    {
        ++Config::getCounts ().copy_ctor;
    }

    Element& operator= (Element const& other)
    {
        m_id = other.m_id;
        m_msg = other.m_msg;
        ++Config::getCounts ().copy_assign;
    }

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    Element (Element&& other)
        : m_id (other.m_id)
        , m_msg (other.m_msg)
    {
        other.m_msg = String::empty;
        ++Config::getCounts ().move_ctor;
    }

    Element& operator= (Element&& other)
    {
        m_id = other.m_id;
        m_msg = other.m_msg;
        other.m_msg = String::empty;
        ++Config::getCounts ().move_assign;
    }
#endif

    IdType id () const
    {
        return m_id;
    }

    String msg () const
    {
        return m_msg;
    }

private:
    IdType m_id;
    String m_msg;
};

//------------------------------------------------------------------------------

/** Base for test state parameters. */
class StateBase : public Uncopyable
{
public:
    typedef int64 SeedType;
};

/** Provides configuration-specific test state parameters. */
template <class Params>
class State : public StateBase
{
public:
    static Random& random ()
    {
        static Random generator (Params::seedValue);
        return generator;
    }
};

//------------------------------------------------------------------------------

class ConfigBase
{
};

template <template <typename, class> class Container,
          class Params>
class Config
    : public State <Params>
    , public ElementConfig <Params>
{
public:
    typedef Config                              ConfigType;
    typedef Params                              ParamsType;
    typedef State <Params>                      StateType;
    typedef ElementConfig <Params>              ElementConfigType;
    typedef Element <ElementConfigType>         ElementType;
    typedef Container <
        ElementType,
        std::allocator <char> >                 ContainerType;

    typedef StateBase::SeedType SeedType;

    // default seed
    static SeedType const seedValue = 69;
};

//------------------------------------------------------------------------------

/** A generic container test. */
template <class Params>
class Test : public Params
{
public:
    typedef typename Params::StateType          StateType;
    typedef typename Params::ElementConfigType  ElementConfigType;
    typedef typename Params::ContainerType      ContainerType;

    void doInsert ()
    {
        m_container.reserve (Params::elementCount);
        for (typename ContainerType::size_type i = 0;
            i < Params::elementCount; ++i)
        {
            m_container.push_back (typename Params::ElementType ());
        }
    }

private:
    typename Params::ContainerType m_container;
};

//------------------------------------------------------------------------------

class DynamicListTests : public UnitTest
{
public:
    struct Params : Config <DynamicList, Params>
    {
        static SeedType const seedValue = 42;
        static ContainerType::size_type const elementCount = 100 * 1000;
    };

    typedef Test <Params> TestType;

    void runTest ()
    {
        TestType test;

        beginTestCase ("insert");

        {
            ScopedCounts counts (TestType::getCounts ());
            test.doInsert ();
            this->logMessage (counts.get ().toString ());
        }

        pass ();
    }

    DynamicListTests () : UnitTest ("DynamicList", "beast")
    {
    }
};

static DynamicListTests dynamicListTests;

}
