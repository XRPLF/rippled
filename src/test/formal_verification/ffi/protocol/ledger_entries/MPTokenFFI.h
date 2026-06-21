#pragma once

#include <test/formal_verification/ffi/protocol/AcceptedCredentialFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/AssetFFI.h>
#include <test/formal_verification/ffi/protocol/MptIdFFI.h>
#include <test/formal_verification/ffi/protocol/STAmountFFI.h>
#include <test/formal_verification/ffi/protocol/STNumberFFI.h>
#include <test/formal_verification/ffi/protocol/UInt128FFI.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol_autogen/ledger_entries/MPToken.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_mptoken_empty(lean_object* unit);

lean_object*
lean_mptoken_key_get(lean_object* o);
lean_object*
lean_mptoken_key_set(lean_object* o, lean_object* key);
uint32_t
lean_mptoken_flags_get(lean_object* o);
lean_object*
lean_mptoken_flags_set(lean_object* o, uint32_t flags);
lean_object*
lean_mptoken_account_get(lean_object* o);
lean_object*
lean_mptoken_account_set(lean_object* o, lean_object* account);
lean_object*
lean_mptoken_issuance_id_get(lean_object* o);
lean_object*
lean_mptoken_issuance_id_set(lean_object* o, lean_object* issuanceID);
uint64_t
lean_mptoken_amount_get(lean_object* o);
lean_object*
lean_mptoken_amount_set(lean_object* o, uint64_t amount);
lean_object*
lean_mptoken_locked_amount_get(lean_object* o);
lean_object*
lean_mptoken_locked_amount_set(lean_object* o, lean_object* lockedAmount);
uint64_t
lean_mptoken_owner_node_get(lean_object* o);
lean_object*
lean_mptoken_owner_node_set(lean_object* o, uint64_t ownerNode);
lean_object*
lean_mptoken_previous_txn_id_get(lean_object* o);
lean_object*
lean_mptoken_previous_txn_id_set(lean_object* o, lean_object* previousTxnID);
uint32_t
lean_mptoken_previous_txn_lgr_seq_get(lean_object* o);
lean_object*
lean_mptoken_previous_txn_lgr_seq_set(lean_object* o, uint32_t previousTxnLgrSeq);
}

namespace xrpl::test::formal_verification {

class MPTokenFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    uint256
    key() const
    {
        return leanGetObj<UInt256FFI>(lean_mptoken_key_get);
    }
    uint32_t
    flags() const
    {
        return leanGet<uint32_t>(lean_mptoken_flags_get);
    }
    AccountID
    account() const
    {
        return leanGetObj<AccountIDFFI>(lean_mptoken_account_get);
    }
    MPTID
    issuanceID() const
    {
        return leanGetObj<MptIdFFI>(lean_mptoken_issuance_id_get);
    }
    uint64_t
    amount() const
    {
        return leanGet<uint64_t>(lean_mptoken_amount_get);
    }
    std::optional<uint64_t>
    lockedAmount() const
    {
        return leanGetOptU64(lean_mptoken_locked_amount_get);
    }
    uint64_t
    ownerNode() const
    {
        return leanGet<uint64_t>(lean_mptoken_owner_node_get);
    }
    uint256
    previousTxnID() const
    {
        return leanGetObj<UInt256FFI>(lean_mptoken_previous_txn_id_get);
    }
    uint32_t
    previousTxnLgrSeq() const
    {
        return leanGet<uint32_t>(lean_mptoken_previous_txn_lgr_seq_get);
    }

    ledger_entries::MPToken
    toCpp() const
    {
        ledger_entries::MPTokenBuilder b(
            account(), issuanceID(), ownerNode(), previousTxnID(), previousTxnLgrSeq());
        b.setFlags(flags());
        if (uint64_t v = amount(); v != 0)
            b.setMPTAmount(v);
        if (auto v = lockedAmount())
            b.setLockedAmount(*v);
        return b.build(key());
    }
};

class MPTokenFFIBuilder : public LeanObjectFFI
{
public:
    MPTokenFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_mptoken_empty))
    {
    }

    MPTokenFFIBuilder&
    flags(uint32_t v)
    {
        leanSet(lean_mptoken_flags_set, v);
        return *this;
    }
    MPTokenFFIBuilder&
    account(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_mptoken_account_set, v);
        return *this;
    }
    MPTokenFFIBuilder&
    issuanceID(MPTID const& v)
    {
        leanSetObj<MptIdFFI>(lean_mptoken_issuance_id_set, v);
        return *this;
    }
    MPTokenFFIBuilder&
    amount(uint64_t v)
    {
        leanSet(lean_mptoken_amount_set, v);
        return *this;
    }
    MPTokenFFIBuilder&
    lockedAmount(uint64_t v)
    {
        leanSetOptU64(lean_mptoken_locked_amount_set, v);
        return *this;
    }
    MPTokenFFIBuilder&
    ownerNode(uint64_t v)
    {
        leanSet(lean_mptoken_owner_node_set, v);
        return *this;
    }
    MPTokenFFIBuilder&
    previousTxnID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_mptoken_previous_txn_id_set, v);
        return *this;
    }
    MPTokenFFIBuilder&
    previousTxnLgrSeq(uint32_t v)
    {
        leanSet(lean_mptoken_previous_txn_lgr_seq_set, v);
        return *this;
    }

    MPTokenFFI
    build(uint256 const& key)
    {
        leanSetObj<UInt256FFI>(lean_mptoken_key_set, key);
        return leanBuildAs<MPTokenFFI>();
    }

    MPTokenFFIBuilder&
    fromCpp(ledger_entries::MPToken const& c)
    {
        flags(c.getFlags());
        account(c.getAccount());
        issuanceID(c.getMPTokenIssuanceID());
        if (auto v = c.getMPTAmount())
            amount(*v);
        if (auto v = c.getLockedAmount())
            lockedAmount(*v);
        ownerNode(c.getOwnerNode());
        previousTxnID(c.getPreviousTxnID());
        previousTxnLgrSeq(c.getPreviousTxnLgrSeq());
        return *this;
    }
};

}  // namespace xrpl::test::formal_verification
