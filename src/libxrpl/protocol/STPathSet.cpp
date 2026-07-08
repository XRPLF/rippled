#include <xrpl/protocol/STPathSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl {

std::size_t
STPathElement::getHash(STPathElement const& element)
{
    std::size_t hashAccount = 2654435761;
    std::size_t hashCurrency = 2654435761;
    std::size_t hashIssuer = 2654435761;

    // NIKB NOTE: This doesn't have to be a secure hash as speed is more
    //            important. We don't even really need to fully hash the whole
    //            base_uint here, as a few bytes would do for our use.

    for (auto const x : element.getAccountID())
        hashAccount += (hashAccount * 257) ^ x;

    // Check pathAsset type instead of element's type_
    // In some cases type_ might be account but the asset
    // is still set to either MPT or currency (see Pathfinder::addLink())
    element.getPathAsset().visit(
        [&](MPTID const& mpt) { hashCurrency += beast::Uhash<>{}(mpt); },
        [&](Currency const& currency) {
            for (auto const x : currency)
                hashCurrency += (hashCurrency * 509) ^ x;
        });

    for (auto const x : element.getIssuerID())
        hashIssuer += (hashIssuer * 911) ^ x;

    return (hashAccount ^ hashCurrency ^ hashIssuer);
}

STPathSet::STPathSet(SerialIter& sit, SField const& name) : STBase(name)
{
    std::vector<STPathElement> path;
    for (;;)
    {
        int const iType = sit.get8();

        if (iType == STPathElement::TypeNone || iType == STPathElement::TypeBoundary)
        {
            if (path.empty())
            {
                JLOG(debugLog().error()) << "Empty path in pathset";
                Throw<std::runtime_error>("empty path");
            }

            pushBack(path);
            path.clear();

            if (iType == STPathElement::TypeNone)
                return;
        }
        else if ((iType & ~STPathElement::TypeAll) != 0)
        {
            JLOG(debugLog().error()) << "Bad path element " << iType << " in pathset";
            Throw<std::runtime_error>("bad path element");
        }
        else
        {
            auto const hasAccount = (iType & STPathElement::TypeAccount) != 0u;
            auto const hasCurrency = (iType & STPathElement::TypeCurrency) != 0u;
            auto const hasIssuer = (iType & STPathElement::TypeIssuer) != 0u;
            auto const hasMPT = (iType & STPathElement::TypeMpt) != 0u;

            AccountID account;
            PathAsset asset;
            AccountID issuer;

            if (hasAccount)
                account = sit.get160();

            if (hasCurrency && hasMPT)
            {
                JLOG(debugLog().error()) << "Bad path element MPT and Currency in pathset";
                Throw<std::runtime_error>("bad path element: MPT and Currency");
            }

            if (hasCurrency)
                asset = Currency::fromRaw(sit.get160());

            if (hasMPT)
                asset = sit.get192();

            if (hasIssuer)
                issuer = sit.get160();

            path.emplace_back(account, asset, issuer, hasCurrency || hasMPT);
        }
    }
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
    value_.push_back(base);

    std::vector<STPath>::reverse_iterator it = value_.rbegin();

    STPath& newPath = *it;
    newPath.pushBack(tail);

    while (++it != value_.rend())
    {
        if (*it == newPath)
        {
            value_.pop_back();
            return false;
        }
    }
    return true;
}

bool
STPathSet::isEquivalent(STBase const& t) const
{
    STPathSet const* v = dynamic_cast<STPathSet const*>(&t);
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
    for (auto& p : path_)
    {
        if (p.getAccountID() == account && p.getPathAsset() == asset && p.getIssuerID() == issuer)
            return true;
    }

    return false;
}

json::Value
STPath::getJson(JsonOptions) const
{
    json::Value ret(json::ValueType::Array);

    for (auto const& it : path_)
    {
        json::Value elem(json::ValueType::Object);
        auto const iType = it.getNodeType();

        elem[jss::type] = iType;

        if ((iType & STPathElement::TypeAccount) != 0u)
            elem[jss::account] = to_string(it.getAccountID());

        XRPL_ASSERT(
            ((iType & STPathElement::TypeCurrency) == 0u) ||
                ((iType & STPathElement::TypeMpt) == 0u),
            "xrpl::STPath::getJson : not type Currency and MPT");
        if ((iType & STPathElement::TypeCurrency) != 0u)
            elem[jss::currency] = to_string(it.getCurrency());

        if ((iType & STPathElement::TypeMpt) != 0u)
            elem[jss::mpt_issuance_id] = to_string(it.getMPTID());

        if ((iType & STPathElement::TypeIssuer) != 0u)
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

    for (auto const& spPath : value_)
    {
        if (!first)
            s.add8(STPathElement::TypeBoundary);

        for (auto const& speElement : spPath)
        {
            int const iType = speElement.getNodeType();

            s.add8(iType);

            if ((iType & STPathElement::TypeAccount) != 0u)
                s.addBitString(speElement.getAccountID());

            if ((iType & STPathElement::TypeMpt) != 0u)
                s.addBitString(speElement.getMPTID());

            if ((iType & STPathElement::TypeCurrency) != 0u)
                s.addBitString(speElement.getCurrency());

            if ((iType & STPathElement::TypeIssuer) != 0u)
                s.addBitString(speElement.getIssuerID());
        }

        first = false;
    }

    s.add8(STPathElement::TypeNone);
}

}  // namespace xrpl
