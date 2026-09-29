#include <xrpl/protocol/STAccount.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

STAccount::STAccount()
{
}

STAccount::STAccount(SField const& n) : STBase(n)
{
}

STAccount::STAccount(SerialIter& sit, SField const& name) : STAccount(name)
{
    if (auto const s = sit.getVL(); !s.empty())
    {
        // Throwing from constructors can be awkward, but this is only
        // called from STVar::STVar (SerialIter&, SField const&) which
        // also throws.
        auto const id = AccountID::fromRaw(s);

        if (!id) [[unlikely]]
            Throw<std::runtime_error>("Invalid STAccount size");

        value_ = *id;
        default_ = false;
    }
}

STAccount::STAccount(SField const& n, AccountID const& v) : STBase(n), value_(v), default_(false)
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
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STAccount::add : field is binary");
    XRPL_ASSERT(getFName().fieldType == STI_ACCOUNT, "xrpl::STAccount::add : valid field type");

    // Preserve the serialization behavior of an STBlob:
    //  o If we are default (all zeros) serialize as an empty blob.
    //  o Otherwise serialize 160 bits.
    if (isDefault())
    {
        s.addVL(Slice{});
        return;
    }

    s.addVL(Slice{value_.data(), value_.size()});
}

bool
STAccount::isEquivalent(STBase const& t) const
{
    auto const* const tPtr = dynamic_cast<STAccount const*>(&t);
    return (tPtr != nullptr) && (default_ == tPtr->default_) && (value_ == tPtr->value_);
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

}  // namespace xrpl
