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

namespace ripple {

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

    for (auto const x : element.getCurrency())
        hash_currency += (hash_currency * 509) ^ x;

    for (auto const x : element.getIssuerID())
        hash_issuer += (hash_issuer * 911) ^ x;

    return (hash_account ^ hash_currency ^ hash_issuer);
}

STPathSet::STPathSet(SerialIter& sit, SField const& name) : STBase(name)
{
    std::vector<STPathElement> path;
    for (;;)
    {
        int iType = sit.get8();

        if (iType == STPathElement::typeNone ||
            iType == STPathElement::typeBoundary)
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
        else if (iType & ~STPathElement::typeAll)
        {
            JLOG(debugLog().error())
                << "Bad path element " << iType << " in pathset";
            Throw<std::runtime_error>("bad path element");
        }
        else
        {
            auto hasAccount = iType & STPathElement::typeAccount;
            auto hasCurrency = iType & STPathElement::typeCurrency;
            auto hasIssuer = iType & STPathElement::typeIssuer;

            AccountID account;
            Currency currency;
            AccountID issuer;

            if (hasAccount)
                account = sit.get160();

            if (hasCurrency)
                currency = sit.get160();

            if (hasIssuer)
                issuer = sit.get160();

            path.emplace_back(account, currency, issuer, hasCurrency);
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
    return v && (value == v->value);
}

bool
STPathSet::isDefault() const
{
    return value.empty();
}

bool
STPath::hasSeen(
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer) const
{
    for (auto& p : mPath)
    {
        if (p.getAccountID() == account && p.getCurrency() == currency &&
            p.getIssuerID() == issuer)
            return true;
    }

    return false;
}

Json::Value
STPath::getJson(JsonOptions) const
{
    Json::Value ret(Json::arrayValue);

    for (auto it : mPath)
    {
        Json::Value elem(Json::objectValue);
        auto const iType = it.getNodeType();

        elem[jss::type] = iType;

        if (iType & STPathElement::typeAccount)
            elem[jss::account] = to_string(it.getAccountID());

        if (iType & STPathElement::typeCurrency)
            elem[jss::currency] = to_string(it.getCurrency());

        if (iType & STPathElement::typeIssuer)
            elem[jss::issuer] = to_string(it.getIssuerID());

        ret.append(elem);
    }

    return ret;
}

Json::Value
STPathSet::getJson(JsonOptions options) const
{
    Json::Value ret(Json::arrayValue);
    for (auto it : value)
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
    XRPL_ASSERT(
        getFName().isBinary(), "ripple::STPathSet::add : field is binary");
    XRPL_ASSERT(
        getFName().fieldType == STI_PATHSET,
        "ripple::STPathSet::add : valid field type");
    bool first = true;

    for (auto const& spPath : value)
    {
        if (!first)
            s.add8(STPathElement::typeBoundary);

        for (auto const& speElement : spPath)
        {
            int iType = speElement.getNodeType();

            s.add8(iType);

            if (iType & STPathElement::typeAccount)
                s.addBitString(speElement.getAccountID());

            if (iType & STPathElement::typeCurrency)
                s.addBitString(speElement.getCurrency());

            if (iType & STPathElement::typeIssuer)
                s.addBitString(speElement.getIssuerID());
        }

        first = false;
    }

    s.add8(STPathElement::typeNone);
}

}  // namespace ripple
