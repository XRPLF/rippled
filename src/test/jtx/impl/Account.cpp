#include <test/jtx/Account.h>
#include <test/jtx/amount.h>

#include <xrpl/protocol/UintTypes.h>

namespace ripple {
namespace test {
namespace jtx {

std::unordered_map<std::pair<std::string, KeyType>, Account, beast::uhash<>>
    Account::cache_;

Account const Account::master(
    "master",
    generateKeyPair(KeyType::secp256k1, generateSeed("masterpassphrase")),
    Account::privateCtorTag{});

Account::Account(
    std::string name,
    std::pair<PublicKey, SecretKey> const& keys,
    Account::privateCtorTag)
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
    auto const iter = cache_.find(p);
    if (iter != cache_.end())
        return iter->second;

    auto const keys = [stringType, &name, type]() {
        // Special handling for base58Seeds.
        if (stringType == base58Seed)
        {
            std::optional<Seed> const seed = parseBase58<Seed>(name);
            if (!seed.has_value())
                Throw<std::runtime_error>("Account:: invalid base58 seed");

            return generateKeyPair(type, *seed);
        }
        return generateKeyPair(type, generateSeed(name));
    }();
    auto r = cache_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(std::move(p)),
        std::forward_as_tuple(std::move(name), keys, privateCtorTag{}));
    return r.first->second;
}

Account::Account(std::string name, KeyType type)
    : Account(fromCache(Account::other, std::move(name), type))
{
}

Account::Account(AcctStringType stringType, std::string base58SeedStr)
    : Account(fromCache(
          Account::base58Seed,
          std::move(base58SeedStr),
          KeyType::secp256k1))
{
}

Account::Account(std::string name, AccountID const& id)
    : Account(name, randomKeyPair(KeyType::secp256k1), privateCtorTag{})
{
    // override the randomly generated values
    id_ = id;
    human_ = toBase58(id_);
}

IOU
Account::operator[](std::string const& s) const
{
    auto const currency = to_currency(s);
    assert(currency != noCurrency());
    return IOU(*this, currency);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
