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

#ifndef RIPPLE_CONDITIONS_DERCODER_H
#define RIPPLE_CONDITIONS_DERCODER_H

#include <ripple/basics/Slice.h>
#include <ripple/conditions/impl/DerTraits.h>

#include <boost/optional.hpp>

#include <cstdint>
#include <system_error>
#include <stack>
#include <tuple>
#include <type_traits>
#include <vector>

namespace ripple {
namespace cryptoconditions {

/**
  The `der` namespace contains a set of classes the implement ASN.1 DER encoding
  and decoding for cryptoconditions.

  There are two keys to understanding how to use these coders: 1)
  DerCoderTraits<T>, and 2) `withTuple`.

  To encode or decode a type `T`, a specialization of DerCoderTraits<T> must
  exist. This specialization contains all the functions specific to streaming
  type `T`. The most important are: `encode`, `decode`, `length`, and `compare`.
  @see {@link #DerCoderTraits}.

  If a class defines the function `withTuple`, that class can use the helper
  functions `withTupleEncodeHelper`, `withTupleDecodeHelper`,
  `withTupleEncodedLengthHelper`, and `withTupleCompareHelper`. The `withTuple`
  function takes a callable, and the class will wrap its member variables in a
  tuple of references (usually using `tie`), and call the callable with the
  tuple as an argument. See one of the existing cryptocondtion implementations
  for an example of how `withTuple` is used.

  @note Efficiently encoding cryptocondition classes into ASN.1 has some challenges:

  1) The size of the preamble depends on the size of content being encoded. This
  makes it difficult to encode in a single pass. The most natural implementation
  would a) encode the content, b) encode the preamble, c) copy the result into
  some final buffer. The function `DerCoderTraits<T>::length` solves this
  problem. When encoding a value of type T, the length will return the number of
  bytes used to encode contents of the value (but does not include the
  preamble).

  2) Encoding DER sets requires the elements of the set be encoded in sorted
  order (sorted by the encoding of the individual elements). The function
  `DerCoderTraits<T>::compare` solves this problem. This function returns a
  values less than 0 if the lhs < rhs, 0 if lhs == rhs, and a value greater than
  0 if lhs > rhs.

  3) When encoding cryptoconditions that contain other cryptoconditions in
  hierarchies (such as threshold and prefix), some values - like length and sort
  order - will be computed multiple times, and the number of times a value is
  computed grows as a function of the cryptocondition's depth in the hierarchy.
  This is solved with a TraitCache. This caches previously computed values of
  length and sort order so they do not need to be recomputed. Note that storing
  values in the cache is type dependent, and the address of the variable must be
  stable while encoding. It makes sense to cache higher level values, but not
  primitives.
 */

namespace der {

struct Encoder;
struct Decoder;

/// constructor tag to specify an ASN.1 sequence
struct SequenceTag {};
/// constructor tag to specify an ASN.1 set
struct SetTag {};

/// the type information part of an ASN.1 preamble
struct Tag
{
    ClassId classId = ClassId::universal;
    std::uint64_t tagNum = 0;
    bool primitive = true;

    Tag() = default;

    Tag(ClassId classId_, std::uint64_t tagNum_, bool primitive_)
        : classId(classId_), tagNum(tagNum_), primitive(primitive_)
    {
    }

    template <class T>
    Tag(DerCoderTraits<T> t, std::uint64_t tn)
        : Tag(DerCoderTraits<T>::classId(), tn, DerCoderTraits<T>::primitive())
    {
    }

    explicit
    Tag(SequenceTag);

    explicit
    Tag(SetTag);

    /// return true if the tag represents an ASN.1 set
    bool
    isSet() const;

    friend bool
    operator<(Tag const& lhs, Tag const& rhs)
    {
        return std::tie(lhs.classId, lhs.tagNum, lhs.primitive) <
            std::tie(rhs.classId, rhs.tagNum, rhs.primitive);
    }
    friend bool
    operator==(Tag const& lhs, Tag const& rhs)
    {
        return lhs.classId == rhs.classId && lhs.tagNum == rhs.tagNum &&
            lhs.primitive == rhs.primitive;
    }
    friend bool
    operator!=(Tag const& lhs, Tag const& rhs)
    {
        return !operator==(lhs, rhs);
    }
};

/** an ans.1 preamble

    values are encoded in ans.1 with a preamble that specifies how to interpret
    the content, followed by the content. This struct represents the preamble.
*/
struct Preamble
{
    /// type information
    Tag tag_;
    /// content length in bytes
    std::uint64_t contentLength_;
};

/** RAII class for coder groups

    ASN.1 values are coded as a hierarchy. There are root values, which have
    sub-values as children. A `GroupGuard` organizes the serialization code so
    C++ scopes represent levels in the ASN.1 hierarchy. The constructor pushes a
    new group onto the coders group stack, and the destructor pops the group.
    Entering a scope represents a new value that will be coded. New values will
    be descendants of this value coded in this scope until the scope is exited.
 */
template <class Coder>
class GroupGuard
{
    /// The encoder or decoder
    Coder& s_;

public:
    GroupGuard(Coder& s, Tag t, GroupType bt)
        : s_(s)
    {
        s_.startGroup(t, bt);
    }

    GroupGuard(Coder& s, Tag t, GroupType bt, std::uint64_t contentSize)
        : s_(s)
    {
        s_.startGroup(t, bt, contentSize);
    }

    GroupGuard(Coder& s, boost::optional<Tag> const& t, GroupType bt)
        : s_(s)
    {
        s_.startGroup(t, bt);
    }

    GroupGuard(Coder& s, SequenceTag t)
        : GroupGuard(s, Tag{t}, GroupType::sequence)
    {
    }

    GroupGuard(Coder& s, SetTag t)
        : GroupGuard(s, Tag{t}, GroupType::set)
    {
    }

    template <class T>
    GroupGuard(Coder& s, DerCoderTraits<T> t)
        : s_(s)
    {
        boost::optional<Tag> tag;
        if (auto const tagNum = t.tagNum())
            tag.emplace(t, *tagNum);
        s_.startGroup(tag, t.groupType());
    }

    template <class T>
    GroupGuard(Coder& s, T const& v, DerCoderTraits<T> t)
        : s_(s)
    {
        auto const tagNum = t.tagNum(v);
        Tag tag(t, tagNum);
        s_.startGroup(tag, t.groupType());
    }

    // Needed for fuzz testing
    GroupGuard(Coder& s, GroupType bt)
        : s_(s)
    {
        s_.startGroup(boost::none, bt);
    }

    ~GroupGuard()
    {
        s_.endGroup();
    }
};

/** End of stream guard

    Coders need to know when when a serialization is complete. Clients signal
    this by calling `eos`. This guard calls `eos` in the destructor so leaving
    a scope may be used to signal `eos`.

    @note: This class is mostly used for testing. The usual way to signal `eos`
    is by adding `der::eos` at the end of a stream. For example: `coder << value
    << der::eos;`
 */
template <class Coder>
class EosGuard
{
    // Encoder or decoder
    Coder& s_;

public:
    explicit
    EosGuard(Coder& s)
        : s_(s)
    {
    }

    ~EosGuard()
    {
        s_.eos();
    }
};

template<std::uint64_t ChunkBitSize>
std::uint64_t
numLeadingZeroChunks(std::uint64_t v, std::uint64_t n)
{
    static_assert(ChunkBitSize <= 8, "Unsupported chunk bit size");

    std::uint64_t result = 0;
    while (n--)
    {
        auto b = static_cast<std::uint8_t>((v >> (n * ChunkBitSize)) & 0xFF);
        if (b)
            break;
        ++result;
    }
    return result;
}

/** decode the tag from ASN.1 format
 */
void
decodeTag(Slice& slice, Tag& tag, std::error_code& ec);

/** Encode the integer in a format appropriate for an ans.1 tag number.

    Encode the integer in big endian form, in as few of bytes as possible. All
    but the last byte has the high order bit set. The number is encoded in base
    128 (7-bits each).
*/
void
encodeTagNum(MutableSlice& dst, std::uint64_t v, std::error_code& ec);

/** Return the number of bytes required to encode a tag with the given tag num */
std::uint64_t
tagNumLength(std::uint64_t v);

/** Decode the content length from ASN.1 format
*/
void
decodeContentLength(Slice& slice, std::uint64_t& contentLength, std::error_code& ec);

/** Encode the integer in a format appropriate for an ans.1 content length

    Encode the integer in big endian form, in as few of bytes as possible.
*/
void
encodeContentLength(MutableSlice& dst, std::uint64_t v, std::error_code& ec);

/** return the number of bytes required to encode the given content length
 */
std::uint64_t
contentLengthLength(std::uint64_t);

/** return the number of bytes required to encode the given tag
 */
std::uint64_t
tagLength(Tag t);

/** return the number of bytes required to encode the value, including the preamble
 */
template <class Trait, class T>
std::uint64_t
totalLength(
    T const& v,
    boost::optional<GroupType> const& parentGroupType,
    TagMode encoderTagMode,
    TraitsCache& traitsCache,
    boost::optional<std::uint64_t> const& childNumber)
{
    auto const contentLength =
        Trait::length(v, parentGroupType, encoderTagMode, traitsCache);
    if (encoderTagMode == TagMode::automatic &&
        parentGroupType && *parentGroupType == GroupType::choice)
        return contentLength;

    auto const oneTagResult = tagNumLength(childNumber.value_or(0)) +
        contentLength + contentLengthLength(contentLength);

    if (parentGroupType && *parentGroupType == GroupType::autoSequence &&
        DerCoderTraits<T>::groupType() == GroupType::choice)
    {
        // auto sequences with a choice write a two tags: one for the sequence
        // number and one for the choice
        // note: This breaks down if the choice number is large enough to
        // require more than one byte for the tag (more than 30 choices)
        return tagNumLength(0) + oneTagResult + contentLengthLength(oneTagResult);
    }

    // all cryptocondition preambles are one byte
    return oneTagResult;
}

/** A value in a hierarchy of values when encoding

    ASN.1 values are coded as a hierarchy. There is one root value, which has
    sub-values as children. When encoding, this class keeps track the type that
    is being encoded, what bytes in the stream represent content for this value,
    and child values.

    @note: decoders use a different class to represent the hierarchy of values.
 */
class Group
{
    /// ASN.1 type information for the value being encoded
    Tag id_;
    /// current number of children
    size_t numChildren_ = 0;
    /// ASN.1 explicit (direct) or automatic tagging
    TagMode tagMode_;
    /// additional type information for the group
    GroupType groupType_;

    /** data slice reserved for both the preamble and contents of the group

        @note: it _must_ be the correct size. It will not be resized.
    */
    MutableSlice slice_;

public:
    Group(Group const&) = default;
    Group(Group&&) = default;
    Group&
    operator=(Group&&) = default;
    Group&
    operator=(Group const&) = default;

    Group(
        Tag t,
        TagMode tagMode,
        GroupType groupType,
        MutableSlice slice);

    /// the data slice reserved for both the preamble and contents of the group
    MutableSlice&
    slice();

    Slice
    slice() const;

    /** Increment the number of children this group has
     */
    void
    incrementNumChildren()
    {
        ++numChildren_;
    }

    /// return true if the group represents an ASN.1 set
    bool
    isSet() const;

    /** return true if the group represents an auto sequence

        @note an auto sequence is an ASN.1 sequence that has autogenerated
              tag numbers
     */
    bool
    isAutoSequence() const;

    /// return true if the group represents an ASN.1 choice
    bool
    isChoice() const;

    /** set the groups type information

        @param primitive true is primitive, false if constructed
        @param bt the groups type information
     */
    void
    set(bool primitive, GroupType bt);

    /// return the number of sub-values
    size_t
    numChildren() const;

    GroupType groupType() const;
};

/** encode the preamble into the dst slice
 */
void
encodePreamble(MutableSlice& dst, Preamble const& p, std::error_code& ec);

/** decode the preamble from slice into p
 */
void
decodePreamble(Slice& slice, Preamble& p, std::error_code& ec);

/** type representing and end of stream

    Coders need to know when when a serialization is complete. Clients signal
    this by calling `eos`. The typical way of calling `eos` is by serializing a
    value of type Eos. There is a convenience global variable for this purpose.
    It will typically be used as follows: `coder << value << der::eos;`
*/
struct Eos {};
extern Eos eos;
/// constructor tag to specify a decoder in automatic mode
struct Automatic {};
extern Automatic automatic;
/** constructor tag to specify a type is being constructed for decoding into

    Often, it is convenient to create a type and then decode into that type.
    However, this would usually require that type to be default constructable
    (as the contents used to create are deserialized after the variable is
    constructed). This `Constructor` type is used to create constructors and
    specify that they should only be used for DER decoding.
 */
struct Constructor {};
extern Constructor constructor;


/** Stream interface to encode values into ASN.1 DER format

    The encoder class has an interface similar to a c++ output stream. Values
    are added to the stream using the `<<` operator. After all the values are
    added to the encoder, it must be terminated with a call to `eos()`. As a
    convenience, there is a special variable called `eos` that when streamed will
    call the stream's `eos()` function. Typically, the code to encode values to a
    stream is: `encoder << value_1 << ... << value_n << eos;`.

    Every type to be streamed must specialize the DerCoderTraits class @see
    {@link #DerCoderTraits}. There exist specializations for some C++ types and
    primitive rippled types - including integers, strings, bitstrings, tuples,
    buffers, arrays, and wrappers for wrapping collections like vector into
    either ASN.1 sets or sequences.

    After the values are written, the stream should be checked for errors. The
    function `ec` will return the error code of the first error encountered
    while streaming. Streaming will stop after the first error.

    Once the values are streamed, the actual encoding is retrieved by calling
    the `write` function.

    Encoding and decoding values often have the same code structure. The only
    difference is encoding will use `operator<<` and decoding will use
    `operator>>`. To allow writing generic code, both encoders and decoders
    support `operator&`. Typically, the generic code both encode and decode is:
    `coder & value_1 & ... & value_n & eos;`
 */
struct Encoder
{
    /// explicit or automatic tagging
    TagMode tagMode_ = TagMode::direct;

    /** values are coded as a hierarchy. `subgroups_` tracks the current
        position in the hierarchy.

        The bottom of the stack is the root value, the top of the stack is the
        current parent.
     */
    std::stack<Group> subgroups_;

    /** root of the tree of groups that were encoded

        @note: This is not populated until after encoding is complete
    */
    boost::optional<Group> root_;

    /** Buffer to encode into */
    std::vector<char> rootBuf_;

    /** Slice to encode into

        @note: rootBuf_ should contain the same information as rootSlice_, and
        `rootSlice_` may be removed in the future. It is kept as a debugging
        tool to make sure rootBuf_ is not resized after it is resized for the
        root group.
    */
    Slice rootSlice_;

    /** the error code of the first error encountered

        @note after the error code is set encoding stops
     */
    std::error_code ec_;

    /** true if the `eos` function has been called

        some error handling cannot happen until all the values have been coded.
        `atEos_` ensures every stream is terminated with an `eos` call so those
        error checks can be run.
     */
    bool atEos_ = false;

    /** traitsCache cache some values that need to be repeatedly computed and may be expensive to compute.

        Some value types will cache lengths and sort orders, other values types will not cache any values.
    */
    TraitsCache traitsCache_;

    explicit
    Encoder(TagMode tagMode);
    ~Encoder();

    /// prepare to add a new value as a child of the current value
    void
    startGroup(Tag t, GroupType groupType, std::uint64_t contentSize);

    /// finish adding the new value
    void
    endGroup();

    /** terminate the stream

        Streams must be terminated before the destructor is called. Certain error checks
        cannot occur until the encoder knows streaming is complete. Calling `eos()` runs these
        error checks. Failing to call `eos` before the destructor is an error.
     */
    void
    eos();

    /// total size in bytes of the content and all the preambles
    size_t
    size() const;

    /// return the portion of the buffer that represents the parent value
    MutableSlice&
    parentSlice();

    /** return the first error code encountered

        ec should be checked after streaming to ensure no errors occurred
     */
    std::error_code const&
    ec() const;

    /** get the serialization buffer that contains the values encoded as ASN.1 der
     */
    std::vector<char> const&
    serializationBuffer(std::error_code& ec) const;

    /** return true if the group at the top of the stack represents an auto
        sequence

        @note an auto sequence is an ASN.1 sequence that has autogenerated
              tag numbers
     */
    bool
    parentIsAutoSequence() const;

    /** return true if the group at the top of the stack represents an ASN.1
        choice
     */ 
    bool
    parentIsChoice() const;

    /** Add values to the encoder
    @{
    */
    friend
    Encoder&
    operator&(Encoder& s, Eos e)
    {
        s.eos();
        return s;
    }

    template <class T>
    friend
    Encoder&
    operator&(Encoder& s, T const& v)
    {
        if (s.ec_)
            return s;

        using traits = DerCoderTraits<std::decay_t<T>>;
        auto const groupType = traits::groupType();

        auto contentSize = [&] {
            boost::optional<GroupType> parentGroupType;
            if (!s.subgroups_.empty())
                parentGroupType.emplace(s.subgroups_.top().groupType());
            return traits::length(v, parentGroupType, s.tagMode_, s.traitsCache_);
        };

        if (s.parentIsAutoSequence())
        {
            if (groupType == GroupType::choice)
            {
                Tag const tag1{ClassId::contextSpecific,
                               s.subgroups_.top().numChildren(),
                               traits::primitive()};
                Tag const tag2{traits{}, traits::tagNum(v)};
                auto const contentSize = traits::length(
                    v, GroupType::sequenceChild, s.tagMode_, s.traitsCache_);
                GroupGuard<Encoder> g1(s, tag1, GroupType::sequenceChild,
                    tagLength(tag2) + contentLengthLength(contentSize) + contentSize);
                if (s.ec_)
                    return s;
                GroupGuard<Encoder> g2(s, tag2, groupType, contentSize);
                if (s.ec_)
                    return s;
                traits::encode(s, v);
            }
            else
            {
                Tag const tag{ClassId::contextSpecific,
                              s.subgroups_.top().numChildren(),
                              traits::primitive()};
                GroupGuard<Encoder> g(s, tag, groupType, contentSize());
                if (s.ec_)
                    return s;
                traits::encode(s, v);
            }
        }
        else
        {
            Tag const tag{traits{}, traits::tagNum(v)};
            GroupGuard<Encoder> g(s, tag, groupType, contentSize());
            if (s.ec_)
                return s;
            traits::encode(s, v);
        }

        return s;
    }

    template <class T>
    friend Encoder&
    operator<<(Encoder& s, T const& t)
    {
        return s & t;
    }
    /** @} */
};

//------------------------------------------------------------------------------

/** Stream interface to decode values from ASN.1 DER format

    The decode class has an interface similar to a c++ output stream. Values are
    decoded from the stream using the `>>` operator. After all the values are
    decoded, it must be terminated with a call to `eos()`. As a convenience, there
    is a special variable called `eos` that when streamed will call the stream's
    `eos()` function. Typically, the code to encode values to a stream is:
    `decoder >> value_1 >> ... >> value_n >> eos;`.

    Every type to be streamed must specialize the DerCoderTraits class @see
    {@link #DerCoderTraits}. There exist specializations for some C++ types and
    primitive rippled types - including integers, strings, bitstrings, tuples,
    buffers, arrays, and wrappers for wrapping collections like vector into
    either ASN.1 sets or sequences.

    After the values are decoded, the stream should be checked for errors. The
    function `ec` will return the error code of the first error encountered
    while decoding. Decoding will stop after the first error.

    Encoding and decoding values often have the same code structure. The only
    difference is encoding will use `operator<<` and decoding will use
    `operator>>`. To allow writing generic code, both encoders and decoders
    support `operator&`. Typically, the generic code both encode and decode is:
    `coder & value_1 & ... & value_n & eos;`
 */
struct Decoder
{
    /** explicit or automatic tagging

        @note this must match the mode the values were encoded with
     */
    TagMode tagMode_;

    /** true if the `eos` function has been called

        some error handling cannot happen until all the values have been coded.
        `atEos_` ensures every stream is terminated with an `eos` call so those
        error checks can be run.
     */
    bool atEos_ = false;

    /** slice for the entire buffer to be decoded */
    Slice rootSlice_;

    /** values are coded as a hierarchy. `ancestors_` tracks the current
        position in the hierarchy.

        The bottom of the stack is the root value, the top of the stack is the
        current parent.

        The tuple contains the slice, ancestor tag, groupType, and number of children
     */
    std::stack<std::tuple<Slice, Tag, GroupType, std::uint32_t>> ancestors_;

    /** the error code of the first error encountered

        @note after the error code is set decoding stops
     */
    std::error_code ec_;

    Decoder() = delete;

    Decoder(Slice slice, TagMode tagMode);

    ~Decoder();

    /// prepare to decode a value as a child of the current value
    void
    startGroup(boost::optional<Tag> const& t, GroupType groupType);

    /** finish decoding the new value */
    void
    endGroup();

    /** terminate the stream

        Streams must be terminated before the destructor is called. Certain error checks
        cannot occur until the encoder knows streaming is complete. Calling `eos()` runs these
        error checks. Failing to call `eos` before the destructor is an error.
     */
    void
    eos();

    /** return the first error code encountered

        ec should be checked after streaming to ensure no errors occurred
     */
    std::error_code const&
    ec() const;

    /** return the tag at the top of the ancestors stack

        return boost::none if the stack is empty
     */
    boost::optional<Tag>
    parentTag() const;

    /** return true if the ancestor at the top of the stack represents an auto
        sequence

        @note an auto sequence is an ASN.1 sequence that has autogenerated
              tag numbers
     */
    bool
    parentIsAutoSequence() const;

    /** return true if the ancestor at the top of the stack represents an ASN.1
        choice
     */ 
    bool
    parentIsChoice() const;

    /** return the portion of the buffer that represents the parent value */
    Slice&
    parentSlice();

    /** Decode values from the encoder into variables

       @note The forwarding ref is used for some value types to support
       std::tie, SetWrapper, and SequenceWrapper (i.e. `s >> make_set(some_vec)`)

    @{
    */
    friend
    Decoder&
    operator&(Decoder& s, Eos e)
    {
        s.eos();
        return s;
    }

    friend
    Decoder&
    operator&(Decoder& s, Preamble& p)
    {
        if (s.ec_)
            return s;

        decodePreamble(s.parentSlice(), p, s.ec_);
        return s;
    }

    template <class T>
    friend
    Decoder&
    operator&(Decoder& s, T&& v)
    {
        if (s.ec_)
            return s;

        using traits = DerCoderTraits<std::decay_t<T>>;
        auto const groupType = traits::groupType();
        if (s.parentIsAutoSequence())
        {
            if (groupType == GroupType::choice)
            {
                auto& numChildren = std::get<std::uint32_t>(s.ancestors_.top());
                Tag const tag1{
                    ClassId::contextSpecific, numChildren++, traits::primitive()};
                GroupGuard<Decoder> g1(s, tag1, GroupType::sequenceChild);
                if (s.ec_)
                    return s;
                boost::optional<Tag> tag2;
                if (auto const tagNum = traits::tagNum())
                    tag2.emplace(traits{}, *tagNum);
                GroupGuard<Decoder> g2(s, tag2, groupType);
                if (s.ec_)
                    return s;
                traits::decode(s, v);
            }
            else
            {
                auto& numChildren = std::get<std::uint32_t>(s.ancestors_.top());
                Tag const tag{
                    ClassId::contextSpecific, numChildren++, traits::primitive()};
                GroupGuard<Decoder> g(s, tag, groupType);
                if (s.ec_)
                    return s;
                traits::decode(s, v);
            }
        }
        else
        {
            boost::optional<Tag> tag;
            if (auto const tagNum = traits::tagNum())
                tag.emplace(traits{}, *tagNum);
            GroupGuard<Decoder> g(s, tag, groupType);
            if (s.ec_)
                return s;
            traits::decode(s, v);
        }

        return s;
    }

    template <class T>
    friend Decoder&
    operator>>(Decoder& s, T&& t)
    {
        return s & std::forward<T>(t);
    }
    /** @} */
};

//------------------------------------------------------------------------------

/** For types that define `withTuple`, encode the type.

    @note If the user defined type defines the function `withTuple`, then
    `withTupleEncodeHelper`, `withTupleDecodeHelper`, and
    `withTupleEncodeHelper` may be used to help implement the DerCoderTraits
    functions `encode`, `decode`, and `length`. The `withTuple` function should
    take a single parameter: a callback function. That callback function should
    take a single parameter, a tuple of references that represent the object being
    coded.
 */
template<class TChoice>
void
withTupleEncodeHelper(TChoice const& c, cryptoconditions::der::Encoder& encoder)
{
    c.withTuple(
        [&encoder](auto const& tup) { encoder << tup; },
        encoder.traitsCache_);
}

/** For types that define `withTuple`, decode the type.

    @see note on {@link #withTupleEncodeHelper}
 */
template<class TChoice>
void
withTupleDecodeHelper(TChoice& c, cryptoconditions::der::Decoder& decoder)
{
    TraitsCache dummy; // traits cache is not used in decoding
    c.withTuple([&decoder](auto&& tup) { decoder >> tup; },
                dummy);
}

/** For types that define `withTuple`, find the length, in bytes, of the encoded content.

    @see note on {@link #withTupleEncodeHelper}
 */
template <class TChoice>
std::uint64_t
withTupleEncodedLengthHelper(
    TChoice const& c,
    boost::optional<GroupType> const& parentGroupType,
    TagMode encoderTagMode,
    TraitsCache& traitsCache)
{
    std::uint64_t result = 0;
    boost::optional<GroupType> thisGroupType(GroupType::sequence);
    c.withTuple(
        [&](auto const& tup) {
            using T = std::decay_t<decltype(tup)>;
            using Traits = cryptoconditions::der::DerCoderTraits<T>;
            result =
                Traits::length(tup, thisGroupType, encoderTagMode, traitsCache);
        },
        traitsCache);
    return result;
}

/** For types that define `withTuple`, compare the type.

    @see note on {@link #withTupleEncodeHelper}
 */
template <class TChoiceDerived, class TChoiceBase>
int
withTupleCompareHelper(
    TChoiceDerived const& lhs,
    TChoiceBase const& rhs,
    TraitsCache& traitsCache)
{
    auto const lhsType = lhs.type();
    auto const rhsType = rhs.type();
    if (lhsType != rhsType)
    {
        if (lhsType < rhsType)
            return -1;
        return 1;
    }

    auto const pRhs = dynamic_cast<TChoiceDerived const*>(&rhs);
    if (!pRhs)
    {
        assert(0);
        return -1;
    }

    int result = 0;
    lhs.withTuple(
        [&](auto const& lhsTup) {
            pRhs->withTuple(
                [&](auto const& rhsTup) {
                    using traits =
                        DerCoderTraits<std::decay_t<decltype(lhsTup)>>;
                    result = traits::compare(lhsTup, rhsTup, traitsCache);
                },
                traitsCache);
        },
        traitsCache);
    return result;
}


}  // der
}  // cryptoconditions
}  // ripple

#endif
