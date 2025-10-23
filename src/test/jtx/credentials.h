#ifndef XRPL_TEST_JTX_CREDENTIALS_H_INCLUDED
#define XRPL_TEST_JTX_CREDENTIALS_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>

namespace ripple {
namespace test {
namespace jtx {

namespace credentials {

inline Keylet
keylet(
    test::jtx::Account const& subject,
    test::jtx::Account const& issuer,
    std::string_view credType)
{
    return keylet::credential(
        subject.id(), issuer.id(), Slice(credType.data(), credType.size()));
}

// Sets the optional URI.
class uri
{
private:
    std::string const uri_;

public:
    explicit uri(std::string_view u) : uri_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfURI.jsonName] = uri_;
    }
};

// Set credentialsIDs array
class ids
{
private:
    std::vector<std::string> const credentials_;

public:
    explicit ids(std::vector<std::string> const& creds) : credentials_(creds)
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        auto& arr(jtx.jv[sfCredentialIDs.jsonName] = Json::arrayValue);
        for (auto const& hash : credentials_)
            arr.append(hash);
    }
};

Json::Value
create(
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType);

Json::Value
accept(
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType);

Json::Value
deleteCred(
    jtx::Account const& acc,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType);

Json::Value
ledgerEntry(
    jtx::Env& env,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType);

Json::Value
ledgerEntry(jtx::Env& env, std::string const& credIdx);

}  // namespace credentials
}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
