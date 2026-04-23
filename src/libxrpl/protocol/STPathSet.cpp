#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl {

std::size_t
STPathElement::get_hash(STPathElement const& element)
{
    std::size_t hash_account = 2654435761;
    std::size_t hash_currency = 2654435761;
    std::size_t hash_issuer = 2654435761;

    // NIKB NOTE: This doesn't have to be a secure hash as speed is more
    //            important. We don't even really need to fully hash the whole
    //            base_uint here, as a few bytes would do for our use.

    for (auto const x : element.getAccountID())
        hash_account += (hash_account * 257) ^ x;

    // Check pathAsset type instead of element's mType
    // In some cases mType might be account but the asset
    // is still set to either MPT or currency (see Pathfinder::addLink())
    element.getPathAsset().visit(
        [&](MPTID const& mpt) { hash_currency += beast::uhash<>{}(mpt); },
        [&](Currency const& currency) {
            for (auto const x : currency)
                hash_currency += (hash_currency * 509) ^ x;
        });

    for (auto const x : element.getIssuerID())
        hash_issuer += (hash_issuer * 911) ^ x;

    return (hash_account ^ hash_currency ^ hash_issuer);
}

STPathSet::STPathSet(SerialIter& sit, SField const& name) : STBase(name)
{
    std::vector<STPathElement> path;
    for (;;)
    {
        int const iType = sit.get8();

        if (iType == STPathElement::typeNone || iType == STPathElement::typeBoundary)
        {
            if (path.empty())
            {
                JLOG(debugLog().error()) << "Empty path in pathset";
                Throw<std::runtime_error>("empty path");
            }

            push_back(path);
            path.clear();

            if (iType == STPathElement::typeNone)
                return;
        }
        else if ((iType & ~STPathElement::typeAll) != 0)
        {
            JLOG(debugLog().error()) << "Bad path element " << iType << " in pathset";
            Throw<std::runtime_error>("bad path element");
        }
        else
        {
            auto const hasAccount = (iType & STPathElement::typeAccount) != 0u;
            auto const hasCurrency = (iType & STPathElement::typeCurrency) != 0u;
            auto const hasIssuer = (iType & STPathElement::typeIssuer) != 0u;
            auto const hasMPT = (iType & STPathElement::typeMPT) != 0u;

            AccountID account;
            PathAsset asset;
            AccountID issuer;

            if (hasAccount)
                account = sit.get160();

            XRPL_ASSERT(
                !(hasCurrency && hasMPT), "xrpl::STPathSet::STPathSet : not has Currency and MPT");
            if (hasCurrency)
                asset = static_cast<Currency>(sit.get160());

            if (hasMPT)
                asset = sit.get192();

            if (hasIssuer)
                issuer = sit.get160();

            path.emplace_back(account, asset, issuer, hasCurrency);
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
    value.push_back(base);

    std::vector<STPath>::reverse_iterator it = value.rbegin();

    STPath& newPath = *it;
    newPath.push_back(tail);

    while (++it != value.rend())
    {
        if (*it == newPath)
        {
            value.pop_back();
            return false;
        }
    }
    return true;
}

bool
STPathSet::isEquivalent(STBase const& t) const
{
    STPathSet const* v = dynamic_cast<STPathSet const*>(&t);
    return (v != nullptr) && (value == v->value);
}

bool
STPathSet::isDefault() const
{
    return value.empty();
}

bool
STPath::hasSeen(AccountID const& account, PathAsset const& asset, AccountID const& issuer) const
{
    for (auto& p : mPath)
    {
        if (p.getAccountID() == account && p.getPathAsset() == asset && p.getIssuerID() == issuer)
            return true;
    }

    return false;
}

Json::Value
STPath::getJson(JsonOptions) const
{
    Json::Value ret(Json::arrayValue);

    for (auto const& it : mPath)
    {
        Json::Value elem(Json::objectValue);
        auto const iType = it.getNodeType();

        elem[jss::type] = iType;

        if ((iType & STPathElement::typeAccount) != 0u)
            elem[jss::account] = to_string(it.getAccountID());

        XRPL_ASSERT(
            ((iType & STPathElement::typeCurrency) == 0u) ||
                ((iType & STPathElement::typeMPT) == 0u),
            "xrpl::STPath::getJson : not type Currency and MPT");
        if ((iType & STPathElement::typeCurrency) != 0u)
            elem[jss::currency] = to_string(it.getCurrency());

        if ((iType & STPathElement::typeMPT) != 0u)
            elem[jss::mpt_issuance_id] = to_string(it.getMPTID());

        if ((iType & STPathElement::typeIssuer) != 0u)
            elem[jss::issuer] = to_string(it.getIssuerID());

        ret.append(elem);
    }

    return ret;
}

Json::Value
STPathSet::getJson(JsonOptions options) const
{
    Json::Value ret(Json::arrayValue);
    for (auto const& it : value)
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

    for (auto const& spPath : value)
    {
        if (!first)
            s.add8(STPathElement::typeBoundary);

        for (auto const& speElement : spPath)
        {
            int const iType = speElement.getNodeType();

            s.add8(iType);

            if ((iType & STPathElement::typeAccount) != 0u)
                s.addBitString(speElement.getAccountID());

            if ((iType & STPathElement::typeMPT) != 0u)
                s.addBitString(speElement.getMPTID());

            if ((iType & STPathElement::typeCurrency) != 0u)
                s.addBitString(speElement.getCurrency());

            if ((iType & STPathElement::typeIssuer) != 0u)
                s.addBitString(speElement.getIssuerID());
        }

        first = false;
    }

    s.add8(STPathElement::typeNone);
}

}  // namespace xrpl
