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

#ifndef RIPPLE_PROTOCOL_STOBJECT_H_INCLUDED
#define RIPPLE_PROTOCOL_STOBJECT_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STPathSet.h>
#include <ripple/protocol/STVector256.h>
#include <ripple/protocol/SOTemplate.h>
#include <ripple/protocol/impl/STVar.h>
#include <boost/iterator/transform_iterator.hpp>
#include <utility>

#include <beast/streams/debug_ostream.h>
#include <beast/utility/static_initializer.h>
#include <mutex>
#include <unordered_map>

namespace ripple {

class STArray;

class STObject
    : public STBase
    , public CountedObject <STObject>
{
private:
    struct Transform
    {
        using argument_type = detail::STVar;
        using result_type = STBase;

        STBase const&
        operator() (detail::STVar const& e) const
        {
            return e.get();
        }
    };

    enum
    {
        reserveSize = 20
    };

    struct Log
    {
        std::mutex mutex_;
        std::unordered_map<
            std::size_t, std::size_t> map_;

        ~Log()
        {
            beast::debug_ostream os;
            for(auto const& e : map_)
                os << e.first << "," << e.second;
        }

        void
        operator() (std::size_t n)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto const result = map_.emplace(n, 1);
            if (! result.second)
                ++result.first->second;
        }
    };

    using list_type = std::vector<detail::STVar>;

    list_type v_;
    SOTemplate const* mType;

public:
    using iterator = boost::transform_iterator<
        Transform, STObject::list_type::const_iterator>;

    static char const* getCountedObjectName () { return "STObject"; }

    STObject(STObject&&);
    STObject(STObject const&) = default;
    STObject (const SOTemplate & type, SField const& name);
    STObject (const SOTemplate & type, SerialIter & sit, SField const& name);
    STObject (SerialIter& sit, SField const& name);
    STObject& operator= (STObject const&) = default;
    STObject& operator= (STObject&& other);

    explicit STObject (SField const& name);

    virtual ~STObject();

    STBase*
    copy (std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move (std::size_t n, void* buf) override
    {
        return emplace(n, buf, std::move(*this));
    }

    iterator begin() const
    {
        return iterator(v_.begin());
    }

    iterator end() const
    {
        return iterator(v_.end());
    }

    bool empty() const
    {
        return v_.empty();
    }

    bool setType (const SOTemplate & type);
    bool isValidForType ();
    bool isFieldAllowed (SField const&);
    bool isFree () const
    {
        return mType == nullptr;
    }

    void set (const SOTemplate&);
    bool set (SerialIter& u, int depth = 0);

    virtual SerializedTypeID getSType () const override
    {
        return STI_OBJECT;
    }
    virtual bool isEquivalent (const STBase & t) const override;
    virtual bool isDefault () const override
    {
        return v_.empty();
    }

    virtual void add (Serializer & s) const override
    {
        add (s, true);    // just inner elements
    }

    void add (Serializer & s, bool withSignature) const;

    // VFALCO NOTE does this return an expensive copy of an object with a
    //             dynamic buffer?
    // VFALCO TODO Remove this function and fix the few callers.
    Serializer getSerializer () const
    {
        Serializer s;
        add (s);
        return s;
    }

    virtual std::string getFullText () const override;
    virtual std::string getText () const override;

    // TODO(tom): options should be an enum.
    virtual Json::Value getJson (int options) const override;

    template <class... Args>
    std::size_t
    emplace_back(Args&&... args)
    {
        v_.emplace_back(std::forward<Args>(args)...);
        return v_.size() - 1;
    }

    int getCount () const
    {
        return v_.size ();
    }

    bool setFlag (std::uint32_t);
    bool clearFlag (std::uint32_t);
    bool isFlag(std::uint32_t) const;
    std::uint32_t getFlags () const;

    uint256 getHash (std::uint32_t prefix) const;
    uint256 getSigningHash (std::uint32_t prefix) const;

    const STBase& peekAtIndex (int offset) const
    {
        return v_[offset].get();
    }
    STBase& getIndex(int offset)
    {
        return v_[offset].get();
    }
    const STBase* peekAtPIndex (int offset) const
    {
        return &v_[offset].get();
    }
    STBase* getPIndex (int offset)
    {
        return &v_[offset].get();
    }

    int getFieldIndex (SField const& field) const;
    SField const& getFieldSType (int index) const;

    const STBase& peekAtField (SField const& field) const;
    STBase& getField (SField const& field);
    const STBase* peekAtPField (SField const& field) const;
    STBase* getPField (SField const& field, bool createOkay = false);

    // these throw if the field type doesn't match, or return default values
    // if the field is optional but not present
    std::string getFieldString (SField const& field) const;
    unsigned char getFieldU8 (SField const& field) const;
    std::uint16_t getFieldU16 (SField const& field) const;
    std::uint32_t getFieldU32 (SField const& field) const;
    std::uint64_t getFieldU64 (SField const& field) const;
    uint128 getFieldH128 (SField const& field) const;

    uint160 getFieldH160 (SField const& field) const;
    uint256 getFieldH256 (SField const& field) const;
    RippleAddress getFieldAccount (SField const& field) const;
    Account getFieldAccount160 (SField const& field) const;

    Blob getFieldVL (SField const& field) const;
    STAmount const& getFieldAmount (SField const& field) const;
    STPathSet const& getFieldPathSet (SField const& field) const;
    const STVector256& getFieldV256 (SField const& field) const;
    const STArray& getFieldArray (SField const& field) const;

    /** Set a field.
        if the field already exists, it is replaced.
    */
    void
    set (std::unique_ptr<STBase> v);

    void setFieldU8 (SField const& field, unsigned char);
    void setFieldU16 (SField const& field, std::uint16_t);
    void setFieldU32 (SField const& field, std::uint32_t);
    void setFieldU64 (SField const& field, std::uint64_t);
    void setFieldH128 (SField const& field, uint128 const&);
    void setFieldH256 (SField const& field, uint256 const& );
    void setFieldVL (SField const& field, Blob const&);
    void setFieldAccount (SField const& field, Account const&);
    void setFieldAccount (SField const& field, RippleAddress const& addr)
    {
        setFieldAccount (field, addr.getAccountID ());
    }
    void setFieldAmount (SField const& field, STAmount const&);
    void setFieldPathSet (SField const& field, STPathSet const&);
    void setFieldV256 (SField const& field, STVector256 const& v);
    void setFieldArray (SField const& field, STArray const& v);

    template <class Tag>
    void setFieldH160 (SField const& field, base_uint<160, Tag> const& v)
    {
        STBase* rf = getPField (field, true);

        if (!rf)
            throw std::runtime_error ("Field not found");

        if (rf->getSType () == STI_NOTPRESENT)
            rf = makeFieldPresent (field);

        using Bits = STBitString<160>;
        if (auto cf = dynamic_cast<Bits*> (rf))
            cf->setValue (v);
        else
            throw std::runtime_error ("Wrong field type");
    }

    STObject& peekFieldObject (SField const& field);

    bool isFieldPresent (SField const& field) const;
    STBase* makeFieldPresent (SField const& field);
    void makeFieldAbsent (SField const& field);
    bool delField (SField const& field);
    void delField (int index);

    bool hasMatchingEntry (const STBase&);

    bool operator== (const STObject & o) const;
    bool operator!= (const STObject & o) const
    {
        return ! (*this == o);
    }

private:
    // Implementation for getting (most) fields that return by value.
    //
    // The remove_cv and remove_reference are necessitated by the STBitString
    // types.  Their getValue returns by const ref.  We return those types
    // by value.
    template <typename T, typename V =
        typename std::remove_cv < typename std::remove_reference <
            decltype (std::declval <T> ().getValue ())>::type >::type >
    V getFieldByValue (SField const& field) const
    {
        const STBase* rf = peekAtPField (field);

        if (!rf)
            throw std::runtime_error ("Field not found");

        SerializedTypeID id = rf->getSType ();

        if (id == STI_NOTPRESENT)
            return V (); // optional field not present

        const T* cf = dynamic_cast<const T*> (rf);

        if (!cf)
            throw std::runtime_error ("Wrong field type");

        return cf->getValue ();
    }

    // Implementations for getting (most) fields that return by const reference.
    //
    // If an absent optional field is deserialized we don't have anything
    // obvious to return.  So we insist on having the call provide an
    // 'empty' value we return in that circumstance.
    template <typename T, typename V>
    V const& getFieldByConstRef (SField const& field, V const& empty) const
    {
        const STBase* rf = peekAtPField (field);

        if (!rf)
            throw std::runtime_error ("Field not found");

        SerializedTypeID id = rf->getSType ();

        if (id == STI_NOTPRESENT)
            return empty; // optional field not present

        const T* cf = dynamic_cast<const T*> (rf);

        if (!cf)
            throw std::runtime_error ("Wrong field type");

        return *cf;
    }

    // Implementation for setting most fields with a setValue() method.
    template <typename T, typename V>
    void setFieldUsingSetValue (SField const& field, V value)
    {
        static_assert(!std::is_lvalue_reference<V>::value, "");

        STBase* rf = getPField (field, true);

        if (!rf)
            throw std::runtime_error ("Field not found");

        if (rf->getSType () == STI_NOTPRESENT)
            rf = makeFieldPresent (field);

        T* cf = dynamic_cast<T*> (rf);

        if (!cf)
            throw std::runtime_error ("Wrong field type");

        cf->setValue (std::move (value));
    }

    // Implementation for setting fields using assignment
    template <typename T>
    void setFieldUsingAssignment (SField const& field, T const& value)
    {
        STBase* rf = getPField (field, true);

        if (!rf)
            throw std::runtime_error ("Field not found");

        if (rf->getSType () == STI_NOTPRESENT)
            rf = makeFieldPresent (field);

        T* cf = dynamic_cast<T*> (rf);

        if (!cf)
            throw std::runtime_error ("Wrong field type");

        (*cf) = value;
    }
};

} // ripple

#endif
