#ifndef XRPL_TEST_JTX_DID_H_INCLUDED
#define XRPL_TEST_JTX_DID_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>

namespace ripple {
namespace test {
namespace jtx {

/** DID operations. */
namespace did {

Json::Value
set(jtx::Account const& account);

Json::Value
setValid(jtx::Account const& account);

/** Sets the optional DIDDocument on a DIDSet. */
class document
{
private:
    std::string document_;

public:
    explicit document(std::string const& u) : document_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfDIDDocument.jsonName] = document_;
    }
};

/** Sets the optional URI on a DIDSet. */
class uri
{
private:
    std::string uri_;

public:
    explicit uri(std::string const& u) : uri_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfURI.jsonName] = uri_;
    }
};

/** Sets the optional Attestation on a DIDSet. */
class data
{
private:
    std::string data_;

public:
    explicit data(std::string const& u) : data_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfData.jsonName] = data_;
    }
};

Json::Value
del(jtx::Account const& account);

}  // namespace did

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
