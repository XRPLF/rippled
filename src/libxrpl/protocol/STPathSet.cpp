#include <xrpl/protocol/STPathSet.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/detail/STVar.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl {

// For guidance on deciding which option to pursue:
// 1. Try to decrease the size of the STPathSet first.  For instance, if a std::optional was
//    injected into the type, could you get the same functionality using a std::unique_ptr instead?
// 2. If the size of the STPathSet is already as small as it can be, then consider what the cost
//    of increasing STVar::kMaxSize would be on all the other STVar types.  Each of those types
//    will carry the additional cost of accommodating the larger STPathSet in their SBO.
// 3. If the cost of increasing STVar::kMaxSize is too high, then heap allocate the STPathSet and
//    remove this static_assert.
static_assert(
    sizeof(STPathSet) <= detail::STVar::kMaxSize,
    "STPathSet is too large to fit in STVar's small object optimization. Please verify if it "
    "should, if the kMaxSize should be increased, or if STPathSet should be stored on the heap "
    "instead of in STVar.");

STPathSet::STPathSet(DeduplicationTag) : seen_{std::make_unique<HardenedHashSet<STPath>>()}
{
}

STPathSet::STPathSet(STPathSet const& other)
    : STBase{other}
    , CountedObject<STPathSet>{other}
    , value_{other.value_}
    , seen_{
          other.seen_ != nullptr ? std::make_unique<HardenedHashSet<STPath>>(*other.seen_)
                                 : nullptr,
      }
{
}

STPathSet&
STPathSet::operator=(STPathSet const& other)
{
    if (this == &other)
    {
        return *this;
    }
    auto newSeen =
        other.seen_ != nullptr ? std::make_unique<HardenedHashSet<STPath>>(*other.seen_) : nullptr;
    STBase::operator=(other);
    CountedObject<STPathSet>::operator=(other);
    value_ = other.value_;
    seen_ = std::move(newSeen);
    return *this;
}

STPathSet::STPathSet(SerialIter& sit, SField const& name) : STBase(name)
{
    std::vector<STPathElement> path;

    STPathElementType type = STPathElementType::None;

    do
    {
        type = safeCast<STPathElementType>(sit.get8());

        if (type == STPathElementType::None || type == STPathElementType::Boundary)
        {
            if (path.empty())
                Throw<std::runtime_error>("bad path element: empty path");

            // Move rather than converting the vector to an STPath by copy.
            value_.emplace_back(std::move(path));
            path.clear();

            continue;
        }

        if ((type & ~STPathElementType::All) != STPathElementType::None)
            Throw<std::runtime_error>(
                std::format("bad path element: <{:#04x}>", std::to_underlying(type)));

        bool const hasAccount = (type & STPathElementType::Account) == STPathElementType::Account;
        bool const hasCurrency =
            (type & STPathElementType::Currency) == STPathElementType::Currency;
        bool const hasMPT = (type & STPathElementType::Mpt) == STPathElementType::Mpt;
        bool const hasIssuer = (type & STPathElementType::Issuer) == STPathElementType::Issuer;

        if (hasCurrency && hasMPT)
            Throw<std::runtime_error>("bad path element: MPT and Currency");

        AccountID account;
        PathAsset asset;
        AccountID issuer;

        if (hasAccount)
            account = sit.get160();

        if (hasCurrency)
            asset = Currency::fromRaw(sit.get160());

        if (hasMPT)
            asset = sit.get192();

        if (hasIssuer)
            issuer = sit.get160();

        path.emplace_back(account, asset, issuer, hasCurrency || hasMPT);

    } while (type != STPathElementType::None);
}

STBase*
STPathSet::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STPathSet::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

bool
STPathSet::assembleAdd(STPath const& base, STPathElement const& tail)
{  // assemble base+tail and add it to the set if it's not a duplicate
    XRPL_ASSERT(seen_ != nullptr, "xrpl::STPathSet::assembleAdd : DeduplicationTag");
    STPath combined = base;
    combined.pushBack(tail);
    return appendUnique([&](auto& value) { value.push_back(std::move(combined)); });
}

bool
STPathSet::isEquivalent(STBase const& t) const
{
    auto const* v = dynamic_cast<STPathSet const*>(&t);
    return (v != nullptr) && (value_ == v->value_);
}

bool
STPathSet::isDefault() const
{
    return value_.empty();
}

bool
STPath::hasSeen(AccountID const& account, PathAsset const& asset, AccountID const& issuer) const
{
    return std::ranges::any_of(path_, [&](auto& p) {
        return p.getAccountID() == account && p.getPathAsset() == asset &&
            p.getIssuerID() == issuer;
    });
}

json::Value
STPath::getJson(JsonOptions) const
{
    json::Value ret(json::ValueType::Array);

    for (auto const& it : path_)
    {
        json::Value elem(json::ValueType::Object);

        elem[jss::type] = safeCast<std::uint32_t>(it.getNodeType());

        if (it.isType(STPathElementType::Account))
            elem[jss::account] = to_string(it.getAccountID());

        XRPL_ASSERT(
            !(it.hasCurrency() && it.hasMPT()),
            "xrpl::STPath::getJson : not type Currency and MPT");
        if (it.hasCurrency())
            elem[jss::currency] = to_string(it.getCurrency());

        if (it.hasMPT())
            elem[jss::mpt_issuance_id] = to_string(it.getMPTID());

        if (it.hasIssuer())
            elem[jss::issuer] = to_string(it.getIssuerID());

        ret.append(elem);
    }

    return ret;
}

json::Value
STPathSet::getJson(JsonOptions options) const
{
    json::Value ret(json::ValueType::Array);
    for (auto const& it : value_)
        ret.append(it.getJson(options));

    return ret;
}

SerializedTypeID
STPathSet::getSType() const
{
    return STI_PATHSET;
}

void
STPathSet::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STPathSet::add : field is binary");
    XRPL_ASSERT(getFName().fieldType == STI_PATHSET, "xrpl::STPathSet::add : valid field type");
    bool first = true;

    // Workaround, since `Serializer` does not accept strongly-typed enums
    // yet. When support is added, the static_assert will trigger, and the
    // lambda can be removed entirely.
    auto const toByte = [](std::same_as<STPathElementType> auto t) noexcept {
        static_assert(
            !requires(Serializer& ser) { ser.add8(t); },
            "Serializer::add8 now accepts STPathElementType directly; remove toByte.");
        return std::to_underlying(t);
    };

    for (auto const& spPath : value_)
    {
        if (!first)
            s.add8(toByte(STPathElementType::Boundary));

        for (auto const& speElement : spPath)
        {
            s.add8(toByte(speElement.getNodeType()));

            if (speElement.isType(STPathElementType::Account))
                s.addBitString(speElement.getAccountID());

            if (speElement.hasMPT())
                s.addBitString(speElement.getMPTID());

            if (speElement.hasCurrency())
                s.addBitString(speElement.getCurrency());

            if (speElement.hasIssuer())
                s.addBitString(speElement.getIssuerID());
        }

        first = false;
    }

    s.add8(toByte(STPathElementType::None));
}

}  // namespace xrpl
