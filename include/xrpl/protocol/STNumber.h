#ifndef XRPL_PROTOCOL_STNUMBER_H_INCLUDED
#define XRPL_PROTOCOL_STNUMBER_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Number.h>
#include <xrpl/protocol/STBase.h>

#include <ostream>

namespace ripple {

/**
 * A serializable number.
 *
 * This type is-a `Number`, and can be used everywhere that is accepted.
 * This type simply integrates `Number` with the serialization framework,
 * letting it be used for fields in ledger entries and transactions.
 * It is effectively an `STAmount` sans `Asset`:
 * it can represent a value of any token type (XRP, IOU, or MPT)
 * without paying the storage cost of duplicating asset information
 * that may be deduced from the context.
 */
class STNumber : public STBase, public CountedObject<STNumber>
{
private:
    Number value_;

public:
    using value_type = Number;

    STNumber() = default;
    explicit STNumber(SField const& field, Number const& value = Number());
    STNumber(SerialIter& sit, SField const& field);

    SerializedTypeID
    getSType() const override;
    std::string
    getText() const override;
    void
    add(Serializer& s) const override;

    Number const&
    value() const;
    void
    setValue(Number const& v);

    STNumber&
    operator=(Number const& rhs)
    {
        setValue(rhs);
        return *this;
    }

    bool
    isEquivalent(STBase const& t) const override;
    bool
    isDefault() const override;

    operator Number() const
    {
        return value_;
    }

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;
};

std::ostream&
operator<<(std::ostream& out, STNumber const& rhs);

struct NumberParts
{
    std::uint64_t mantissa = 0;
    int exponent = 0;
    bool negative = false;
};

NumberParts
partsFromString(std::string const& number);

STNumber
numberFromJson(SField const& field, Json::Value const& value);

}  // namespace ripple

#endif
