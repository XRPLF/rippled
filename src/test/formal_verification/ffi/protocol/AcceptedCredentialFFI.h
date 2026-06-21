#pragma once

#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>

#include <xrpl/basics/Blob.h>

#include <lean/lean.h>

#include <cstdint>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_accepted_credential_build(lean_object* issuer, lean_object* credentialType);
lean_object*
lean_accepted_credential_issuer_get(lean_object* ac);
lean_object*
lean_accepted_credential_type_get(lean_object* ac);
lean_object*
lean_accepted_credential_list_empty(lean_object* unit);
lean_object*
lean_accepted_credential_list_append(lean_object* list, lean_object* item);
}

namespace xrpl::test::formal_verification {

// A Lean `List AcceptedCredential` ↔ rippled (issuer, credentialType) pairs
class AcceptedCredentialFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = std::vector<std::pair<AccountID, Blob>>;

    static AcceptedCredentialFFI
    build(CppType const& creds)
    {
        lean_object* list = leanEmptyOf(lean_accepted_credential_list_empty);
        for (auto const& [iss, ct] : creds)
            list = leanCall(
                lean_accepted_credential_list_append,
                list,
                leanCall(
                    lean_accepted_credential_build,
                    AccountIDFFI::build(iss),
                    mkBytes(ct.data(), ct.size())));
        return AcceptedCredentialFFI(list);
    }

    CppType
    read() const
    {
        CppType result;
        leanForEach(raw(), [&](lean_object* ac) {
            AccountID iss = AccountIDFFI(lean_accepted_credential_issuer_get(retain(ac))).read();
            LeanObjectFFI ct(lean_accepted_credential_type_get(retain(ac)));
            uint8_t const* p = lean_sarray_cptr(ct.raw());
            result.emplace_back(iss, Blob(p, p + lean_sarray_size(ct.raw())));
        });
        return result;
    }
};

static_assert(LeanWrapper<AcceptedCredentialFFI>);

}  // namespace xrpl::test::formal_verification
