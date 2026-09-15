#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <utility>

namespace xrpl::test {

struct CurrentLedgerObjNestedArrayLenImpl : RealHostFixture
{
    using RealHostFixture::makeHost;

    WasmHost
    makeHost(Account const& acct)
    {
        makeSignerList(acct, 2, {{Account{"alice"}, 1}, {Account{"becky"}, 1}});
        auto assembler = bareTx();
        return makeHost(keylet::signerList(acct.id()), assembler.type, std::move(assembler.build));
    }
};

TEST_F(CurrentLedgerObjNestedArrayLenImpl, SignerEntriesLength)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(h->getCurrentLedgerObjNestedArrayLen(FieldLocator{{sfSignerEntries.getCode()}}), 2);
}

TEST_F(CurrentLedgerObjNestedArrayLenImpl, NonArrayFieldNoArray)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getCurrentLedgerObjNestedArrayLen(FieldLocator{{sfSignerQuorum.getCode()}}),
        HostFunctionError::NoArray);
}

TEST_F(CurrentLedgerObjNestedArrayLenImpl, MissingFieldNotFound)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getCurrentLedgerObjNestedArrayLen(FieldLocator{{sfSigners.getCode()}}),
        HostFunctionError::FieldNotFound);
}

TEST_F(CurrentLedgerObjNestedArrayLenImpl, MissingCurrentObjectNotFound)
{
    auto const owner = fund("owner");
    auto assembler = bareTx();
    auto h = makeHost(keylet::signerList(owner.id()), assembler.type, std::move(assembler.build));
    expectError(
        h->getCurrentLedgerObjNestedArrayLen(FieldLocator{{sfSignerEntries.getCode()}}),
        HostFunctionError::LedgerObjNotFound);
}

}  // namespace xrpl::test
