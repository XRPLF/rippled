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

#ifndef RIPPLE_PROTOCOL_KNOWNFORMATS_H_INCLUDED
#define RIPPLE_PROTOCOL_KNOWNFORMATS_H_INCLUDED

#include <ripple/basics/contract.h>
#include <ripple/protocol/SOTemplate.h>
#include <memory>

namespace ripple {

/** Manages a list of known formats.

    Each format has a name, an associated KeyType (typically an enumeration),
    and a predefined @ref SOElement.

    @tparam KeyType The type of key identifying the format.
*/
template <class KeyType>
class KnownFormats
{
public:
    /** A known format.
    */
    class Item
    {
    public:
        Item (char const* name, KeyType type)
            : m_name (name)
            , m_type (type)
        {
        }

        Item& operator<< (SOElement const& el)
        {
            elements.push_back (el);

            return *this;
        }

        /** Retrieve the name of the format.
        */
        std::string const& getName () const noexcept
        {
            return m_name;
        }

        /** Retrieve the transaction type this format represents.
        */
        KeyType getType () const noexcept
        {
            return m_type;
        }

    public:
        // VFALCO TODO make an accessor for this
        SOTemplate elements;

    private:
        std::string const m_name;
        KeyType const m_type;
    };

private:
    using NameMap = std::map <std::string, Item*>;
    using TypeMap = std::map <KeyType, Item*>;

public:
    /** Create the known formats object.

        Derived classes will load the object will all the known formats.
    */
    KnownFormats () = default;

    /** Destroy the known formats object.

        The defined formats are deleted.
    */
    virtual ~KnownFormats () = default;

    /** Retrieve the type for a format specified by name.

        If the format name is unknown, an exception is thrown.

        @param  name The name of the type.
        @return      The type.
    */
    KeyType findTypeByName (std::string const name) const
    {
        Item const* const result = findByName (name);

        if (result != nullptr)
            return result->getType ();
        Throw<std::runtime_error> ("Unknown format name");
        return {}; // Silence compiler warning.
    }

    /** Retrieve a format based on its type.
    */
    // VFALCO TODO Can we just return the SOElement& ?
    Item const* findByType (KeyType type) const noexcept
    {
        Item* result = nullptr;

        typename TypeMap::const_iterator const iter = m_types.find (type);

        if (iter != m_types.end ())
        {
            result = iter->second;
        }

        return result;
    }

protected:
    /** Retrieve a format based on its name.
    */
    Item const* findByName (std::string const& name) const noexcept
    {
        Item* result = nullptr;

        typename NameMap::const_iterator const iter = m_names.find (name);

        if (iter != m_names.end ())
        {
            result = iter->second;
        }

        return result;
    }

    /** Add a new format.

        The new format has the set of common fields already added.

        @param name The name of this format.
        @param type The type of this format.

        @return The created format.
    */
    Item& add (char const* name, KeyType type)
    {
        m_formats.emplace_back (
            std::make_unique <Item> (name, type));
        auto& item (*m_formats.back());

        addCommonFields (item);

        m_types [item.getType ()] = &item;
        m_names [item.getName ()] = &item;

        return item;
    }

    /** Adds common fields.

        This is called for every new item.
    */
    virtual void addCommonFields (Item& item) = 0;

private:
    KnownFormats(KnownFormats const&) = delete;
    KnownFormats& operator=(KnownFormats const&) = delete;

    std::vector <std::unique_ptr <Item>> m_formats;
    NameMap m_names;
    TypeMap m_types;
};

} // ripple

#endif
