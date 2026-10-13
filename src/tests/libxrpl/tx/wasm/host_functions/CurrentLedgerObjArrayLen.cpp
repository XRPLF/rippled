#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>
#include <tx/wasm/fixtures/WasmLedger.h>

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

TEST_F(CurrentLedgerObjArrayLenImpl, signer_entries_length)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(h->getCurrentLedgerObjArrayLen(sfSignerEntries), 2);
}

TEST_F(CurrentLedgerObjArrayLenImpl, non_array_field_no_array)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(h->getCurrentLedgerObjArrayLen(sfAccount), HostFunctionError::NoArray);
}

TEST_F(CurrentLedgerObjArrayLenImpl, missing_array_field_not_found)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(h->getCurrentLedgerObjArrayLen(sfMemos), HostFunctionError::FieldNotFound);
}

TEST_F(CurrentLedgerObjArrayLenImpl, missing_current_object_not_found)
{
    auto const owner = fund("owner");
    auto assembler = bareTx();
    auto h = makeHost(keylet::signerList(owner.id()), assembler.type, std::move(assembler.build));
    expectError(
        h->getCurrentLedgerObjArrayLen(sfSignerEntries), HostFunctionError::LedgerObjNotFound);
}

}  // namespace xrpl::test
