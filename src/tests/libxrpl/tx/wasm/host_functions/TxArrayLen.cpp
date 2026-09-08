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

struct TxArrayLenImpl : RealHostFixture
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
            memos.push_back(makeMemo(RealHostFixture::toBytes("world")));
            obj.setFieldArray(sfMemos, memos);
        };
        return makeHost(keylet::account(acct.id()), assembler.type, std::move(assembler.build));
    }
};

TEST_F(TxArrayLenImpl, MemosLength)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(h->getTxArrayLen(sfMemos), 2);
}

TEST_F(TxArrayLenImpl, CredentialIdsLength)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(h->getTxArrayLen(sfCredentialIDs), 1);
}

TEST_F(TxArrayLenImpl, NonArrayFieldNoArray)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(h->getTxArrayLen(sfAccount), HostFunctionError::NoArray);
}

TEST_F(TxArrayLenImpl, MissingArrayFieldNotFound)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(h->getTxArrayLen(sfSigners), HostFunctionError::FieldNotFound);
}

}  // namespace xrpl::test
