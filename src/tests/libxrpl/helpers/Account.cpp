#include <helpers/Account.h>

#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>

namespace xrpl::test {

Account const Account::kMaster{"masterpassphrase"};

Account::Account(std::string_view name, KeyType type)
    : name_(name)
    , keyPair_(generateKeyPair(type, generateSeed(name_)))
    , id_(calcAccountID(keyPair_.first))
{
}

}  // namespace xrpl::test
