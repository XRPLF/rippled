#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <cstdint>
#include <limits>
#include <utility>

namespace xrpl::test {

struct TxNestedFieldImpl : RealHostFixture
{
    TxAssembler
    assemble(Account const& acct)
    {
        auto assembler = escrowFinishTx(ledger, acct);
        assembler.build = [inner = std::move(assembler.build)](STObject& obj) {
            inner(obj);
            auto memos = STArray{};
            auto memo = STObject::makeInnerObject(sfMemo);
            memo.setFieldVL(sfMemoData, Slice{"hello", 5});
            memos.push_back(std::move(memo));
            obj.setFieldArray(sfMemos, memos);
        };
        return assembler;
    }

    using RealHostFixture::makeHost;

    WasmHost
    makeHost(Account const& acct)
    {
        auto assembler = assemble(acct);
        return makeHost(keylet::account(acct.id()), assembler.type, std::move(assembler.build));
    }
};

TEST_F(TxNestedFieldImpl, MatchesNestedMemo)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getTxNestedField(FieldLocator{{sfMemos.getCode(), 0, sfMemoData.getCode()}}),
        RealHostFixture::toBytes("hello"));
}

TEST_F(TxNestedFieldImpl, MatchesCredId)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getTxNestedField(FieldLocator{{sfCredentialIDs.getCode(), 0}}),
        RealHostFixture::toBytes(credentialId()));
}

TEST_F(TxNestedFieldImpl, MatchesBaseFieldViaNestedLocator)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getTxNestedField(FieldLocator{{sfAccount.getCode()}}),
        RealHostFixture::toBytes(owner.id()));
}

TEST_F(TxNestedFieldImpl, MissingFieldNotFound)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::FieldNotFound;

    expectError(
        h->getTxNestedField(FieldLocator{{sfSigners.getCode(), 0, sfAccount.getCode()}}), err);
    expectError(h->getTxNestedField(FieldLocator{{sfMemos.getCode(), 0, sfURI.getCode()}}), err);
    expectError(h->getTxNestedField(FieldLocator{{sfMemos.getCode(), 0, -1}}), err);
    expectError(h->getTxNestedField(FieldLocator{{-1, 0, sfAccount.getCode()}}), err);
    expectError(h->getTxNestedField(FieldLocator{{0, 0, sfAccount.getCode()}}), err);
}

TEST_F(TxNestedFieldImpl, IndexOutOfBounds)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::IndexOutOfBounds;

    expectError(
        h->getTxNestedField(FieldLocator{{sfMemos.getCode(), 1, sfMemoData.getCode()}}), err);
    expectError(h->getTxNestedField(FieldLocator{{sfCredentialIDs.getCode(), 1}}), err);
    expectError(
        h->getTxNestedField(FieldLocator{{sfMemos.getCode(), -1, sfMemoData.getCode()}}), err);
    expectError(h->getTxNestedField(FieldLocator{{sfCredentialIDs.getCode(), -1}}), err);
}

TEST_F(TxNestedFieldImpl, UnknownFieldCodeInvalidField)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::InvalidField;

    expectError(
        h->getTxNestedField(FieldLocator{{fieldCode(20000, 20000), 0, sfAccount.getCode()}}), err);
    expectError(
        h->getTxNestedField(FieldLocator{{sfMemos.getCode(), 0, fieldCode(20000, 20000)}}), err);
    // Far-negative code: not in the SField map at all.
    expectError(
        h->getTxNestedField(
            FieldLocator{{std::numeric_limits<std::int32_t>::min(), 0, sfAccount.getCode()}}),
        err);
}

TEST_F(TxNestedFieldImpl, ContainerWithoutIndexNotLeaf)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::NotLeafField;

    expectError(h->getTxNestedField(FieldLocator{{sfMemos.getCode()}}), err);
    expectError(h->getTxNestedField(FieldLocator{{sfCredentialIDs.getCode()}}), err);
}

TEST_F(TxNestedFieldImpl, NestIntoNonContainerMalformed)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getTxNestedField(FieldLocator{{sfAccount.getCode(), 0, sfAccount.getCode()}}),
        HostFunctionError::LocatorMalformed);
}

}  // namespace xrpl::test
