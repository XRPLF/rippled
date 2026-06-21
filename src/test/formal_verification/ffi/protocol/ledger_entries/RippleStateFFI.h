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
#include <xrpl/protocol_autogen/ledger_entries/RippleState.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_ripple_state_empty(lean_object* unit);

lean_object*
lean_ripple_state_key_get(lean_object* o);
lean_object*
lean_ripple_state_key_set(lean_object* o, lean_object* key);
uint32_t
lean_ripple_state_flags_get(lean_object* o);
lean_object*
lean_ripple_state_flags_set(lean_object* o, uint32_t flags);
lean_object*
lean_ripple_state_balance_get(lean_object* o);
lean_object*
lean_ripple_state_balance_set(lean_object* o, lean_object* balance);
lean_object*
lean_ripple_state_low_limit_get(lean_object* o);
lean_object*
lean_ripple_state_low_limit_set(lean_object* o, lean_object* lowLimit);
lean_object*
lean_ripple_state_high_limit_get(lean_object* o);
lean_object*
lean_ripple_state_high_limit_set(lean_object* o, lean_object* highLimit);
lean_object*
lean_ripple_state_previous_txn_id_get(lean_object* o);
lean_object*
lean_ripple_state_previous_txn_id_set(lean_object* o, lean_object* previousTxnID);
uint32_t
lean_ripple_state_previous_txn_lgr_seq_get(lean_object* o);
lean_object*
lean_ripple_state_previous_txn_lgr_seq_set(lean_object* o, uint32_t previousTxnLgrSeq);
lean_object*
lean_ripple_state_low_node_get(lean_object* o);
lean_object*
lean_ripple_state_low_node_set(lean_object* o, lean_object* lowNode);
lean_object*
lean_ripple_state_low_quality_in_get(lean_object* o);
lean_object*
lean_ripple_state_low_quality_in_set(lean_object* o, lean_object* lowQualityIn);
lean_object*
lean_ripple_state_low_quality_out_get(lean_object* o);
lean_object*
lean_ripple_state_low_quality_out_set(lean_object* o, lean_object* lowQualityOut);
lean_object*
lean_ripple_state_high_node_get(lean_object* o);
lean_object*
lean_ripple_state_high_node_set(lean_object* o, lean_object* highNode);
lean_object*
lean_ripple_state_high_quality_in_get(lean_object* o);
lean_object*
lean_ripple_state_high_quality_in_set(lean_object* o, lean_object* highQualityIn);
lean_object*
lean_ripple_state_high_quality_out_get(lean_object* o);
lean_object*
lean_ripple_state_high_quality_out_set(lean_object* o, lean_object* highQualityOut);
}

namespace xrpl::test::formal_verification {

class RippleStateFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    uint256
    key() const
    {
        return leanGetObj<UInt256FFI>(lean_ripple_state_key_get);
    }
    uint32_t
    flags() const
    {
        return leanGet<uint32_t>(lean_ripple_state_flags_get);
    }
    STAmount
    balance() const
    {
        return leanGetObj<STAmountFFI>(lean_ripple_state_balance_get);
    }
    STAmount
    lowLimit() const
    {
        return leanGetObj<STAmountFFI>(lean_ripple_state_low_limit_get);
    }
    STAmount
    highLimit() const
    {
        return leanGetObj<STAmountFFI>(lean_ripple_state_high_limit_get);
    }
    uint256
    previousTxnID() const
    {
        return leanGetObj<UInt256FFI>(lean_ripple_state_previous_txn_id_get);
    }
    uint32_t
    previousTxnLgrSeq() const
    {
        return leanGet<uint32_t>(lean_ripple_state_previous_txn_lgr_seq_get);
    }
    std::optional<uint64_t>
    lowNode() const
    {
        return leanGetOptU64(lean_ripple_state_low_node_get);
    }
    std::optional<uint32_t>
    lowQualityIn() const
    {
        return leanGetOptU32(lean_ripple_state_low_quality_in_get);
    }
    std::optional<uint32_t>
    lowQualityOut() const
    {
        return leanGetOptU32(lean_ripple_state_low_quality_out_get);
    }
    std::optional<uint64_t>
    highNode() const
    {
        return leanGetOptU64(lean_ripple_state_high_node_get);
    }
    std::optional<uint32_t>
    highQualityIn() const
    {
        return leanGetOptU32(lean_ripple_state_high_quality_in_get);
    }
    std::optional<uint32_t>
    highQualityOut() const
    {
        return leanGetOptU32(lean_ripple_state_high_quality_out_get);
    }

    ledger_entries::RippleState
    toCpp() const
    {
        ledger_entries::RippleStateBuilder b(
            balance(), lowLimit(), highLimit(), previousTxnID(), previousTxnLgrSeq());
        b.setFlags(flags());
        if (auto v = lowNode())
            b.setLowNode(*v);
        if (auto v = lowQualityIn())
            b.setLowQualityIn(*v);
        if (auto v = lowQualityOut())
            b.setLowQualityOut(*v);
        if (auto v = highNode())
            b.setHighNode(*v);
        if (auto v = highQualityIn())
            b.setHighQualityIn(*v);
        if (auto v = highQualityOut())
            b.setHighQualityOut(*v);
        return b.build(key());
    }
};

class RippleStateFFIBuilder : public LeanObjectFFI
{
public:
    RippleStateFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_ripple_state_empty))
    {
    }

    RippleStateFFIBuilder&
    flags(uint32_t v)
    {
        leanSet(lean_ripple_state_flags_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    balance(STAmount const& v)
    {
        leanSetObj<STAmountFFI>(lean_ripple_state_balance_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    lowLimit(STAmount const& v)
    {
        leanSetObj<STAmountFFI>(lean_ripple_state_low_limit_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    highLimit(STAmount const& v)
    {
        leanSetObj<STAmountFFI>(lean_ripple_state_high_limit_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    previousTxnID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_ripple_state_previous_txn_id_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    previousTxnLgrSeq(uint32_t v)
    {
        leanSet(lean_ripple_state_previous_txn_lgr_seq_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    lowNode(uint64_t v)
    {
        leanSetOptU64(lean_ripple_state_low_node_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    lowQualityIn(uint32_t v)
    {
        leanSetOptU32(lean_ripple_state_low_quality_in_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    lowQualityOut(uint32_t v)
    {
        leanSetOptU32(lean_ripple_state_low_quality_out_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    highNode(uint64_t v)
    {
        leanSetOptU64(lean_ripple_state_high_node_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    highQualityIn(uint32_t v)
    {
        leanSetOptU32(lean_ripple_state_high_quality_in_set, v);
        return *this;
    }
    RippleStateFFIBuilder&
    highQualityOut(uint32_t v)
    {
        leanSetOptU32(lean_ripple_state_high_quality_out_set, v);
        return *this;
    }

    RippleStateFFI
    build(uint256 const& key)
    {
        leanSetObj<UInt256FFI>(lean_ripple_state_key_set, key);
        return leanBuildAs<RippleStateFFI>();
    }

    RippleStateFFIBuilder&
    fromCpp(ledger_entries::RippleState const& c)
    {
        flags(c.getFlags());
        balance(c.getBalance());
        lowLimit(c.getLowLimit());
        highLimit(c.getHighLimit());
        previousTxnID(c.getPreviousTxnID());
        previousTxnLgrSeq(c.getPreviousTxnLgrSeq());
        if (auto v = c.getLowNode())
            lowNode(*v);
        if (auto v = c.getLowQualityIn())
            lowQualityIn(*v);
        if (auto v = c.getLowQualityOut())
            lowQualityOut(*v);
        if (auto v = c.getHighNode())
            highNode(*v);
        if (auto v = c.getHighQualityIn())
            highQualityIn(*v);
        if (auto v = c.getHighQualityOut())
            highQualityOut(*v);
        return *this;
    }
};

}  // namespace xrpl::test::formal_verification
