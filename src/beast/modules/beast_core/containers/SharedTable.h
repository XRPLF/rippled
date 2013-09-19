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

#ifndef BEAST_SHAREDTABLE_H_INCLUDED
#define BEAST_SHAREDTABLE_H_INCLUDED

/** Handle to a reference counted fixed size table.

    @note Currently, ElementType must be an aggregate of POD.

    @tparam ElementType The type of element.

    @ingroup beast_basics
*/
template <class ElementType>
class SharedTable
{
public:
    typedef ElementType Entry;

    static SharedTable <ElementType> const null;

    /** Creates a null table.
    */
    SharedTable ()
    {
    }

    /** Creates a table with the specified number of entries.

        The entries are uninitialized.

        @param numEntries The number of entries in the table.

        @todo Initialize the data if ElementType is not POD.
    */
    explicit SharedTable (int numEntries)
        : m_data (new Data (numEntries))
    {
    }

    /** Creates a shared reference to another table.
    */
    SharedTable (SharedTable const& other)
        : m_data (other.m_data)
    {
    }

    /** Makes this table refer to another table.
    */
    SharedTable& operator= (SharedTable const& other)
    {
        m_data = other.m_data;
        return *this;
    }

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    SharedTable (SharedTable&& other) noexcept
        : m_data (static_cast < typename Data::Ptr&& > (other.m_data))
    {
    }

    SharedTable& operator= (SharedTable && other) noexcept
    {
        m_data = static_cast < typename Data::Ptr && > (other.m_data);
        return *this;
    }
#endif

    /** Destructor.
    */
    ~SharedTable ()
    {
    }

    /** Returns true if the two tables share the same set of entries.
    */
    bool operator== (SharedTable const& other) const noexcept
    {
        return m_data == other.m_data;
    }

    /** Returns true if the two tables do not share the same set of entries.
    */
    bool operator!= (SharedTable const& other) const noexcept
    {
        return m_data != other.m_data;
    }

    /** Returns true if the table is not null.
    */
    inline bool isValid () const noexcept
    {
        return m_data != nullptr;
    }

    /** Returns true if the table is null.
    */
    inline bool isNull () const noexcept
    {
        return m_data == nullptr;
    }

    /** Returns the number of tables referring to the same shared entries.
    */
    int getReferenceCount () const noexcept
    {
        return m_data == nullptr ? 0 : m_data->getReferenceCount ();
    }

    /** Create a physical duplicate of the table.
    */
    SharedTable createCopy () const
    {
        return SharedTable (m_data != nullptr ? m_data->clone () : nullptr);
    }

    /** Makes sure no other tables share the same entries as this table.
    */
    void duplicateIfShared ()
    {
        if (m_data != nullptr && m_data->getReferenceCount () > 1)
            m_data = m_data->clone ();
    }

    /** Return the number of entries in this table.
    */
    inline int getNumEntries () const noexcept
    {
        return m_data->getNumEntries ();
    }

    /** Retrieve a table entry.

        @param index The index of the entry, from 0 to getNumEntries ().
    */
    inline ElementType& operator [] (int index) const noexcept
    {
        return m_data->getReference (index);
    }

private:
    class Data : public SharedObject
    {
    public:
        typedef SharedPtr <Data> Ptr;

        explicit Data (int numEntries)
            : m_numEntries (numEntries)
            , m_table (numEntries)
        {
        }

        inline Data* clone () const
        {
            Data* data = new Data (m_numEntries);

            memcpy (
                data->m_table.getData (),
                m_table.getData (),
                m_numEntries * sizeof (ElementType));

            return data;
        }

        inline int getNumEntries () const
        {
            return m_numEntries;
        }

        inline ElementType& getReference (int index) const
        {
            bassert (index >= 0 && index < m_numEntries);
            return m_table [index];
        }

    private:
        int const m_numEntries;
        HeapBlock <ElementType> const m_table;
    };

    explicit SharedTable (Data* data)
        : m_data (data)
    {
    }

    SharedPtr <Data> m_data;
};

template <class ElementType>
SharedTable <ElementType> const SharedTable <ElementType>::null;

#endif

//------------------------------------------------------------------------------

