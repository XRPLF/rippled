//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_SMARTPTR_ABSTRACTOBJECT_H_INCLUDED
#define BEAST_SMARTPTR_ABSTRACTOBJECT_H_INCLUDED

#include <list>
#include <memory>
#include <stdexcept>
#include <typeinfo>
#include "../Atomic.h"
#include "../Config.h"
#include "../Uncopyable.h"
#include "../intrusive/LockFreeStack.h"

namespace beast {
namespace abstract {

/** Base for all abstract interfaces. */
class BasicInterface
{
public:
    virtual ~BasicInterface() { }

    /** Returns the unique ID of this interface type.
        The ID must be the same for all instances of the
        derived interface.
    */
    virtual std::size_t id () const = 0;

    /** Returns the unique ID associated with the Derived type. */
    template <typename Derived>
    static std::size_t type_id ()
    {
        static std::size_t const value (next_id ());
        return value;
    }

private:
    // Returns a new unique id
    static std::size_t next_id ()
    {
        static Atomic <std::size_t> value;
        return ++value;
    }
};

//------------------------------------------------------------------------------

/** Base for a derived interface. */
template <typename Derived>
class Interface : public BasicInterface
{
public:
    // Returns the unique ID for all instances of Derived
    std::size_t id () const
    {
        return BasicInterface::type_id <Derived> ();
    }
};

//------------------------------------------------------------------------------

/** Factory for producing interfaces on a specific object. */
template <typename Object>
class Factory
{
public:
    class Callback;

private:
    struct Item;

    typedef LockFreeStack <Item> Items;

    struct Item : Items::Node
    {
        Item (Callback& owner_)
         : owner (owner_)
            { }
        Callback& owner;
    };

    Items m_items;

public:
    /** Base for hooking object creation. */
    class Callback
    {
    public:
        /** Create the callback and insert it into the factory. */
        explicit Callback (Factory& factory)
            : m_item (*this)
        {
            factory.m_items.push_front (&m_item);
        }

        /** Called when Object is created.
            Object must be fully constructed. The order of calls to callbacks
            is not defined.
        */
        virtual void create_interfaces (Object& object) = 0;

    private:
        Item m_item;
    };

    /** Invokes the callbacks for an instance of Object.
        This must be called after the object is fully constructed, for example
        as the last statement in the constructor.
    */
    void create_interfaces (Object& object)
    {
        for (typename Items::iterator iter (m_items.begin());
            iter != m_items.end(); ++iter)
            iter->owner.create_interfaces (object);
    }
};

//------------------------------------------------------------------------------

/** A container of polymorphic interfaces.
    The Object type associated with the container is used to gain access
    to the corresponding factory.
*/
template <typename Object>
class Interfaces : public Uncopyable
{
public:
    Interfaces ()
    {
    }

    /** Returns a reference to the specified interface.
        The interface must exist in the container or an exception is thrown.
        Requirements:
            Derived must be a subclass of Interface
    */
    /** @{ */
    template <typename Derived>
    Derived& get_interface()
    {
        Derived* derived (find_interface <Derived> ());
        if (derived == nullptr)
            throw std::bad_cast ();
        return *derived;
    }

    template <typename Derived>
    Derived const& get_interface() const
    {
        Derived const* derived (find_interface <Derived> ());
        if (derived == nullptr)
            throw std::bad_cast ();
        return *derived;
    }
    /** @} */

    /** Returns a pointer to the specified interface.
        If the interface does not exist, `nullptr` is returned.
        Requirements:
            Derived must be a subclass of Interface
    */
    /** @{ */
    template <typename Derived>
    Derived* find_interface()
    {
        std::size_t const id = BasicInterface::type_id <Derived> ();

        for (typename Set::iterator iter (m_set.begin());
            iter != m_set.end(); ++iter)
        {
            if ((*iter)->id() == id)
            {
                Derived* const derived (
                    dynamic_cast <Derived*> (iter->get()));
                check_postcondition (derived != nullptr);
                return derived;
            }
        }
        return nullptr;
    }

    template <typename Derived>
    Derived const* find_interface() const
    {
        std::size_t const id = BasicInterface::type_id <Derived> ();

        for (typename Set::const_iterator iter (m_set.begin());
            iter != m_set.end(); ++iter)
        {
            if ((*iter)->id() == id)
            {
                Derived const* const derived (
                    dynamic_cast <Derived const*> (iter->get()));
                check_postcondition (derived != nullptr);
                return derived;
            }
        }
        return nullptr;
    }

    template <typename Derived>
    bool has_interface() const
    {
        return find_interface <Derived> () != nullptr;
    }
    /** @} */

    /** Adds the specified interface to the container.
        Ownership of the object, which must be allocated via operator new,
        is transferred to the container. If the interface already exists,
        an exception is thrown.
        Requirements:
            Derived must be a subclass of Interface
    */
    template <typename Derived>
    void add_interface (Derived* derived)
    {
        std::unique_ptr <BasicInterface> base_interface (
            derived);
        if (has_interface <Derived> ())
            throw std::invalid_argument ("non-unique");
        m_set.emplace_back (base_interface.release ());
    }

private:
    typedef std::list <std::unique_ptr <BasicInterface>> Set;
    Set m_set;
};

}
}

#endif
