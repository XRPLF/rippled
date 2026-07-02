#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>

#include <string>

/** DID operations. */
namespace xrpl::test::jtx::did {

json::Value
set(jtx::Account const& account);

json::Value
setValid(jtx::Account const& account);

/** Sets the optional DIDDocument on a DIDSet. */
class Document
{
private:
    std::string document_;

public:
    explicit Document(std::string const& u) : document_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfDIDDocument.jsonName] = document_;
    }
};

/** Sets the optional URI on a DIDSet. */
class Uri
{
private:
    std::string uri_;

public:
    explicit Uri(std::string const& u) : uri_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfURI.jsonName] = uri_;
    }
};

/** Sets the optional Data on a DIDSet. */
class Data
{
private:
    std::string data_;

public:
    explicit Data(std::string const& u) : data_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfData.jsonName] = data_;
    }
};

json::Value
del(jtx::Account const& account);

}  // namespace xrpl::test::jtx::did
