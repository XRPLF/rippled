#include <test/jtx/Account.h>

#include <test/jtx/amount.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/UintTypes.h>

#include <cassert>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace xrpl::test::jtx {

std::unordered_map<std::pair<std::string, KeyType>, Account, beast::Uhash<>> Account::cache;

Account const Account::kMaster(
    "master",
    generateKeyPair(KeyType::Secp256k1, generateSeed("masterpassphrase")),
    Account::PrivateCtorTag{});

Account::Account(
    std::string name,
    std::pair<PublicKey, SecretKey> const& keys,
    Account::PrivateCtorTag)
    : name_(std::move(name))
    , pk_(keys.first)
    , sk_(keys.second)
    , id_(calcAccountID(pk_))
    , human_(toBase58(id_))
{
}

Account
Account::fromCache(AcctStringType stringType, std::string name, KeyType type)
{
    auto p = std::make_pair(name, type);  // non-const so it can be moved from
    auto const iter = cache.find(p);
    if (iter != cache.end())
        return iter->second;

    auto const keys = [stringType, &name, type]() {
        // Special handling for base58Seeds.
        if (stringType == AcctStringType::Base58Seed)
        {
            std::optional<Seed> const seed = parseBase58<Seed>(name);
            if (!seed.has_value())
                Throw<std::runtime_error>("Account:: invalid base58 seed");

            return generateKeyPair(type, *seed);
        }
        return generateKeyPair(type, generateSeed(name));
    }();
    auto r = cache.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(std::move(p)),
        std::forward_as_tuple(std::move(name), keys, PrivateCtorTag{}));
    return r.first->second;
}

Account::Account(std::string name, KeyType type)
    : Account(fromCache(Account::AcctStringType::Other, std::move(name), type))
{
}

Account::Account(AcctStringType stringType, std::string base58SeedStr)
    : Account(fromCache(
          Account::AcctStringType::Base58Seed,
          std::move(base58SeedStr),
          KeyType::Secp256k1))
{
}

Account::Account(std::string name, AccountID const& id)
    : Account(name, randomKeyPair(KeyType::Secp256k1), PrivateCtorTag{})
{
    // override the randomly generated values
    id_ = id;
    human_ = toBase58(id_);
}

IOU
Account::operator[](std::string const& s) const
{
    auto const currency = toCurrency(s);
    assert(currency != noCurrency());
    return IOU(*this, currency);
}

}  // namespace xrpl::test::jtx
