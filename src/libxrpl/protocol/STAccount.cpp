#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace ripple {

STAccount::STAccount() : STBase(), value_(beast::zero), default_(true)
{
}

STAccount::STAccount(SField const& n)
    : STBase(n), value_(beast::zero), default_(true)
{
}

STAccount::STAccount(SField const& n, Buffer&& v) : STAccount(n)
{
    if (v.empty())
        return;  // Zero is a valid size for a defaulted STAccount.

    // Is it safe to throw from this constructor?  Today (November 2015)
    // the only place that calls this constructor is
    //    STVar::STVar (SerialIter&, SField const&)
    // which throws.  If STVar can throw in its constructor, then so can
    // STAccount.
    if (v.size() != uint160::bytes)
        Throw<std::runtime_error>("Invalid STAccount size");

    default_ = false;
    memcpy(value_.begin(), v.data(), uint160::bytes);
}

STAccount::STAccount(SerialIter& sit, SField const& name)
    : STAccount(name, sit.getVLBuffer())
{
}

STAccount::STAccount(SField const& n, AccountID const& v)
    : STBase(n), value_(v), default_(false)
{
}

STBase*
STAccount::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STAccount::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STAccount::getSType() const
{
    return STI_ACCOUNT;
}

void
STAccount::add(Serializer& s) const
{
    XRPL_ASSERT(
        getFName().isBinary(), "ripple::STAccount::add : field is binary");
    XRPL_ASSERT(
        getFName().fieldType == STI_ACCOUNT,
        "ripple::STAccount::add : valid field type");

    // Preserve the serialization behavior of an STBlob:
    //  o If we are default (all zeros) serialize as an empty blob.
    //  o Otherwise serialize 160 bits.
    int const size = isDefault() ? 0 : uint160::bytes;
    s.addVL(value_.data(), size);
}

bool
STAccount::isEquivalent(STBase const& t) const
{
    auto const* const tPtr = dynamic_cast<STAccount const*>(&t);
    return tPtr && (default_ == tPtr->default_) && (value_ == tPtr->value_);
}

bool
STAccount::isDefault() const
{
    return default_;
}

std::string
STAccount::getText() const
{
    if (isDefault())
        return "";
    return toBase58(value());
}

}  // namespace ripple
