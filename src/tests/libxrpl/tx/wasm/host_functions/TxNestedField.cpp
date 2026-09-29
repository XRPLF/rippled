#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/OwnedLocator.h>
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

TEST_F(TxNestedFieldImpl, matches_nested_memo)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getTxNestedField(locator({sfMemos.getCode(), 0, sfMemoData.getCode()})),
        RealHostFixture::toBytes("hello"));
}

TEST_F(TxNestedFieldImpl, matches_cred_id)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getTxNestedField(locator({sfCredentialIDs.getCode(), 0})),
        RealHostFixture::toBytes(credentialId()));
}

TEST_F(TxNestedFieldImpl, matches_base_field_via_nested_locator)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getTxNestedField(locator({sfAccount.getCode()})), RealHostFixture::toBytes(owner.id()));
}

TEST_F(TxNestedFieldImpl, missing_field_not_found)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::FieldNotFound;

    expectError(h->getTxNestedField(locator({sfSigners.getCode(), 0, sfAccount.getCode()})), err);
    expectError(h->getTxNestedField(locator({sfMemos.getCode(), 0, sfURI.getCode()})), err);
    expectError(h->getTxNestedField(locator({sfMemos.getCode(), 0, -1})), err);
    expectError(h->getTxNestedField(locator({-1, 0, sfAccount.getCode()})), err);
    expectError(h->getTxNestedField(locator({0, 0, sfAccount.getCode()})), err);
}

TEST_F(TxNestedFieldImpl, index_out_of_bounds)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::IndexOutOfBounds;

    expectError(h->getTxNestedField(locator({sfMemos.getCode(), 1, sfMemoData.getCode()})), err);
    expectError(h->getTxNestedField(locator({sfCredentialIDs.getCode(), 1})), err);
    expectError(h->getTxNestedField(locator({sfMemos.getCode(), -1, sfMemoData.getCode()})), err);
    expectError(h->getTxNestedField(locator({sfCredentialIDs.getCode(), -1})), err);
}

TEST_F(TxNestedFieldImpl, unknown_field_code_invalid_field)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::InvalidField;

    expectError(
        h->getTxNestedField(locator({fieldCode(20000, 20000), 0, sfAccount.getCode()})), err);
    expectError(h->getTxNestedField(locator({sfMemos.getCode(), 0, fieldCode(20000, 20000)})), err);
    // Far-negative code: not in the SField map at all.
    expectError(
        h->getTxNestedField(
            locator({std::numeric_limits<std::int32_t>::min(), 0, sfAccount.getCode()})),
        err);
}

TEST_F(TxNestedFieldImpl, container_without_index_not_leaf)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::NotLeafField;

    expectError(h->getTxNestedField(locator({sfMemos.getCode()})), err);
    expectError(h->getTxNestedField(locator({sfCredentialIDs.getCode()})), err);
}

TEST_F(TxNestedFieldImpl, nest_into_non_container_malformed)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getTxNestedField(locator({sfAccount.getCode(), 0, sfAccount.getCode()})),
        HostFunctionError::LocatorMalformed);
}

}  // namespace xrpl::test
