#ifndef XRPL_PROTOCOL_STINTEGER_H_INCLUDED
#define XRPL_PROTOCOL_STINTEGER_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/STBase.h>

namespace ripple {

template <typename Integer>
class STInteger : public STBase, public CountedObject<STInteger<Integer>>
{
public:
    using value_type = Integer;

private:
    Integer value_;

public:
    explicit STInteger(Integer v);
    STInteger(SField const& n, Integer v = 0);
    STInteger(SerialIter& sit, SField const& name);

    SerializedTypeID
    getSType() const override;

    Json::Value getJson(JsonOptions) const override;

    std::string
    getText() const override;

    void
    add(Serializer& s) const override;

    bool
    isDefault() const override;

    bool
    isEquivalent(STBase const& t) const override;

    STInteger&
    operator=(value_type const& v);

    value_type
    value() const noexcept;

    void
    setValue(Integer v);

    operator Integer() const;

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class ripple::detail::STVar;
};

using STUInt8 = STInteger<unsigned char>;
using STUInt16 = STInteger<std::uint16_t>;
using STUInt32 = STInteger<std::uint32_t>;
using STUInt64 = STInteger<std::uint64_t>;

using STInt32 = STInteger<std::int32_t>;

template <typename Integer>
inline STInteger<Integer>::STInteger(Integer v) : value_(v)
{
}

template <typename Integer>
inline STInteger<Integer>::STInteger(SField const& n, Integer v)
    : STBase(n), value_(v)
{
}

template <typename Integer>
inline STBase*
STInteger<Integer>::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

template <typename Integer>
inline STBase*
STInteger<Integer>::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

template <typename Integer>
inline void
STInteger<Integer>::add(Serializer& s) const
{
    XRPL_ASSERT(
        getFName().isBinary(), "ripple::STInteger::add : field is binary");
    XRPL_ASSERT(
        getFName().fieldType == getSType(),
        "ripple::STInteger::add : field type match");
    s.addInteger(value_);
}

template <typename Integer>
inline bool
STInteger<Integer>::isDefault() const
{
    return value_ == 0;
}

template <typename Integer>
inline bool
STInteger<Integer>::isEquivalent(STBase const& t) const
{
    STInteger const* v = dynamic_cast<STInteger const*>(&t);
    return v && (value_ == v->value_);
}

template <typename Integer>
inline STInteger<Integer>&
STInteger<Integer>::operator=(value_type const& v)
{
    value_ = v;
    return *this;
}

template <typename Integer>
inline typename STInteger<Integer>::value_type
STInteger<Integer>::value() const noexcept
{
    return value_;
}

template <typename Integer>
inline void
STInteger<Integer>::setValue(Integer v)
{
    value_ = v;
}

template <typename Integer>
inline STInteger<Integer>::operator Integer() const
{
    return value_;
}

}  // namespace ripple

#endif
