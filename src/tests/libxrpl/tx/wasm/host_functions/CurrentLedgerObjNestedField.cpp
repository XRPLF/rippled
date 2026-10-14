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
#include <utility>

namespace xrpl::test {

struct CurrentLedgerObjNestedFieldImpl : RealHostFixture
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

TEST_F(CurrentLedgerObjNestedFieldImpl, matches_nested_signer_quorum)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getCurrentLedgerObjNestedField(locator({sfSignerQuorum.getCode()})),
        RealHostFixture::toBytes(static_cast<std::uint32_t>(2)));
}

TEST_F(CurrentLedgerObjNestedFieldImpl, matches_nested_signer_weight)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectValue(
        h->getCurrentLedgerObjNestedField(
            locator({sfSignerEntries.getCode(), 0, sfSignerWeight.getCode()})),
        RealHostFixture::toBytes(static_cast<std::uint16_t>(1)));
}

TEST_F(CurrentLedgerObjNestedFieldImpl, matches_nested_signer_account)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);

    auto const sle = ledger.getOpenLedger().read(keylet::signerList(owner.id()));
    ASSERT_NE(sle, nullptr);
    auto const& entry0 = sle->getFieldArray(sfSignerEntries)[0];

    expectValue(
        h->getCurrentLedgerObjNestedField(
            locator({sfSignerEntries.getCode(), 0, sfAccount.getCode()})),
        RealHostFixture::toBytes(entry0.getAccountID(sfAccount)));
}

TEST_F(CurrentLedgerObjNestedFieldImpl, missing_field_not_found)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getCurrentLedgerObjNestedField(locator({sfSigners.getCode(), 0, sfAccount.getCode()})),
        HostFunctionError::FieldNotFound);
}

TEST_F(CurrentLedgerObjNestedFieldImpl, index_out_of_bounds)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::IndexOutOfBounds;

    expectError(
        h->getCurrentLedgerObjNestedField(
            locator({sfSignerEntries.getCode(), 2, sfAccount.getCode()})),
        err);
    expectError(
        h->getCurrentLedgerObjNestedField(
            locator({sfSignerEntries.getCode(), -1, sfAccount.getCode()})),
        err);
}

TEST_F(CurrentLedgerObjNestedFieldImpl, unknown_field_code_invalid_field)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    auto const err = HostFunctionError::InvalidField;

    expectError(h->getCurrentLedgerObjNestedField(locator({fieldCode(20000, 20000)})), err);
    expectError(
        h->getCurrentLedgerObjNestedField(
            locator({sfSignerEntries.getCode(), 0, fieldCode(20000, 20000)})),
        err);
}

TEST_F(CurrentLedgerObjNestedFieldImpl, nest_into_non_container_malformed)
{
    auto const owner = fund("owner");
    auto h = makeHost(owner);
    expectError(
        h->getCurrentLedgerObjNestedField(
            locator({sfSignerQuorum.getCode(), 0, sfAccount.getCode()})),
        HostFunctionError::LocatorMalformed);
}

TEST_F(CurrentLedgerObjNestedFieldImpl, missing_current_object_not_found)
{
    auto const owner = fund("owner");
    auto assembler = bareTx();
    auto h = makeHost(keylet::signerList(owner.id()), assembler.type, std::move(assembler.build));
    expectError(
        h->getCurrentLedgerObjNestedField(locator({sfSignerQuorum.getCode()})),
        HostFunctionError::LedgerObjNotFound);
}

}  // namespace xrpl::test
