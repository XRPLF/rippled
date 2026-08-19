#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

#include <utility>

namespace xrpl::test {

struct LedgerObjNestedArrayLenImpl : RealHostFixture
{
    using RealHostFixture::makeHost;

    WasmHost
    makeHost(Account const& acct)
    {
        makeSignerList(acct, 2, {{Account{"alice"}, 1}, {Account{"becky"}, 1}});
        auto assembler = bareTx();
        auto h = makeHost(keylet::account(AccountID{}), assembler.type, std::move(assembler.build));
        h->cacheLedgerObj(keylet::signerList(acct.id()).key, 1);
        return h;
    }
};

TEST_F(LedgerObjNestedArrayLenImpl, SignerEntriesLength)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(h->getLedgerObjNestedArrayLen(1, FieldLocator{{sfSignerEntries.getCode()}}), 2);
}

TEST_F(LedgerObjNestedArrayLenImpl, NonArrayFieldNoArray)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedArrayLen(1, FieldLocator{{sfSignerQuorum.getCode()}}),
        HostFunctionError::NoArray);
}

TEST_F(LedgerObjNestedArrayLenImpl, MissingFieldNotFound)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedArrayLen(1, FieldLocator{{sfSigners.getCode()}}),
        HostFunctionError::FieldNotFound);
}

TEST_F(LedgerObjNestedArrayLenImpl, SlotErrors)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedArrayLen(0, FieldLocator{{sfSignerEntries.getCode()}}),
        HostFunctionError::SlotOutRange);
    expectError(
        h->getLedgerObjNestedArrayLen(257, FieldLocator{{sfSignerEntries.getCode()}}),
        HostFunctionError::SlotOutRange);
    expectError(
        h->getLedgerObjNestedArrayLen(2, FieldLocator{{sfSignerEntries.getCode()}}),
        HostFunctionError::EmptySlot);
}

TEST_F(LedgerObjNestedArrayLenImpl, NestIntoNonContainerMalformed)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getLedgerObjNestedArrayLen(
            1, FieldLocator{{sfSignerQuorum.getCode(), 0, sfAccount.getCode()}}),
        HostFunctionError::LocatorMalformed);
}

}  // namespace xrpl::test
