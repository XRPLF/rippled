#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealVmTest.h>

#include <cstdint>
#include <format>
#include <string>

namespace xrpl::test {

// A contract reads a field of its current ledger object (a real escrow) end to end: the real
// VM runs the guest, `HostContext` marshals the field code into an `SField`, the real impl
// reads the real ledger, and the byte count comes back to the guest. `host_calls/` proves the
// marshalling with a mock and `host_functions/` proves the impl's answer without a VM; this
// proves the two agree over a real ledger.
struct CurrentLedgerObjFieldE2e : RealVmTest
{
    // Create a real escrow owned by `owner` and return its keylet — the object the contract
    // runs against.
    Keylet
    makeEscrow(Account const& owner, Account const& dest)
    {
        ledger.createAccount(owner, XRP(1000));
        ledger.createAccount(dest, XRP(1000));
        auto const ownerSeq = ledger.getAccountRoot(owner.id()).getSequence();
        auto const r = ledger.submit(
            transactions::EscrowCreateBuilder{owner.id(), dest.id(), XRP(100)}.setFinishAfter(
                900'000'000),
            owner);
        EXPECT_EQ(r.ter, tesSUCCESS) << transToken(r.ter);
        ledger.close();
        return keylet::escrow(owner.id(), SeqProxy::rawSequence(ownerSeq));
    }
};

TEST_F(CurrentLedgerObjFieldE2e, ContractReadsAFieldOfItsRealEscrow)
{
    auto const owner = Account{"owner"};
    auto const escrow = makeEscrow(owner, Account{"dest"});

    // Ask the current object for `sfAccount` and return the byte count the host wrote — 20 for
    // an account id — proving the read reached the real ledger and came back through the VM.
    auto const wat = std::format(
        R"wat(
(module
  (import "host_lib" "home_le_field" (func $home_le_field (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (call $home_le_field (i32.const {}) (i32.const 0) (i32.const 32))))
)wat",
        sfAccount.getCode());

    auto const outcome = run(wat, escrow);
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    EXPECT_EQ(outcome->result, static_cast<std::int32_t>(toBytes(owner.id()).size()));
}

}  // namespace xrpl::test
