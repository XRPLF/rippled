#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>

namespace xrpl::test::jtx::credentials {

inline Keylet
keylet(
    test::jtx::Account const& subject,
    test::jtx::Account const& issuer,
    std::string_view credType)
{
    return keylet::credential(subject.id(), issuer.id(), Slice(credType.data(), credType.size()));
}

// Sets the optional URI.
class Uri
{
private:
    std::string const uri_;

public:
    explicit Uri(std::string_view u) : uri_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfURI.jsonName] = uri_;
    }
};

// Set credentialsIDs array
class Ids
{
private:
    std::vector<std::string> const credentials_;

public:
    explicit Ids(std::vector<std::string> creds) : credentials_(std::move(creds))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        auto& arr(jtx.jv[sfCredentialIDs.jsonName] = json::ValueType::Array);
        for (auto const& hash : credentials_)
            arr.append(hash);
    }
};

json::Value
create(jtx::Account const& subject, jtx::Account const& issuer, std::string_view credType);

json::Value
accept(jtx::Account const& subject, jtx::Account const& issuer, std::string_view credType);

json::Value
deleteCred(
    jtx::Account const& acc,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType);

json::Value
ledgerEntry(
    jtx::Env& env,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType);

json::Value
ledgerEntry(jtx::Env& env, std::string const& credIdx);

}  // namespace xrpl::test::jtx::credentials
