#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <utility>

namespace xrpl::test {

struct LedgerObjArrayLenImpl : RealHostFixture
{
    using RealHostFixture::makeHost;

    WasmHost
    makeHost(Account const& acct)
    {
        makeSignerList(acct, 2, {{Account{"alice"}, 1}, {Account{"becky"}, 1}});
        auto assembler = bareTx();
        auto h = makeHost(keylet::account(AccountID{}), assembler.type, std::move(assembler.build));
        EXPECT_TRUE(h->cacheLedgerObj(keylet::signerList(acct.id()).key, 1).has_value());
        return h;
    }
};

TEST_F(LedgerObjArrayLenImpl, SignerEntriesLength)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(h->getLedgerObjArrayLen(1, sfSignerEntries), 2);
}

TEST_F(LedgerObjArrayLenImpl, NonArrayFieldNoArray)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(h->getLedgerObjArrayLen(1, sfAccount), HostFunctionError::NoArray);
}

TEST_F(LedgerObjArrayLenImpl, MissingArrayFieldNotFound)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(h->getLedgerObjArrayLen(1, sfMemos), HostFunctionError::FieldNotFound);
}

TEST_F(LedgerObjArrayLenImpl, SlotErrors)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(h->getLedgerObjArrayLen(0, sfSignerEntries), HostFunctionError::SlotOutRange);
    expectError(h->getLedgerObjArrayLen(257, sfSignerEntries), HostFunctionError::SlotOutRange);
    expectError(h->getLedgerObjArrayLen(2, sfSignerEntries), HostFunctionError::EmptySlot);
}

}  // namespace xrpl::test
