#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

#include <utility>

namespace xrpl::test {

struct CurrentLedgerObjArrayLenImpl : RealHostFixture
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

TEST_F(CurrentLedgerObjArrayLenImpl, SignerEntriesLength)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(h->getCurrentLedgerObjArrayLen(sfSignerEntries), 2);
}

TEST_F(CurrentLedgerObjArrayLenImpl, NonArrayFieldNoArray)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(h->getCurrentLedgerObjArrayLen(sfAccount), HostFunctionError::NoArray);
}

TEST_F(CurrentLedgerObjArrayLenImpl, MissingArrayFieldNotFound)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(h->getCurrentLedgerObjArrayLen(sfMemos), HostFunctionError::FieldNotFound);
}

TEST_F(CurrentLedgerObjArrayLenImpl, MissingCurrentObjectNotFound)
{
    auto const owner = fund("owner");
    auto assembler = bareTx();
    auto h = makeHost(keylet::signerList(owner.id()), assembler.type, std::move(assembler.build));
    expectError(
        h->getCurrentLedgerObjArrayLen(sfSignerEntries), HostFunctionError::LedgerObjNotFound);
}

}  // namespace xrpl::test
