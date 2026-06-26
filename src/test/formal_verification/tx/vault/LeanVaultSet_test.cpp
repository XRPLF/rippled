#include <test/formal_verification/ffi/protocol/RulesFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ffi/tx/TxFFI.h>
#include <test/formal_verification/ffi/tx/vault/VaultSetFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/apply.h>

#include <cstdint>
#include <optional>
#include <string>

namespace xrpl::test {

using namespace formal_verification;

class LeanVaultSet_test : public LedgerSuite
{
    // Amendment FFI codes (XRPL.Model.tx.Amendment.toCode).
    static constexpr std::uint8_t kAmendmentCount = 9;
    static constexpr std::uint8_t kSingleAssetVault = 0;
    static constexpr std::uint8_t kPermissionedDomains = 1;

    LeanObjectFFI
    rulesExcept(std::uint8_t exclude)
    {
        LeanObjectFFI r = rulesEmpty();
        for (std::uint8_t c = 0; c < kAmendmentCount; ++c)
            if (c != exclude)
                r = LeanObjectFFI(lean_rules_enable(r.give(), c));
        return r;
    }

    Keylet
    createVault(jtx::Env& env, jtx::Account const& owner, std::optional<std::uint32_t> flags = {})
    {
        jtx::Vault vault{env};
        auto const [jv, keylet] =
            vault.create({.owner = owner, .asset = xrpIssue(), .flags = flags});
        env(jv);
        env.close();
        return keylet;
    }

    void
    runVaultSet(
        jtx::Env& env,
        OpenView& ov,
        STTx const& tx,
        TER expected,
        char const* label,
        std::optional<std::uint8_t> disable = {})
    {
        runLedgerTest(ov, label, [&](LedgerFFI& ledger) {
            auto const res = xrpl::apply(
                env.app(), ov, tx, TapNone, beast::Journal{beast::Journal::getNullSink()});

            LeanObjectFFI txn = buildVaultSetTx(
                tx.getTransactionID(),
                tx.getAccountID(sfAccount),
                tx.getFieldAmount(sfFee).xrp().drops(),
                tx.getFieldU32(sfSequence),
                tx.getFlags(),
                tx.getFieldH256(sfVaultID),
                tx.isFieldPresent(sfData) ? std::optional<Blob>(tx.getFieldVL(sfData))
                                          : std::nullopt,
                tx[~sfAssetsMaximum],
                tx[~sfDomainID]);
            LeanObjectFFI rules = disable ? rulesExcept(*disable) : rulesAll();
            LeanTerResult const leanRes = processTx(ledger, txn, rules);

            BEAST_EXPECT(res.ter == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECTS(
                leanRes.value == cppTerByte(res.ter),
                std::string("cpp=") + transToken(res.ter) + " lean=" + terName(leanRes.value));
        });
    }

    // Build an unsigned VaultSet. Sufficient for cases that fail in preflight
    static STTx
    unsignedSet(jtx::Env& env, jtx::Account const& a, uint256 const& vaultID, auto fill)
    {
        return STTx(ttVAULT_SET, [&](STObject& obj) {
            obj.setAccountID(sfAccount, a.id());
            obj.setFieldAmount(sfFee, STAmount{XRPAmount{10}});
            obj.setFieldU32(sfSequence, env.seq(a));
            obj.setFieldH256(sfVaultID, vaultID);
            fill(obj);
        });
    }

    void
    testVaultSetZeroVaultID()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const tx = unsignedSet(env, owner, uint256{}, [](STObject&) {});
        OpenView ov{*env.current()};
        runVaultSet(env, ov, tx, temMALFORMED, "vaultSet.zero_vault_id");
    }

    void
    testVaultSetNothingToUpdate()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const tx = unsignedSet(env, owner, uint256{1}, [](STObject&) {});
        OpenView ov{*env.current()};
        runVaultSet(env, ov, tx, temMALFORMED, "vaultSet.nothing_to_update");
    }

    void
    testVaultSetEmptyData()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const tx =
            unsignedSet(env, owner, uint256{1}, [](STObject& o) { o.setFieldVL(sfData, Blob{}); });
        OpenView ov{*env.current()};
        runVaultSet(env, ov, tx, temMALFORMED, "vaultSet.empty_data");
    }

    void
    testVaultSetDataTooLong()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const tx = unsignedSet(
            env, owner, uint256{1}, [](STObject& o) { o.setFieldVL(sfData, Blob(257, 0xAB)); });
        OpenView ov{*env.current()};
        runVaultSet(env, ov, tx, temMALFORMED, "vaultSet.data_too_long");
    }

    void
    testVaultSetNegativeAssetsMaximum()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const tx = unsignedSet(
            env, owner, uint256{1}, [](STObject& o) { o.at(sfAssetsMaximum) = Number(-1); });
        OpenView ov{*env.current()};
        runVaultSet(env, ov, tx, temMALFORMED, "vaultSet.negative_assets_maximum");
    }

    void
    testVaultSetInvalidFlag()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const tx = unsignedSet(env, owner, uint256{1}, [](STObject& o) {
            o.setFieldVL(sfData, Blob{0x01});
            // 0x00040000 is not a valid VaultSet flag
            o.setFieldU32(sfFlags, 0x00040000u);
        });
        OpenView ov{*env.current()};
        runVaultSet(env, ov, tx, temINVALID_FLAG, "vaultSet.invalid_flag");
    }

    void
    testVaultSetFeeTooHigh()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        // Fee above kInitialXrp fails isLegalAmount
        auto const tx = unsignedSet(env, owner, uint256{1}, [](STObject& o) {
            o.setFieldVL(sfData, Blob{0x01});
            o.setFieldAmount(sfFee, STAmount{std::uint64_t(STAmount::kMaxNativeN) + 1, false});
        });
        OpenView ov{*env.current()};
        runVaultSet(env, ov, tx, temBAD_FEE, "vaultSet.fee_too_high");
    }

    void
    testVaultSetFeatureDisabled()
    {
        using namespace jtx;
        Env env(*this, testableAmendments() - featureSingleAssetVault);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const tx = unsignedSet(
            env, owner, uint256{1}, [](STObject& o) { o.setFieldVL(sfData, Blob{0x01}); });
        OpenView ov{*env.current()};
        runVaultSet(env, ov, tx, temDISABLED, "vaultSet.feature_disabled", kSingleAssetVault);
    }

    void
    testVaultSetDomainNeedsPermissionedDomains()
    {
        using namespace jtx;
        Env env(*this, testableAmendments() - featurePermissionedDomains);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const tx = unsignedSet(
            env, owner, uint256{1}, [](STObject& o) { o.setFieldH256(sfDomainID, uint256{7}); });
        OpenView ov{*env.current()};
        runVaultSet(env, ov, tx, temDISABLED, "vaultSet.domain_needs_pd", kPermissionedDomains);
    }

    void
    testVaultSetNoEntry()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto jv = jtx::Vault::set({.owner = owner, .id = keylet::vault(owner.id(), 99).key});
        jv[sfData] = strHex(Blob{0xCA, 0xFE});
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        OpenView ov{*env.current()};
        runVaultSet(env, ov, *jt.stx, tecNO_ENTRY, "vaultSet.no_entry");
    }

    void
    testVaultSetNotOwner()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        Account const other("other");
        env.fund(XRP(10000), owner, other);
        env.close();
        auto const kl = createVault(env, owner);

        auto jv = jtx::Vault::set({.owner = other, .id = kl.key});
        jv[sfData] = strHex(Blob{0xCA, 0xFE});
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        OpenView ov{*env.current()};
        runVaultSet(env, ov, *jt.stx, tecNO_PERMISSION, "vaultSet.not_owner");
    }

    void
    testVaultSetDomainOnPublicVault()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);  // public

        auto jv = jtx::Vault::set({.owner = owner, .id = kl.key});
        jv[sfDomainID] = to_string(uint256{7});
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        OpenView ov{*env.current()};
        runVaultSet(env, ov, *jt.stx, tecNO_PERMISSION, "vaultSet.domain_on_public_vault");
    }

    void
    testVaultSetDomainNotFound()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner, tfVaultPrivate);

        auto jv = jtx::Vault::set({.owner = owner, .id = kl.key});
        jv[sfDomainID] = to_string(uint256{7});
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        OpenView ov{*env.current()};
        runVaultSet(env, ov, *jt.stx, tecOBJECT_NOT_FOUND, "vaultSet.domain_not_found");
    }

    void
    testVaultSetLimitExceeded()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);
        env(Vault{env}.deposit({.depositor = owner, .id = kl.key, .amount = XRP(100)}));
        env.close();

        // assetsMaximum below assetsTotal (100 XRP).
        auto jv = jtx::Vault::set({.owner = owner, .id = kl.key});
        jv[sfAssetsMaximum] = 50;
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        OpenView ov{*env.current()};
        runVaultSet(env, ov, *jt.stx, tecLIMIT_EXCEEDED, "vaultSet.limit_exceeded");
    }

    void
    testVaultSetData()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);

        Blob const data{0xDE, 0xAD, 0xBE, 0xEF};
        auto jv = jtx::Vault::set({.owner = owner, .id = kl.key});
        jv[sfData] = strHex(data);
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        OpenView ov{*env.current()};
        runVaultSet(env, ov, *jt.stx, tesSUCCESS, "vaultSet.data");

        auto const sle = ov.read(kl);
        BEAST_EXPECT(sle && sle->getFieldVL(sfData) == data);
    }

    void
    testVaultSetDataMaxLength()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);

        Blob const data(256, 0xAB);  // kMaxDataPayloadLength
        auto jv = jtx::Vault::set({.owner = owner, .id = kl.key});
        jv[sfData] = strHex(data);
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        OpenView ov{*env.current()};
        runVaultSet(env, ov, *jt.stx, tesSUCCESS, "vaultSet.data_max_length");

        auto const sle = ov.read(kl);
        BEAST_EXPECT(sle && sle->getFieldVL(sfData).size() == 256);
    }

    void
    testVaultSetAssetsMaximum()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);

        auto jv = jtx::Vault::set({.owner = owner, .id = kl.key});
        jv[sfAssetsMaximum] = 1000;
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        OpenView ov{*env.current()};
        runVaultSet(env, ov, *jt.stx, tesSUCCESS, "vaultSet.assets_maximum");

        auto const sle = ov.read(kl);
        BEAST_EXPECT(sle && (*sle)[sfAssetsMaximum] == Number(1000));
    }

    void
    testVaultSetAssetsMaximumExtreme()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);

        auto jv = jtx::Vault::set({.owner = owner, .id = kl.key});
        jv[sfAssetsMaximum] = "1000000000000000";  // 1e15, well above empty assetsTotal
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        OpenView ov{*env.current()};
        runVaultSet(env, ov, *jt.stx, tesSUCCESS, "vaultSet.assets_maximum_extreme");
    }

    void
    testVaultSetDomainID()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        Account const pdOwner("pdOwner");
        Account const credIssuer("credIssuer");
        env.fund(XRP(10000), owner, pdOwner, credIssuer);
        env.close();
        auto const kl = createVault(env, owner, tfVaultPrivate);

        pdomain::Credentials const creds{{.issuer = credIssuer, .credType = "credential"}};
        env(pdomain::setTx(pdOwner, creds));
        uint256 const domainID = pdomain::getNewDomain(env.meta());
        env.close();

        auto jv = jtx::Vault::set({.owner = owner, .id = kl.key});
        jv[sfDomainID] = to_string(domainID);
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        OpenView ov{*env.current()};
        runVaultSet(env, ov, *jt.stx, tesSUCCESS, "vaultSet.domain_id");

        auto const vault = ov.read(kl);
        if (!BEAST_EXPECT(vault))
            return;
        auto const issuance = ov.read(keylet::mptIssuance(vault->getFieldH192(sfShareMPTID)));
        BEAST_EXPECT(
            issuance && issuance->isFieldPresent(sfDomainID) &&
            issuance->getFieldH256(sfDomainID) == domainID);
    }

    // Stage one bad field on the vault, then a Data-only VaultSet that leaves it
    // in the after-state, so ValidVault rejects it.
    void
    runVaultInvariant(
        jtx::Env& env,
        jtx::Account const& owner,
        Keylet const& kl,
        char const* label,
        auto stage)
    {
        OpenView ov{*env.current()};
        {
            auto sle = std::make_shared<SLE>(*ov.read(kl));
            stage(*sle);
            ov.rawReplace(sle);
        }
        auto jv = jtx::Vault::set({.owner = owner, .id = kl.key});
        jv[sfData] = strHex(Blob{0x01});
        auto const jt = env.jt(jv);
        if (!BEAST_EXPECT(jt.stx))
            return;
        runVaultSet(env, ov, *jt.stx, tecINVARIANT_FAILED, label);
    }

    // assets available must not exceed assets outstanding
    void
    testVaultSetInvariantAssetsAvailable()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);
        runVaultInvariant(env, owner, kl, "vaultSet.invariant_assets_available", [](SLE& sle) {
            sle.at(sfAssetsAvailable) = Number(100);
        });
    }

    // a zero-share vault must hold no assets
    void
    testVaultSetInvariantZeroSizedAssets()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);
        runVaultInvariant(env, owner, kl, "vaultSet.invariant_zero_sized_assets", [](SLE& sle) {
            sle.at(sfAssetsTotal) = Number(100);
        });
    }

    // loss unrealized must not exceed assets outstanding minus available
    void
    testVaultSetInvariantLossUnrealized()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);
        runVaultInvariant(env, owner, kl, "vaultSet.invariant_loss_unrealized", [](SLE& sle) {
            sle.at(sfLossUnrealized) = Number(100);
        });
    }

    // assets outstanding must not exceed assets maximum
    void
    testVaultSetInvariantExceedMaximum()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);
        env(Vault{env}.deposit({.depositor = owner, .id = kl.key, .amount = XRP(100)}));
        env.close();
        runVaultInvariant(env, owner, kl, "vaultSet.invariant_exceed_maximum", [](SLE& sle) {
            sle.at(sfAssetsMaximum) = Number(50);
        });
    }

    // assets outstanding must be positive
    void
    testVaultSetInvariantNegativeAssetsTotal()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);
        runVaultInvariant(env, owner, kl, "vaultSet.invariant_negative_total", [](SLE& sle) {
            sle.at(sfAssetsTotal) = Number(-1);
        });
    }

    // assets maximum must be positive
    void
    testVaultSetInvariantNegativeAssetsMaximum()
    {
        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(10000), owner);
        env.close();
        auto const kl = createVault(env, owner);
        runVaultInvariant(env, owner, kl, "vaultSet.invariant_negative_maximum", [](SLE& sle) {
            sle.at(sfAssetsMaximum) = Number(-1);
        });
    }

    void
    runTests() override
    {
        // preflight
        testVaultSetZeroVaultID();
        testVaultSetNothingToUpdate();
        testVaultSetEmptyData();
        testVaultSetDataTooLong();
        testVaultSetNegativeAssetsMaximum();
        testVaultSetInvalidFlag();
        testVaultSetFeeTooHigh();
        testVaultSetFeatureDisabled();
        testVaultSetDomainNeedsPermissionedDomains();

        // preclaim
        testVaultSetNoEntry();
        testVaultSetNotOwner();
        testVaultSetDomainOnPublicVault();
        testVaultSetDomainNotFound();

        // doApply
        testVaultSetLimitExceeded();

        // success
        testVaultSetData();
        testVaultSetDataMaxLength();
        testVaultSetAssetsMaximum();
        testVaultSetAssetsMaximumExtreme();
        testVaultSetDomainID();

        // invariants (failing until the model checks them)
        // testVaultSetInvariantAssetsAvailable();
        // testVaultSetInvariantZeroSizedAssets();
        // testVaultSetInvariantLossUnrealized();
        // testVaultSetInvariantExceedMaximum();
        // testVaultSetInvariantNegativeAssetsTotal();
        // testVaultSetInvariantNegativeAssetsMaximum();
    }
};

BEAST_DEFINE_TESTSUITE(LeanVaultSet, formal_verification, xrpl);

}  // namespace xrpl::test
