//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/conditions/impl/Der.h>

namespace ripple {
namespace cryptoconditions {
namespace der {

/** Returns an error code.

    This function is used by the implementation to convert
    @ref error values into @ref error_code objects.
*/
static inline std::error_category const&
derCategory()
{
    using error_category = std::error_category;
    using error_code = std::error_code;
    using error_condition = std::error_condition;

    struct cat_t : public error_category
    {
        char const*
        name() const noexcept override
        {
            return "Der";
        }

        std::string
        message(int e) const override
        {
            switch (static_cast<Error>(e))
            {
                case Error::integerBounds:
                    return "integer bounds";

                case Error::longGroup:
                    return "long group";

                case Error::shortGroup:
                    return "short group";

                case Error::badDerEncoding:
                    return "bad der encoding";

                case Error::tagOverflow:
                    return "tag overflow";

                case Error::preambleMismatch:
                    return "preamble mismatch";

                case Error::contentLengthMismatch:
                    return "content length mismatch";

                case Error::unknownChoiceTag:
                    return "unknown choice tag";

                case Error::unsupported:
                    return "unsupported der feature";

                case Error::logicError:
                    return "a coding precondition or postcondition was "
                           "violated";

                default:
                    return "der error";
            }
        }

        std::error_condition
        default_error_condition(int ev) const noexcept override
        {
            return std::error_condition{ev, *this};
        }

        bool
        equivalent(int ev, error_condition const& ec) const noexcept override
        {
            return ec.value() == ev && &ec.category() == this;
        }

        bool
        equivalent(error_code const& ec, int ev) const noexcept override
        {
            return ec.value() == ev && &ec.category() == this;
        }
    };
    static cat_t const cat{};
    return cat;
}

std::error_code
make_error_code(Error e)
{
    return std::error_code{static_cast<int>(e), derCategory()};
}

//------------------------------------------------------------------------------

boost::optional<std::size_t>
TraitsCache::length(void const* addr) const
{
    auto const i = lengthCache_.find(addr);
    if (i != lengthCache_.end())
        return i->second;
    return boost::none;
}

void
TraitsCache::length(void const* addr, size_t l)
{
    if (lengthCache_.empty())
        lengthCache_.reserve(32);
    lengthCache_[addr] = l;
}

boost::optional<boost::container::small_vector<size_t, 8>>
TraitsCache::sortOrder(void const* addr) const
{
    auto const i = sortOrderCache_.find(addr);
    if (i != sortOrderCache_.end())
        return i->second;
    return boost::none;
}

void
TraitsCache::sortOrder(
    void const* addr,
    boost::container::small_vector<size_t, 8> const& so)
{
    if (sortOrderCache_.empty())
        sortOrderCache_.reserve(32);
    sortOrderCache_[addr] = so;
}

//------------------------------------------------------------------------------

Tag::Tag(SequenceTag)
    : classId(ClassId::universal), tagNum(16), primitive(false)
{
}

Tag::Tag(SetTag)
    : classId(ClassId::universal), tagNum(17), primitive(false)
{
}

bool
Tag::isSet() const
{
    return classId == ClassId::universal && tagNum == 17;
}

//------------------------------------------------------------------------------

std::uint64_t
tagNumLength(std::uint64_t v)
{
    if (v <= 30)
        return 1;

    std::size_t n = 1 + 8 * sizeof(v) / 7;

    // skip leading zeros
    while (n--)
    {
        auto b = static_cast<std::uint8_t>((v >> (n * 7)) & 0xFF);
        if (b)
            break;
    }

    assert(n);
    return n + 2;
}

void
encodeTagNum(MutableSlice& dst, std::uint64_t v, std::error_code& ec)
{
    assert(v > 30);
    std::size_t n = 1 + 8 * sizeof(v) / 7;

    // skip leading zeros
    while (n--)
    {
        auto b = static_cast<std::uint8_t>((v >> (n * 7)) & 0xFF);
        if (b)
            break;
    }

    assert(n);
    ++n;
    if (dst.size() != n)
    {
        ec = make_error_code(Error::logicError);
        return;
    }
    while (n--)
    {
        auto b = static_cast<std::uint8_t>((v >> (n * 7)) & 0xFF);
        // all but the last byte has the high order bit set
        if (n)
            b |= 1 << 7;
        else
            b &= ~(1 << 7);
        dst.push_back(static_cast<char>(b));
    }
}

void
encodeContentLength(MutableSlice& dst, std::uint64_t v, std::error_code& ec)
{
    if (v <= 127)
    {
        if (dst.size() != 1)
        {
            ec = make_error_code(Error::logicError);
            return;
        }
        dst.push_back(static_cast<char>(v));
        return;
    }

    std::size_t n = sizeof(v);

    // skip leading zeros
    while (n--)
    {
        auto b = static_cast<std::uint8_t>((v >> (n * 8)) & 0xFF);
        if (b)
            break;
    }

    ++n;
    assert(n);

    if (dst.size() != n + 1)
    {
        ec = make_error_code(Error::logicError);
        return;
    }

    dst.push_back(static_cast<char>(n) | (1 << 7));

    while (n--)
        dst.push_back(static_cast<char>((v >> (n * 8)) & 0xFF));
}

size_t
contentLengthLength(std::uint64_t v)
{
    if (v <= 127)
    {
        return 1;
    }

    std::size_t n = sizeof(v);

    // skip leading zeros
    while (n--)
    {
        auto b = static_cast<std::uint8_t>((v >> (n * 8)) & 0xFF);
        if (b)
            break;
    }

    return n + 2;
}

std::uint64_t
tagLength(Tag t)
{
    if (t.tagNum <= 30)
        return 1;

    auto v = t.tagNum;
    std::size_t n = 1 + 8 * sizeof(v) / 7;

    // skip leading zeros
    while (n--)
    {
        auto b = static_cast<std::uint8_t>((v >> (n * 7)) & 0xFF);
        if (b)
            break;
    }

    return 2+n;
}

void
encodePreamble(MutableSlice& dst, Preamble const& p, std::error_code& ec)
{
    if (dst.size() <= 1)
    {
        ec = make_error_code(Error::logicError);
        return;
    }

    char d = (static_cast<uint8_t>(p.tag_.classId) << 6);
    if (!p.tag_.primitive)
        d |= 1 << 5;

    if (p.tag_.tagNum <= 30)
    {
        d |= p.tag_.tagNum;
        dst.push_back(d);
    }
    else
    {
        d |= 0x1f;
        dst.push_back(d);
        encodeTagNum(dst, p.tag_.tagNum, ec);
        if (ec)
            return;
    }
    encodeContentLength(dst, p.contentLength_, ec);
}

void
decodePreamble(Slice& slice, Preamble& p, std::error_code& ec)
{
    auto popFront = [&]() -> std::uint8_t {
        if (slice.empty())
        {
            ec = make_error_code(Error::shortGroup);
            return 0;
        }
        auto const r = slice[0];
        slice += 1;
        return r;
    };

    std::uint8_t curByte = popFront();
    if (ec)
        return;

    p.tag_.classId = static_cast<ClassId>(curByte >> 6);
    p.tag_.primitive = !(curByte & (1 << 5));

    // decode the tag
    if ((curByte & 0x1f) != 0x1f)
    {
        p.tag_.tagNum = curByte & 0x1f;
    }
    else
    {
        std::uint64_t tagNum = 0;

        do
        {
            curByte = popFront();
            if (ec)
                return;
            auto const asBase128 = curByte & ~(1 << 7);

            if (tagNum & (static_cast<decltype(tagNum)>(0xfe)
                          << 8 * (sizeof(tagNum) - 1)))
            {
                // Shifting by 7 bits would overflow tagNum
                ec = make_error_code(Error::tagOverflow);
                return;
            }

            tagNum = (tagNum << 7) | asBase128;

            if (!tagNum)
            {
                // leading zeros
                ec = make_error_code(Error::badDerEncoding);
                return;
            }

        } while (curByte & (1 << 7));

        p.tag_.tagNum = tagNum;
        if (tagNum <= 30)
        {
            // tag was encoded with the long form, but should have been short
            // form
            ec = make_error_code(Error::badDerEncoding);
            return;
        }
    }

    // decode the content length
    std::uint64_t& contentLength = p.contentLength_;
    contentLength = 0;

    curByte = popFront();
    if (ec)
        return;
    if (curByte <= 127)
    {
        contentLength = curByte;
    }
    else if ((curByte & ~(1 << 7)) > 8)
    {
        ec = make_error_code(Error::unsupported);
        return;
    }
    else
    {
        int const n = (curByte & ~(1 << 7));
        for (int i = 0; i < n; ++i)
        {
            curByte = popFront();
            if (ec)
                return;
            contentLength = (contentLength << 8) | curByte;
        }
    }
}

//------------------------------------------------------------------------------

Group::Group(
    Tag t,
    TagMode tagMode,
    GroupType groupType,
    MutableSlice slice)
    : id_(t)
    , tagMode_(tagMode)
    , groupType_(groupType)
    , slice_(slice)
{
}

MutableSlice&
Group::slice()
{
    return slice_;
}

Slice
Group::slice() const
{
    return slice_;
}

bool
Group::isSet() const
{
    return id_.isSet();
}

bool
Group::isAutoSequence() const
{
    return tagMode_ == TagMode::automatic &&
        groupType_ == GroupType::autoSequence;
}

bool
Group::isChoice() const
{
    return groupType_ == GroupType::choice;
}

void
Group::set(bool primitive, GroupType bt)
{
    id_.primitive = primitive;
    groupType_ = bt;
}

size_t
Group::numChildren() const
{
    return numChildren_;
}

GroupType Group::groupType() const
{
    return groupType_;
}

//------------------------------------------------------------------------------

Encoder::Encoder(TagMode tagMode) : tagMode_(tagMode)
{
}

Encoder::~Encoder()
{
    if (ec_)
        return;

    // hitting this assert means the encoding stream was not terminated with a
    // call to eos(); The usual way to do this is with the `eos` object:
    // encoder << someObject << eos;
    // Certain error checks can only happen after the stream knows there are not
    // other objects to be encoded.
    assert(atEos_);
}

void
Encoder::startGroup(Tag t, GroupType groupType, std::uint64_t contentSize)
{
    if (ec_)
        return;

    assert(!root_);

    if (groupType == GroupType::choice && parentIsChoice())
    {
        // Choice/choice groups are not supported
        ec_ = make_error_code(Error::unsupported);
        return;
    }

    if (parentIsChoice() && tagMode_ == TagMode::automatic)
    {
        auto g = subgroups_.top();
        g.set(t.primitive, groupType);
        subgroups_.emplace(std::move(g));
        return;
    }

    auto const contentLL = contentLengthLength(contentSize);
    auto const tagL = tagLength(t);
    auto const sliceSize = contentSize + contentLL + tagL;

    auto const parentSlice = [&]
    {
        if (!subgroups_.empty())
            return subgroups_.top().slice();
        assert(rootBuf_.empty());
        rootBuf_.resize(sliceSize);
        rootSlice_ = Slice{rootBuf_.data(), rootBuf_.size()};
        return MutableSlice(rootBuf_.data(), rootBuf_.size());
    }();

    if (sliceSize > parentSlice.size())
    {
        // incorrect length calculation
        ec_ = make_error_code(Error::logicError);
        return;
    }

    MutableSlice thisSlice{parentSlice.data(), sliceSize};

    auto const preambleLength = sliceSize - contentSize;
    if (preambleLength > thisSlice.size())
    {
        // incorrect length calculation
        ec_ = make_error_code(Error::logicError);
        return;
    }
    MutableSlice preambleSlice{thisSlice.data(), preambleLength};
    encodePreamble(preambleSlice, Preamble{t, contentSize}, ec_);
    if (ec_)
        return;
    if (!preambleSlice.empty())
    {
        // incorrect length calculation
        ec_ = make_error_code(Error::logicError);
        return;
    }
    thisSlice += preambleLength;

    subgroups_.emplace(t, tagMode_, groupType, thisSlice);
};

void
Encoder::endGroup()
{
    if (ec_)
        return;

    if (subgroups_.empty())
    {
        ec_ = make_error_code(Error::logicError);
        return;
    }

    Group top(std::move(subgroups_.top()));
    subgroups_.pop();

    if (!top.slice().empty())
    {
        // incorrect length calculation
        ec_ = make_error_code(Error::logicError);
        return;
    }

    if (parentIsChoice() && tagMode_ == TagMode::automatic)
    {
        // copy the child group, but don't add it to the parent
        subgroups_.top() = std::move(top);
        return;
    }

    if (subgroups_.empty())
    {
        assert(!root_);
        root_.emplace(std::move(top));
        return;
    }

    auto& parentSlice = subgroups_.top().slice();
    auto const inc = std::distance(parentSlice.data(), top.slice().data());
    if (inc < 0 || inc > parentSlice.size())
    {
        // incorrect length calculation
        ec_ = make_error_code(Error::logicError);
        return;
    }
    parentSlice += inc;
    subgroups_.top().incrementNumChildren();
};

void
Encoder::eos()
{
    atEos_ = true;

    if (ec_)
        return;

    if (!subgroups_.empty())
    {
        ec_ = make_error_code(Error::logicError);
        return;
    }
}

size_t
Encoder::size() const
{
    return rootSlice_.size();
}

MutableSlice&
Encoder::parentSlice()
{
    static MutableSlice empty{nullptr, 0};
    if (!subgroups_.empty())
        return subgroups_.top().slice();
    return empty;
}

std::error_code const&
Encoder::ec() const
{
    return ec_;
}

std::vector<char> const&
Encoder::
serializationBuffer(std::error_code& ec) const
{
    if (ec_)
    {
        ec = ec_;
        return rootBuf_;
    }

    if (!root_ || rootSlice_.size() != rootBuf_.size())
        ec = make_error_code(Error::logicError);
    
    return rootBuf_;
}

bool
Encoder::parentIsAutoSequence() const
{
    return tagMode_ == TagMode::automatic && !subgroups_.empty() &&
        subgroups_.top().isAutoSequence();
}

bool
Encoder::parentIsChoice() const
{
    return !subgroups_.empty() && subgroups_.top().isChoice();
}

//------------------------------------------------------------------------------

Decoder::Decoder(Slice slice, TagMode tagMode)
    : tagMode_(tagMode), rootSlice_(slice)
{
}

Decoder::~Decoder()
{
    if (ec_)
        return;

    // hitting this assert means the decoding stream was not terminated with a
    // call to eos(); The usual way to do this is with the `eos` object:
    // decoder >> someObject >> eos;
    // Certain error checks can only happen after the stream knows there are not
    // other objects to be encoded.
    assert(atEos_);
}

void
Decoder::startGroup(boost::optional<Tag> const& t, GroupType groupType)
{
    if (ec_)
        return;

    if (groupType == GroupType::choice && parentIsChoice())
    {
        // Choice/choice groups are not supported
        ec_ = make_error_code(Error::unsupported);
        return;
    }

    if (parentIsChoice() && tagMode_ == TagMode::automatic)
    {
        if (std::get<std::uint32_t>(ancestors_.top()) > 0)
        {
            // choice groups must have exactly one child, and adding this child
            // would violate that constraint
            ec_ = make_error_code(Error::badDerEncoding);
            return;
        }
        auto a = ancestors_.top();
        std::get<GroupType>(a) = groupType;
        ancestors_.emplace(a);
        return;
    }

    Preamble p;
    (*this) >> p;

    if (!(groupType == GroupType::choice && tagMode_ == TagMode::automatic))
    {
        if (t && (p.tag_ != t))
        {
            ec_ = make_error_code(Error::preambleMismatch);
            return;
        }
    }

    auto const& s = parentSlice();
    if (p.contentLength_ > s.size())
    {
        ec_ = make_error_code(Error::shortGroup);
        return;
    }
    ancestors_.emplace(Slice{s.data(), p.contentLength_}, p.tag_, groupType, 0);
}

void
Decoder::endGroup()
{
    if (ec_)
        return;

    if (ancestors_.empty())
    {
        ec_ = make_error_code(Error::logicError);
        return;
    }

    if (std::get<GroupType>(ancestors_.top()) == GroupType::choice &&
        tagMode_ == TagMode::automatic &&
        std::get<std::uint32_t>(ancestors_.top()) != 1)
    {
        // choice groups must have exactly one child
        ec_ = make_error_code(Error::badDerEncoding);
        return;
    }

    auto const poped = std::get<Slice>(ancestors_.top());
    ancestors_.pop();
    if (!poped.empty())
    {
        ec_ = make_error_code(Error::longGroup);
        return;
    }

    if (!ancestors_.empty() &&
        std::get<GroupType>(ancestors_.top()) == GroupType::choice &&
        tagMode_ == TagMode::automatic)
    {
        // track children to make sure choices always have exactly one child
        ++std::get<std::uint32_t>(ancestors_.top());
    }

    auto& parent = parentSlice();
    auto const toConsume = std::distance(parent.data(), poped.data());
    if (toConsume < 0 || toConsume > parent.size())
    {
        // incorrect length calculation
        ec_ = make_error_code(Error::logicError);
        return;
    }
    parent += toConsume;
}

void
Decoder::eos()
{
    atEos_ = true;
    if (ec_)
        return;

    if (!ancestors_.empty())
    {
        ec_ = make_error_code(Error::logicError);
        return;
    }
    if (!rootSlice_.empty())
    {
        ec_ = make_error_code(Error::longGroup);
        return;
    }
}

boost::optional<Tag>
Decoder::parentTag() const
{
    if (ancestors_.empty())
        return boost::none;
    return std::get<Tag>(ancestors_.top());
}

Slice&
Decoder::parentSlice()
{
    if (!ancestors_.empty())
        return std::get<Slice>(ancestors_.top());
    return rootSlice_;
};

bool
Decoder::parentIsAutoSequence() const
{
    return tagMode_ == TagMode::automatic && !ancestors_.empty() &&
        std::get<GroupType>(ancestors_.top()) == GroupType::autoSequence;
}

bool
Decoder::parentIsChoice() const
{
    return !ancestors_.empty() &&
        std::get<GroupType>(ancestors_.top()) == GroupType::choice;
}

std::error_code const&
Decoder::ec() const
{
    return ec_;
}

//------------------------------------------------------------------------------

Eos eos;
Automatic automatic;
Constructor constructor;

}  // der
}  // ripple
}  // cryptoconditions
