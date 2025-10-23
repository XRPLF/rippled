#ifndef XRPL_PROTOCOL_STACCOUNT_H_INCLUDED
#define XRPL_PROTOCOL_STACCOUNT_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STBase.h>

#include <string>

namespace ripple {

class STAccount final : public STBase, public CountedObject<STAccount>
{
private:
    // The original implementation of STAccount kept the value in an STBlob.
    // But an STAccount is always 160 bits, so we can store it with less
    // overhead in a ripple::uint160.  However, so the serialized format of the
    // STAccount stays unchanged, we serialize and deserialize like an STBlob.
    AccountID value_;
    bool default_;

public:
    using value_type = AccountID;

    STAccount();

    STAccount(SField const& n);
    STAccount(SField const& n, Buffer&& v);
    STAccount(SerialIter& sit, SField const& name);
    STAccount(SField const& n, AccountID const& v);

    SerializedTypeID
    getSType() const override;

    std::string
    getText() const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(STBase const& t) const override;

    bool
    isDefault() const override;

    STAccount&
    operator=(AccountID const& value);

    AccountID const&
    value() const noexcept;

    void
    setValue(AccountID const& v);

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

inline STAccount&
STAccount::operator=(AccountID const& value)
{
    setValue(value);
    return *this;
}

inline AccountID const&
STAccount::value() const noexcept
{
    return value_;
}

inline void
STAccount::setValue(AccountID const& v)
{
    value_ = v;
    default_ = false;
}

inline bool
operator==(STAccount const& lhs, STAccount const& rhs)
{
    return lhs.value() == rhs.value();
}

inline auto
operator<(STAccount const& lhs, STAccount const& rhs)
{
    return lhs.value() < rhs.value();
}

inline bool
operator==(STAccount const& lhs, AccountID const& rhs)
{
    return lhs.value() == rhs;
}

inline auto
operator<(STAccount const& lhs, AccountID const& rhs)
{
    return lhs.value() < rhs;
}

inline auto
operator<(AccountID const& lhs, STAccount const& rhs)
{
    return lhs < rhs.value();
}

}  // namespace ripple

#endif
