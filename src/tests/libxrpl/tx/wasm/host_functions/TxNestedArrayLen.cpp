#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <utility>

namespace xrpl::test {

struct TxNestedArrayLenImpl : RealHostFixture
{
    using RealHostFixture::makeHost;

    WasmHost
    makeHost(Account const& acct)
    {
        auto assembler = escrowFinishTx(ledger, acct);
        assembler.build = [inner = std::move(assembler.build)](STObject& obj) {
            inner(obj);
            auto memos = STArray{};
            memos.push_back(makeMemo(RealHostFixture::toBytes("hello")));
            obj.setFieldArray(sfMemos, memos);
        };
        return makeHost(keylet::account(acct.id()), assembler.type, std::move(assembler.build));
    }
};

TEST_F(TxNestedArrayLenImpl, MemosLength)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(h->getTxNestedArrayLen(FieldLocator{{sfMemos.getCode()}}), 1);
}

TEST_F(TxNestedArrayLenImpl, CredentialIdsLength)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(h->getTxNestedArrayLen(FieldLocator{{sfCredentialIDs.getCode()}}), 1);
}

TEST_F(TxNestedArrayLenImpl, NonArrayFieldNoArray)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getTxNestedArrayLen(FieldLocator{{sfAccount.getCode()}}), HostFunctionError::NoArray);
}

TEST_F(TxNestedArrayLenImpl, MissingFieldNotFound)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getTxNestedArrayLen(FieldLocator{{sfSigners.getCode()}}),
        HostFunctionError::FieldNotFound);
}

}  // namespace xrpl::test
