//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_CONDITIONS_DERCHOICE_H_INCLUDED
#define RIPPLE_TEST_CONDITIONS_DERCHOICE_H_INCLUDED

#include <ripple/conditions/impl/Der.h>

#include <algorithm>
#include <fstream>
#include <string>

#include <boost/filesystem.hpp>

namespace ripple {

namespace test {

struct DerChoiceBaseClass
{
    virtual ~DerChoiceBaseClass() = default;

    virtual std::uint8_t
    type() const = 0;

    virtual
    void
    encode(cryptoconditions::der::Encoder& s) const = 0;

    virtual
    void
    decode(cryptoconditions::der::Decoder& s) = 0;

    virtual int
    compare(
        DerChoiceBaseClass const& rhs,
        cryptoconditions::der::TraitsCache& traitsCache) const = 0;

    virtual
    std::uint64_t
    derEncodedLength(
        boost::optional<cryptoconditions::der::GroupType> const& parentGroupType,
        cryptoconditions::der::TagMode encoderTagMode,
        cryptoconditions::der::TraitsCache& traitsCache) const = 0;

    // for debugging
    virtual
    void
    print(std::ostream& ostr, bool ordered=false) const = 0;
};

bool
equal(DerChoiceBaseClass const* lhs, DerChoiceBaseClass const* rhs);

inline
bool
equal(
    std::unique_ptr<DerChoiceBaseClass> const& lhs,
    std::unique_ptr<DerChoiceBaseClass> const& rhs)
{
    return equal(lhs.get(), rhs.get());
}

inline
bool
operator==(
    std::unique_ptr<DerChoiceBaseClass> const& lhs,
    std::unique_ptr<DerChoiceBaseClass> const& rhs)
{
    return equal(lhs, rhs);
}

inline
bool
operator!=(
    std::unique_ptr<DerChoiceBaseClass> const& lhs,
    std::unique_ptr<DerChoiceBaseClass> const& rhs)
{
    return !operator==(lhs, rhs);
}

struct DerChoiceDerived1 : DerChoiceBaseClass
{
    Buffer buf_;
    std::vector<std::unique_ptr<DerChoiceBaseClass>> subChoices_;
    std::int32_t signedInt_;

    DerChoiceDerived1() = default;

    DerChoiceDerived1(
        std::vector<char> const& b,
        std::vector<std::unique_ptr<DerChoiceBaseClass>> sub,
        std::int32_t si);

    template<class F>
    void withTuple(F&& f, cryptoconditions::der::TraitsCache& traitsCache);

    template<class F>
    void withTuple(F&& f, cryptoconditions::der::TraitsCache& traitsCache) const
    {
        const_cast<DerChoiceDerived1*>(this)->withTuple(std::forward<F>(f), traitsCache);
    }

    std::uint8_t
    type() const override;

    std::uint64_t
    derEncodedLength(
        boost::optional<cryptoconditions::der::GroupType> const& parentGroupType,
        cryptoconditions::der::TagMode encoderTagMode,
        cryptoconditions::der::TraitsCache& traitsCache) const override;

    void
    encode(cryptoconditions::der::Encoder& encoder) const override;

    void
    decode(cryptoconditions::der::Decoder& decoder) override;

    int
    compare(
        DerChoiceBaseClass const& rhs,
        cryptoconditions::der::TraitsCache& traitsCache) const override;

    void
    print(std::ostream& ostr, bool ordered=false) const override;

    friend
    bool
    operator==(DerChoiceDerived1 const& lhs, DerChoiceDerived1 const& rhs);

    friend
    bool
    operator!=(DerChoiceDerived1 const& lhs, DerChoiceDerived1 const& rhs);
};

struct DerChoiceDerived2 : DerChoiceBaseClass
{
    std::string name_;
    std::uint64_t id_;

    DerChoiceDerived2() = default;

    DerChoiceDerived2(std::string const& n, std::uint64_t i);

    template <class F>
    void
    withTuple(F&& f, cryptoconditions::der::TraitsCache& traitsCache);

    template <class F>
    void
    withTuple(F&& f, cryptoconditions::der::TraitsCache& traitsCache) const
    {
        const_cast<DerChoiceDerived2*>(this)->withTuple(std::forward<F>(f), traitsCache);
    }

    std::uint8_t
    type() const override;

    std::uint64_t
    derEncodedLength(
        boost::optional<cryptoconditions::der::GroupType> const& parentGroupType,
        cryptoconditions::der::TagMode encoderTagMode,
        cryptoconditions::der::TraitsCache& traitsCache) const override;

    void
    encode(cryptoconditions::der::Encoder& encoder) const override;

    void
    decode(cryptoconditions::der::Decoder& decoder) override;

    int
    compare(
        DerChoiceBaseClass const& rhs,
        cryptoconditions::der::TraitsCache& traitsCache) const override;

    void
    print(std::ostream& ostr, bool ordered=false) const override;

    friend
    bool
    operator==(DerChoiceDerived2 const& lhs, DerChoiceDerived2 const& rhs);

    friend
    bool
    operator!=(DerChoiceDerived2 const& lhs, DerChoiceDerived2 const& rhs);
};

struct DerChoiceDerived3 : DerChoiceBaseClass
{
    // DER set
    std::vector<std::unique_ptr<DerChoiceBaseClass>> subChoices_;

    DerChoiceDerived3() = default;

    DerChoiceDerived3(std::vector<std::unique_ptr<DerChoiceBaseClass>> sub);

    template<class F>
    void withTuple(F&& f, cryptoconditions::der::TraitsCache& traitsCache);

    template<class F>
    void withTuple(F&& f, cryptoconditions::der::TraitsCache& traitsCache) const
    {
        const_cast<DerChoiceDerived3*>(this)->withTuple(
            std::forward<F>(f), traitsCache);
    }

    std::uint8_t
    type() const override;

    std::uint64_t
    derEncodedLength(
        boost::optional<cryptoconditions::der::GroupType> const& parentGroupType,
        cryptoconditions::der::TagMode encoderTagMode,
        cryptoconditions::der::TraitsCache& traitsCache) const override;

    void
    encode(cryptoconditions::der::Encoder& encoder) const override;

    void
    decode(cryptoconditions::der::Decoder& decoder) override;

    int
    compare(
        DerChoiceBaseClass const& rhs,
        cryptoconditions::der::TraitsCache& traitsCache) const override;

    void
    print(std::ostream& ostr, bool ordered=false) const override;

    friend
    bool
    operator==(DerChoiceDerived3 const& lhs, DerChoiceDerived3 const& rhs);

    friend
    bool
    operator!=(DerChoiceDerived3 const& lhs, DerChoiceDerived3 const& rhs);
};

struct DerChoiceDerived4 : DerChoiceBaseClass
{
    // DER set
    std::vector<std::unique_ptr<DerChoiceBaseClass>> subChoices_;

    DerChoiceDerived4() = default;

    DerChoiceDerived4(std::vector<std::unique_ptr<DerChoiceBaseClass>> sub);

    template <class F>
    void
    withTuple(F&& f, cryptoconditions::der::TraitsCache& traitsCache);

    template <class F>
    void
    withTuple(F&& f, cryptoconditions::der::TraitsCache& traitsCache) const
    {
        const_cast<DerChoiceDerived4*>(this)->withTuple(
            std::forward<F>(f), traitsCache);
    }

    std::uint8_t
    type() const override;

    std::uint64_t
    derEncodedLength(
        boost::optional<cryptoconditions::der::GroupType> const& parentGroupType,
        cryptoconditions::der::TagMode encoderTagMode,
        cryptoconditions::der::TraitsCache& traitsCache) const override;

    void
    encode(cryptoconditions::der::Encoder& encoder) const override;

    void
    decode(cryptoconditions::der::Decoder& decoder) override;

    int
    compare(
        DerChoiceBaseClass const& rhs,
        cryptoconditions::der::TraitsCache& traitsCache) const override;

    void
    print(std::ostream& ostr, bool ordered=false) const override;

    friend
    bool
    operator==(DerChoiceDerived4 const& lhs, DerChoiceDerived4 const& rhs);

    friend
    bool
    operator!=(DerChoiceDerived4 const& lhs, DerChoiceDerived4 const& rhs);
};

struct DerChoiceDerived5 : DerChoiceBaseClass
{
    std::unique_ptr<DerChoiceBaseClass> subChoice_;
    std::string name_;
    std::uint64_t id_;

    DerChoiceDerived5() = default;

    DerChoiceDerived5(
        std::unique_ptr<DerChoiceBaseClass> sub,
        std::string const& n,
        std::uint64_t i);

    template <class F>
    void
    withTuple(F&& f, cryptoconditions::der::TraitsCache& traitsCache);

    template <class F>
    void
    withTuple(F&& f, cryptoconditions::der::TraitsCache& traitsCache) const
    {
        const_cast<DerChoiceDerived5*>(this)->withTuple(
            std::forward<F>(f), traitsCache);
    }

    std::uint8_t
    type() const override;

    std::uint64_t
    derEncodedLength(
        boost::optional<cryptoconditions::der::GroupType> const& parentGroupType,
        cryptoconditions::der::TagMode encoderTagMode,
        cryptoconditions::der::TraitsCache& traitsCache) const override;

    void
    encode(cryptoconditions::der::Encoder& encoder) const override;

    void
    decode(cryptoconditions::der::Decoder& decoder) override;

    int
    compare(
        DerChoiceBaseClass const& rhs,
        cryptoconditions::der::TraitsCache& traitsCache) const override;

    void
    print(std::ostream& ostr, bool ordered=false) const override;

    friend
    bool
    operator==(DerChoiceDerived5 const& lhs, DerChoiceDerived5 const& rhs);

    friend
    bool
    operator!=(DerChoiceDerived5 const& lhs, DerChoiceDerived5 const& rhs);
};

}  // test

namespace cryptoconditions {
namespace der {
template <>
struct DerCoderTraits<std::unique_ptr<test::DerChoiceBaseClass>>
{
    constexpr static GroupType
    groupType()
    {
        return GroupType::choice;
    }
    constexpr static ClassId
    classId()
    {
        return ClassId::contextSpecific;
    }
    static boost::optional<std::uint8_t> const&
    tagNum()
    {
        static boost::optional<std::uint8_t> tn;
        return tn;
    }
    static std::uint8_t
    tagNum(std::unique_ptr<test::DerChoiceBaseClass> const& f)
    {
        assert(f);
        return f->type();
    }
    constexpr static bool
    primitive()
    {
        return false;
    }

    static void
    encode(Encoder& encoder, std::unique_ptr<test::DerChoiceBaseClass> const& b)
    {
        b->encode(encoder);
    }

    static void
    decode(Decoder& decoder, std::unique_ptr<test::DerChoiceBaseClass>& v)
    {
        auto const parentTag = decoder.parentTag();
        if (!parentTag)
        {
            decoder.ec_ = make_error_code(Error::logicError);
            return;
        }

        if (parentTag->classId != classId())
        {
            decoder.ec_ = make_error_code(Error::preambleMismatch);
            return;
        }

        switch (parentTag->tagNum)
        {
            case 1:
                v = std::make_unique<test::DerChoiceDerived1>();
                break;
            case 2:
                v = std::make_unique<test::DerChoiceDerived2>();
                break;
            case 3:
                v = std::make_unique<test::DerChoiceDerived3>();
                break;
            case 4:
                v = std::make_unique<test::DerChoiceDerived4>();
                break;
            case 5:
                v = std::make_unique<test::DerChoiceDerived5>();
                break;
            default:
                decoder.ec_ = make_error_code(der::Error::unknownChoiceTag);
        }

        if (v)
            v->decode(decoder);

        if (decoder.ec_)
            v.reset();
    }

    static std::uint64_t
    length(
        std::unique_ptr<test::DerChoiceBaseClass> const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        auto const l = v->derEncodedLength(groupType(), encoderTagMode, traitsCache);
        if (encoderTagMode == TagMode::automatic)
            return l;

        auto const cll = contentLengthLength(l); 
        return 1 + cll + l;
    }

    static int
    compare(
        std::unique_ptr<test::DerChoiceBaseClass> const& lhs,
        std::unique_ptr<test::DerChoiceBaseClass> const& rhs,
        TraitsCache& traitsCache)
    {
        return lhs->compare(*rhs, traitsCache);
    }
};
}  // der
}  // cryptoconditions
}  // ripple

#endif
