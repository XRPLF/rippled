#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/fee.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl {

class VaultTranches_test : public VaultTestBase
{
    struct TrancheArgs
    {
        std::uint32_t interestRate = 0;
        std::uint32_t subordinationMinimum = 0;
    };

    static void
    addTranches(json::Value& tx, std::vector<TrancheArgs> const& tranches)
    {
        auto& array = tx[sfTranches.jsonName];
        array = json::ValueType::Array;
        for (auto const& t : tranches)
        {
            json::Value entry;
            entry[sfTranche.jsonName][sfInterestRate.jsonName] = t.interestRate;
            if (t.subordinationMinimum != 0)
            {
                entry[sfTranche.jsonName][sfSubordinationMinimum.jsonName] = t.subordinationMinimum;
            }
            array.append(entry);
        }
    }

    // Creates an XRP vault owned by `owner` with the given debt classes and returns its keylet.
    static Keylet
    createVault(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        std::vector<TrancheArgs> const& tranches,
        std::uint32_t flags = 0)
    {
        test::jtx::Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue(), .flags = flags});
        addTranches(tx, tranches);
        env(tx);
        env.close();
        return keylet;
    }

    static MPTID
    trancheShare(test::jtx::Env& env, Keylet const& keylet, std::size_t index)
    {
        return env.le(keylet)->getFieldArray(sfTranches)[index][sfShareMPTID];
    }

    static MPTID
    equityShare(test::jtx::Env& env, Keylet const& keylet)
    {
        return env.le(keylet)->at(sfShareMPTID);
    }

    static json::Value
    depositTx(
        test::jtx::Account const& depositor,
        Keylet const& keylet,
        STAmount const& amount,
        std::optional<MPTID> const& share = std::nullopt)
    {
        auto tx =
            test::jtx::Vault::deposit({.depositor = depositor, .id = keylet.key, .amount = amount});
        if (share)
            tx[sfShareMPTID.jsonName] = to_string(*share);
        return tx;
    }

    static json::Value
    withdrawTx(
        test::jtx::Account const& depositor,
        Keylet const& keylet,
        STAmount const& amount,
        std::optional<MPTID> const& share = std::nullopt)
    {
        auto tx = test::jtx::Vault::withdraw(
            {.depositor = depositor, .id = keylet.key, .amount = amount});
        if (share)
            tx[sfShareMPTID.jsonName] = to_string(*share);
        return tx;
    }

    static std::uint64_t
    sharesHeld(test::jtx::Env& env, test::jtx::Account const& holder, MPTID const& share)
    {
        auto const mptoken = env.le(keylet::mptoken(share, holder.id()));
        return mptoken ? mptoken->at(sfMPTAmount) : 0;
    }

    static std::uint64_t
    sharesOutstanding(test::jtx::Env& env, MPTID const& share)
    {
        return env.le(keylet::mptokenIssuance(share))->at(sfOutstandingAmount);
    }

    static std::vector<Number>
    classValues(test::jtx::Env& env, Keylet const& keylet)
    {
        auto const vault = env.le(keylet);
        return vaultClassValues(
            vault,
            env.current()->parentCloseTime(),
            vault->at(sfAssetsTotal) - vault->at(sfLossUnrealized));
    }

    // Creates a LoanBroker on the vault and a zero-interest single-payment loan of `principal`
    // to `borrower`, due 60 seconds after origination. Returns the broker and loan keylets.
    static std::pair<Keylet, Keylet>
    createLoan(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        test::jtx::Account const& borrower,
        Keylet const& vaultKeylet,
        STAmount const& principal)
    {
        using namespace test::jtx;
        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(loan_broker::set(owner, vaultKeylet.key));
        env.close();
        env(loan::set(borrower, brokerKeylet.key, principal),
            loan::kInterestRate(TenthBips32(0)),
            loan::kGracePeriod(60),
            loan::kPaymentInterval(60),
            loan::kPaymentTotal(1),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2));
        env.close();
        return {brokerKeylet, keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1))};
    }

    // Open-ended vaults take a LoanBroker only before LendingProtocolV1_1.
    FeatureBitset const openLending_{all_ - featureLendingProtocolV1_1};

    void
    testCreateDisabled()
    {
        testcase("create disabled");
        using namespace test::jtx;

        Env env(*this, all_ - featureStructuredCredit);
        Account const owner{"owner"};
        env.fund(XRP(10'000), owner);
        env.close();

        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        addTranches(tx, {{.interestRate = 5'000}});
        env(tx, Ter(temDISABLED));
    }

    // Submits a VaultCreate with one sfTranches entry carrying the raw `entry` fields and
    // expects `ter`.
    void
    expectCreateMalformed(json::Value const& tranches)
    {
        using namespace test::jtx;
        Env env(*this, all_);
        Account const owner{"owner"};
        env.fund(XRP(10'000), owner);
        env.close();

        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        tx[sfTranches.jsonName] = tranches;
        env(tx, Ter(temMALFORMED));
    }

    static json::Value
    trancheEntry(std::uint32_t interestRate)
    {
        json::Value entry;
        entry[sfTranche.jsonName][sfInterestRate.jsonName] = interestRate;
        return entry;
    }

    void
    testCreate()
    {
        testcase("create");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        env.fund(XRP(100'000), owner);
        env.close();

        Vault const vault{env};
        auto [plainTx, plainKeylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        env(plainTx);
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        addTranches(
            tx,
            {{.interestRate = 5'000, .subordinationMinimum = 30'000},
             {.interestRate = 8'000, .subordinationMinimum = 10'000}});
        auto const parentClose = env.current()->parentCloseTime();
        env(tx);
        env.close();

        auto const sle = env.le(keylet);
        if (!BEAST_EXPECT(sle && sle->isFieldPresent(sfTranches)))
            return;
        auto const& tranches = sle->getFieldArray(sfTranches);
        if (!BEAST_EXPECT(tranches.size() == 2))
            return;
        BEAST_EXPECT(tranches[0][sfInterestRate] == 5'000);
        BEAST_EXPECT(tranches[0][sfSubordinationMinimum] == 30'000);
        BEAST_EXPECT(tranches[1][sfInterestRate] == 8'000);
        BEAST_EXPECT(tranches[1][sfSubordinationMinimum] == 10'000);
        BEAST_EXPECT(sle->at(~sfTrancheAccrualTime) == parentClose.time_since_epoch().count());

        auto const equity = env.le(keylet::mptokenIssuance(sle->at(sfShareMPTID)));
        if (!BEAST_EXPECT(equity))
            return;
        for (auto const& tranche : tranches)
        {
            auto const issuance = env.le(keylet::mptokenIssuance(tranche[sfShareMPTID]));
            if (!BEAST_EXPECT(issuance))
                return;
            BEAST_EXPECT(issuance->at(sfIssuer) == sle->at(sfAccount));
            BEAST_EXPECT(issuance->getFlags() == equity->getFlags());
            BEAST_EXPECT(issuance->at(sfOutstandingAmount) == 0);
        }

        auto const plain = env.le(plainKeylet);
        auto const plainPseudo = env.le(keylet::account(plain->at(sfAccount)));
        auto const pseudo = env.le(keylet::account(sle->at(sfAccount)));
        BEAST_EXPECT(pseudo->at(sfOwnerCount) == plainPseudo->at(sfOwnerCount) + 2);
    }

    void
    testCreatePrivate()
    {
        testcase("create private");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const pdOwner{"pdOwner"};
        Account const credIssuer{"credIssuer"};
        env.fund(XRP(100'000), owner, pdOwner, credIssuer);
        env.close();

        pdomain::Credentials const credentials{{.issuer = credIssuer, .credType = "member"}};
        env(pdomain::setTx(pdOwner, credentials));
        auto const domainId = pdomain::getNewDomain(env.meta());
        env.close();

        Vault const vault{env};
        auto [tx, keylet] =
            vault.create({.owner = owner, .asset = xrpIssue(), .flags = tfVaultPrivate});
        tx[sfDomainID] = to_string(domainId);
        addTranches(tx, {{.interestRate = 5'000}});
        env(tx);
        env.close();

        auto const sle = env.le(keylet);
        if (!BEAST_EXPECT(sle && sle->isFieldPresent(sfTranches)))
            return;
        auto const issuance =
            env.le(keylet::mptokenIssuance(sle->getFieldArray(sfTranches)[0][sfShareMPTID]));
        if (!BEAST_EXPECT(issuance))
            return;
        BEAST_EXPECT(issuance->isFlag(lsfMPTRequireAuth));
        BEAST_EXPECT(issuance->at(~sfDomainID) == domainId);
    }

    void
    testCreateNonTransferable()
    {
        testcase("create non-transferable");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        env.fund(XRP(100'000), owner);
        env.close();

        Vault const vault{env};
        auto [tx, keylet] = vault.create(
            {.owner = owner, .asset = xrpIssue(), .flags = tfVaultShareNonTransferable});
        addTranches(tx, {{.interestRate = 5'000}});
        env(tx);
        env.close();

        auto const sle = env.le(keylet);
        if (!BEAST_EXPECT(sle && sle->isFieldPresent(sfTranches)))
            return;
        auto const issuance =
            env.le(keylet::mptokenIssuance(sle->getFieldArray(sfTranches)[0][sfShareMPTID]));
        if (!BEAST_EXPECT(issuance))
            return;
        BEAST_EXPECT(!issuance->isFlag(lsfMPTCanTransfer));
    }

    void
    testCreateMalformedEmpty()
    {
        testcase("create malformed: empty");
        expectCreateMalformed(json::Value{json::ValueType::Array});
    }

    void
    testCreateMalformedTooMany()
    {
        testcase("create malformed: too many");
        json::Value tranches{json::ValueType::Array};
        for (std::size_t i = 0; i <= kMaxVaultTranches; ++i)
            tranches.append(trancheEntry(1'000));
        expectCreateMalformed(tranches);
    }

    void
    testCreateMalformedShareID()
    {
        testcase("create malformed: share id");
        json::Value tranches{json::ValueType::Array};
        auto entry = trancheEntry(1'000);
        entry[sfTranche.jsonName][sfShareMPTID.jsonName] = to_string(makeMptID(1, AccountID(1)));
        tranches.append(entry);
        expectCreateMalformed(tranches);
    }

    void
    testCreateMalformedPrincipal()
    {
        testcase("create malformed: principal");
        json::Value tranches{json::ValueType::Array};
        auto entry = trancheEntry(1'000);
        entry[sfTranche.jsonName][sfPrincipalOutstanding.jsonName] = "100";
        tranches.append(entry);
        expectCreateMalformed(tranches);
    }

    void
    testCreateMalformedRate()
    {
        testcase("create malformed: rate");
        json::Value tranches{json::ValueType::Array};
        tranches.append(trancheEntry(lending::kMaxInterestRate.value() + 1));
        expectCreateMalformed(tranches);
    }

    void
    testCreateMalformedSubordination()
    {
        testcase("create malformed: subordination");
        json::Value tranches{json::ValueType::Array};
        auto entry = trancheEntry(1'000);
        entry[sfTranche.jsonName][sfSubordinationMinimum.jsonName] = kTenthBipsPerUnity.value();
        tranches.append(entry);
        expectCreateMalformed(tranches);
    }

    void
    testDepositEquityThenSenior()
    {
        testcase("deposit equity then senior");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        env.fund(XRP(100'000), owner, lender);
        env.close();

        auto const keylet = createVault(env, owner, {{.interestRate = 5'000}});
        auto const senior = trancheShare(env, keylet, 0);
        env(depositTx(owner, keylet, XRP(1'000)));
        env(depositTx(lender, keylet, XRP(1'000), senior));
        env.close();

        BEAST_EXPECT(sharesHeld(env, lender, senior) == XRP(1'000).value().mantissa());
        BEAST_EXPECT(
            sharesHeld(env, owner, equityShare(env, keylet)) == XRP(1'000).value().mantissa());
        auto const sle = env.le(keylet);
        BEAST_EXPECT(
            sle->getFieldArray(sfTranches)[0][sfPrincipalOutstanding] ==
            Number(XRP(1'000).value()));
        BEAST_EXPECT(sle->at(sfAssetsTotal) == Number(XRP(2'000).value()));
    }

    void
    testDepositAtValue()
    {
        testcase("deposit at value");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        env.fund(XRP(100'000), owner, lender);
        env.close();

        auto const keylet = createVault(env, owner, {{.interestRate = 5'000}});
        auto const senior = trancheShare(env, keylet, 0);
        env(depositTx(owner, keylet, XRP(1'000)));
        env(depositTx(lender, keylet, XRP(1'000), senior));
        env.close(env.now() + std::chrono::seconds{kSecondsInYear});

        auto const before = sharesHeld(env, lender, senior);
        env(depositTx(lender, keylet, XRP(1'050), senior));
        env.close();
        auto const minted = sharesHeld(env, lender, senior) - before;
        // A year at 5% prices the class near 1.05, so 1,050 XRP buys about 1,000 XRP of shares.
        BEAST_EXPECT(minted <= XRP(1'000).value().mantissa());
        BEAST_EXPECT(minted >= XRP(999).value().mantissa());
    }

    void
    testCouponTransfersValue()
    {
        testcase("coupon moves value from equity to the senior class");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        env.fund(XRP(100'000), owner, lender);
        env.close();

        auto const keylet = createVault(env, owner, {{.interestRate = 5'000}});
        env(depositTx(owner, keylet, XRP(1'000)));
        env(depositTx(lender, keylet, XRP(1'000), trancheShare(env, keylet, 0)));
        env.close();
        auto const start = classValues(env, keylet);
        env.close(env.now() + std::chrono::seconds{kSecondsInYear});
        auto const end = classValues(env, keylet);

        BEAST_EXPECT(end[0] > start[0]);
        BEAST_EXPECT(end[1] < start[1]);
        BEAST_EXPECT(end[0] + end[1] == start[0] + start[1]);
        BEAST_EXPECT(end[0] >= Number(XRP(1'049).value()));
        // The ledger clock runs a few seconds past the year, worth a few drops of coupon.
        BEAST_EXPECT(end[0] <= Number(XRP(1'050).value()) + Number(10'000));
    }

    // Three classes (senior 800, mezzanine 100, equity 100) in an open-ended vault whose loan
    // of 900 XRP defaulted, leaving 100 XRP: senior worth 100, mezzanine and equity worth 0.
    struct DefaultedStack
    {
        Keylet vault;
        MPTID senior;
        MPTID mezzanine;
    };

    DefaultedStack
    makeDefaultedStack(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        test::jtx::Account const& lender,
        test::jtx::Account const& borrower)
    {
        using namespace test::jtx;
        auto const keylet = createVault(env, owner, {{}, {}});
        auto const senior = trancheShare(env, keylet, 0);
        auto const mezzanine = trancheShare(env, keylet, 1);
        env(depositTx(owner, keylet, XRP(100)));
        env(depositTx(lender, keylet, XRP(100), mezzanine));
        env(depositTx(lender, keylet, XRP(800), senior));
        env.close();
        auto const [broker, loanKeylet] = createLoan(env, owner, borrower, keylet, XRP(900));
        env.close(std::chrono::seconds{60 + 60 + 1});
        env(loan::manage(owner, loanKeylet.key, tfLoanDefault));
        env.close();
        return {keylet, senior, mezzanine};
    }

    void
    testDepositImpairedDebt()
    {
        testcase("deposit into an impaired debt class");
        using namespace test::jtx;

        Env env(*this, openLending_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(100'000), owner, lender, borrower);
        env.close();

        auto const stack = makeDefaultedStack(env, owner, lender, borrower);
        auto const values = classValues(env, stack.vault);
        BEAST_EXPECT(values == (std::vector<Number>{Number(XRP(100).value()), 0, 0}));
        env(depositTx(lender, stack.vault, XRP(10), stack.mezzanine), Ter(tecTRANCHE_IMPAIRED));
        env(depositTx(lender, stack.vault, XRP(10), stack.senior), Ter(tecTRANCHE_IMPAIRED));
    }

    void
    testDepositImpairedEquity()
    {
        testcase("deposit into an impaired equity class");
        using namespace test::jtx;

        Env env(*this, openLending_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(100'000), owner, lender, borrower);
        env.close();

        auto const stack = makeDefaultedStack(env, owner, lender, borrower);
        env(depositTx(owner, stack.vault, XRP(10)), Ter(tecTRANCHE_IMPAIRED));
    }

    void
    testDepositDebtWithoutEquity()
    {
        testcase("deposit into a debt class with no equity");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        env.fund(XRP(100'000), owner, lender);
        env.close();

        auto const keylet = createVault(env, owner, {{.interestRate = 5'000}});
        env(depositTx(lender, keylet, XRP(100), trancheShare(env, keylet, 0)),
            Ter(tecTRANCHE_COVERAGE));
    }

    void
    testDepositCoverage()
    {
        testcase("deposit coverage");
        using namespace test::jtx;

        Env env(*this, openLending_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(100'000), owner, lender, borrower);
        env.close();

        auto const keylet = createVault(env, owner, {{.subordinationMinimum = 20'000}});
        auto const senior = trancheShare(env, keylet, 0);
        env(depositTx(owner, keylet, XRP(200)));
        env(depositTx(lender, keylet, XRP(600), senior));
        env.close();
        auto const [broker, loanKeylet] = createLoan(env, owner, borrower, keylet, XRP(500));

        // V 1,200 and senior claim 1,000 leave less than 20% beneath the senior class.
        env(depositTx(lender, keylet, XRP(400), senior), Ter(tecTRANCHE_COVERAGE));
        env.close();

        env(loan::pay(borrower, loanKeylet.key, XRP(500)));
        env.close();
        env(depositTx(lender, keylet, XRP(400), senior));
    }

    void
    testDepositUnknownClass()
    {
        testcase("deposit into an unknown class");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        env.fund(XRP(100'000), owner);
        env.close();

        auto const keylet = createVault(env, owner, {{.interestRate = 5'000}});
        auto const other = createVault(env, owner, {{.interestRate = 5'000}});
        env(depositTx(owner, keylet, XRP(100), equityShare(env, other)), Ter(tecWRONG_ASSET));
    }

    void
    testDepositPrivateClass()
    {
        testcase("deposit into a class of a private vault");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const outsider{"outsider"};
        Account const pdOwner{"pdOwner"};
        Account const credIssuer{"credIssuer"};
        env.fund(XRP(100'000), owner, outsider, pdOwner, credIssuer);
        env.close();

        pdomain::Credentials const credentials{{.issuer = credIssuer, .credType = "member"}};
        env(pdomain::setTx(pdOwner, credentials));
        auto const domainId = pdomain::getNewDomain(env.meta());
        env.close();

        Vault const vault{env};
        auto [tx, keylet] =
            vault.create({.owner = owner, .asset = xrpIssue(), .flags = tfVaultPrivate});
        tx[sfDomainID] = to_string(domainId);
        addTranches(tx, {{.interestRate = 5'000}});
        env(tx);
        env.close();
        env(depositTx(owner, keylet, XRP(100)));
        env.close();

        env(depositTx(outsider, keylet, XRP(10)), Ter(tecNO_AUTH));
        env(depositTx(outsider, keylet, XRP(10), trancheShare(env, keylet, 0)), Ter(tecNO_AUTH));
    }

    static STAmount
    shares(MPTID const& share, std::uint64_t count)
    {
        return STAmount{MPTIssue{share}, count};
    }

    static Number
    trancheField(
        test::jtx::Env& env,
        Keylet const& keylet,
        std::size_t index,
        SF_NUMBER const& field)
    {
        return env.le(keylet)->getFieldArray(sfTranches)[index][field];
    }

    void
    testWithdrawSenior()
    {
        testcase("withdraw senior");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        env.fund(XRP(100'000), owner, lender);
        env.close();

        auto const keylet = createVault(env, owner, {{.interestRate = 5'000}});
        auto const senior = trancheShare(env, keylet, 0);
        env(depositTx(owner, keylet, XRP(1'000)));
        env(depositTx(lender, keylet, XRP(1'000), senior));
        env.close(env.now() + std::chrono::seconds{kSecondsInYear});

        auto const half = sharesHeld(env, lender, senior) / 2;
        env(withdrawTx(lender, keylet, shares(senior, half)));
        env.close();

        BEAST_EXPECT(sharesHeld(env, lender, senior) == half);
        BEAST_EXPECT(
            trancheField(env, keylet, 0, sfPrincipalOutstanding) == Number(XRP(500).value()));
        auto const accrued = trancheField(env, keylet, 0, sfInterestAccrued);
        BEAST_EXPECT(accrued >= Number(XRP(25).value()));
        BEAST_EXPECT(accrued <= Number(XRP(25).value()) + Number(10'000));
    }

    void
    testWithdrawLastShares()
    {
        testcase("withdraw the last shares of a debt class");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        env.fund(XRP(100'000), owner, lender);
        env.close();

        auto const keylet = createVault(env, owner, {{.interestRate = 5'000}});
        auto const senior = trancheShare(env, keylet, 0);
        env(depositTx(owner, keylet, XRP(1'000)));
        env(depositTx(lender, keylet, XRP(1'000), senior));
        env.close(env.now() + std::chrono::seconds{kSecondsInYear});

        env(withdrawTx(lender, keylet, shares(senior, sharesHeld(env, lender, senior))));
        env.close();

        BEAST_EXPECT(sharesOutstanding(env, senior) == 0);
        BEAST_EXPECT(trancheField(env, keylet, 0, sfPrincipalOutstanding) == 0);
        BEAST_EXPECT(trancheField(env, keylet, 0, sfInterestAccrued) == 0);
    }

    void
    testWithdrawBySharesAmount()
    {
        testcase("withdraw names the class by its shares");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        env.fund(XRP(100'000), owner, lender);
        env.close();

        auto const keylet = createVault(env, owner, {{.interestRate = 5'000}});
        auto const senior = trancheShare(env, keylet, 0);
        env(depositTx(owner, keylet, XRP(1'000)));
        env(depositTx(lender, keylet, XRP(1'000), senior));
        env.close();

        auto const before = sharesOutstanding(env, senior);
        env(withdrawTx(lender, keylet, shares(senior, 1'000)));
        env.close();
        BEAST_EXPECT(sharesOutstanding(env, senior) == before - 1'000);
    }

    void
    testWithdrawClassMismatch()
    {
        testcase("withdraw names two classes");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        env.fund(XRP(100'000), owner, lender);
        env.close();

        auto const keylet = createVault(env, owner, {{.interestRate = 5'000}});
        auto const senior = trancheShare(env, keylet, 0);
        env(depositTx(owner, keylet, XRP(1'000)));
        env(depositTx(lender, keylet, XRP(1'000), senior));
        env.close();

        env(withdrawTx(lender, keylet, shares(senior, 1'000), equityShare(env, keylet)),
            Ter(tecWRONG_ASSET));
    }

    void
    testCoverageBlocksEquity()
    {
        testcase("coverage holds equity in while a loan is out");
        using namespace test::jtx;

        Env env(*this, openLending_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(100'000), owner, lender, borrower);
        env.close();

        auto const keylet = createVault(env, owner, {{.subordinationMinimum = 20'000}});
        env(depositTx(owner, keylet, XRP(200)));
        env(depositTx(lender, keylet, XRP(600), trancheShare(env, keylet, 0)));
        env.close();
        auto const [broker, loanKeylet] = createLoan(env, owner, borrower, keylet, XRP(500));

        env(withdrawTx(owner, keylet, XRP(100)), Ter(tecTRANCHE_COVERAGE));
        env.close();

        env(loan::pay(borrower, loanKeylet.key, XRP(500)));
        env.close();
        env(withdrawTx(owner, keylet, XRP(100)));
    }

    void
    testSeniorWithdrawWhileBreached()
    {
        testcase("senior withdrawal while coverage is breached");
        using namespace test::jtx;

        Env env(*this, openLending_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(100'000), owner, lender, borrower);
        env.close();

        auto const keylet = createVault(env, owner, {{.subordinationMinimum = 20'000}});
        auto const senior = trancheShare(env, keylet, 0);
        env(depositTx(owner, keylet, XRP(200)));
        env(depositTx(lender, keylet, XRP(600), senior));
        env.close();
        auto const [broker, loanKeylet] = createLoan(env, owner, borrower, keylet, XRP(500));
        // A loan can be impaired only once it is late.
        env.close(std::chrono::seconds{61});
        env(loan::manage(owner, loanKeylet.key, tfLoanImpair));
        env.close();
        BEAST_EXPECT(trancheMargins(env.le(keylet), env.current()->parentCloseTime())[0] < 0);

        env(withdrawTx(lender, keylet, XRP(100), senior));
    }

    void
    testEquityLast()
    {
        testcase("equity is the last class to leave");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        env.fund(XRP(100'000), owner, lender);
        env.close();

        auto const keylet = createVault(env, owner, {{}});
        auto const equity = equityShare(env, keylet);
        env(depositTx(owner, keylet, XRP(100)));
        env(depositTx(lender, keylet, XRP(100), trancheShare(env, keylet, 0)));
        env.close();

        env(withdrawTx(owner, keylet, shares(equity, sharesHeld(env, owner, equity))),
            Ter(tecTRANCHE_COVERAGE));
    }

    // Equity 100, mezzanine 100 and senior 800 in an open-ended vault with one loan of 150.
    DefaultedStack
    makeLoanStack(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        test::jtx::Account const& lender,
        test::jtx::Account const& borrower,
        std::uint32_t loanFlags)
    {
        using namespace test::jtx;
        auto const keylet = createVault(env, owner, {{}, {}});
        auto const senior = trancheShare(env, keylet, 0);
        auto const mezzanine = trancheShare(env, keylet, 1);
        env(depositTx(owner, keylet, XRP(100)));
        env(depositTx(lender, keylet, XRP(100), mezzanine));
        env(depositTx(lender, keylet, XRP(800), senior));
        env.close();
        auto const [broker, loanKeylet] = createLoan(env, owner, borrower, keylet, XRP(150));
        env.close(std::chrono::seconds{60 + 60 + 1});
        env(loan::manage(owner, loanKeylet.key, loanFlags));
        env.close();
        return {keylet, senior, mezzanine};
    }

    void
    testDefaultWaterfall()
    {
        testcase("default falls on equity, then the junior debt class");
        using namespace test::jtx;

        Env env(*this, openLending_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(100'000), owner, lender, borrower);
        env.close();

        auto const stack = makeLoanStack(env, owner, lender, borrower, tfLoanDefault);
        BEAST_EXPECT(
            classValues(env, stack.vault) ==
            (std::vector<Number>{Number(XRP(800).value()), Number(XRP(50).value()), 0}));
    }

    void
    testImpairmentWaterfall()
    {
        testcase("impairment falls on equity, then the junior debt class");
        using namespace test::jtx;

        Env env(*this, openLending_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(100'000), owner, lender, borrower);
        env.close();

        auto const stack = makeLoanStack(env, owner, lender, borrower, tfLoanImpair);
        BEAST_EXPECT(
            classValues(env, stack.vault) ==
            (std::vector<Number>{Number(XRP(800).value()), Number(XRP(50).value()), 0}));
    }

    void
    testFinalSweep()
    {
        testcase("the final redemption sweeps the vault");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        env.fund(XRP(100'000), owner, lender);
        env.close();

        auto const keylet = createVault(env, owner, {{.interestRate = 5'000}});
        auto const senior = trancheShare(env, keylet, 0);
        auto const equity = equityShare(env, keylet);
        env(depositTx(owner, keylet, XRP(1'000)));
        env(depositTx(lender, keylet, XRP(1'000), senior));
        env.close(env.now() + std::chrono::seconds{kSecondsInYear});

        env(withdrawTx(lender, keylet, shares(senior, sharesHeld(env, lender, senior))));
        env.close();
        BEAST_EXPECT(env.le(keylet)->at(sfAssetsTotal) > beast::kZero);

        env(withdrawTx(owner, keylet, shares(equity, sharesHeld(env, owner, equity))));
        env.close();
        auto const sle = env.le(keylet);
        BEAST_EXPECT(sle->at(sfAssetsTotal) == beast::kZero);
        BEAST_EXPECT(sle->at(sfAssetsAvailable) == beast::kZero);
    }

    void
    testRedeemAllAfterMaturity()
    {
        testcase("every class redeems after maturity");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(100'000), owner, lender, borrower);
        env.close();

        Vault const vault{env};
        auto [tx, keylet, subscriptionDate] =
            vault.createClosedEnded({.owner = owner, .asset = xrpIssue()});
        addTranches(tx, {{.interestRate = 1'000}, {.interestRate = 2'000}});
        env(tx);
        env.close();
        auto const senior = trancheShare(env, keylet, 0);
        auto const mezzanine = trancheShare(env, keylet, 1);
        auto const equity = equityShare(env, keylet);
        env(depositTx(owner, keylet, XRP(100)));
        env(depositTx(lender, keylet, XRP(100), mezzanine));
        env(depositTx(lender, keylet, XRP(800), senior));
        env.close();

        vault.closePastSubscription(subscriptionDate);
        auto const [broker, loanKeylet] = createLoan(env, owner, borrower, keylet, XRP(500));
        env(loan::pay(borrower, loanKeylet.key, XRP(500)));
        env.close();

        auto const redemptionDate = env.le(keylet)->at(~sfRedemptionDate);
        if (!BEAST_EXPECT(redemptionDate))
            return;
        closeToTime(env, tp{d{*redemptionDate}});

        env(withdrawTx(lender, keylet, shares(senior, sharesHeld(env, lender, senior))));
        env(withdrawTx(lender, keylet, shares(mezzanine, sharesHeld(env, lender, mezzanine))));
        env(withdrawTx(owner, keylet, shares(equity, sharesHeld(env, owner, equity))));
        env.close();

        auto const sle = env.le(keylet);
        BEAST_EXPECT(sle->at(sfAssetsTotal) == beast::kZero);
        BEAST_EXPECT(sharesOutstanding(env, senior) == 0);
        BEAST_EXPECT(sharesOutstanding(env, mezzanine) == 0);
        BEAST_EXPECT(sharesOutstanding(env, equity) == 0);
    }

public:
    void
    run() override
    {
        testCreateDisabled();
        testCreate();
        testCreatePrivate();
        testCreateNonTransferable();
        testCreateMalformedEmpty();
        testCreateMalformedTooMany();
        testCreateMalformedShareID();
        testCreateMalformedPrincipal();
        testCreateMalformedRate();
        testCreateMalformedSubordination();
        testDepositEquityThenSenior();
        testDepositAtValue();
        testCouponTransfersValue();
        testDepositImpairedDebt();
        testDepositImpairedEquity();
        testDepositDebtWithoutEquity();
        testDepositCoverage();
        testDepositUnknownClass();
        testDepositPrivateClass();
        testWithdrawSenior();
        testWithdrawLastShares();
        testWithdrawBySharesAmount();
        testWithdrawClassMismatch();
        testCoverageBlocksEquity();
        testSeniorWithdrawWhileBreached();
        testEquityLast();
        testDefaultWaterfall();
        testImpairmentWaterfall();
        testFinalSweep();
        testRedeemAllAfterMaturity();
    }
};

BEAST_DEFINE_TESTSUITE(VaultTranches, app, xrpl);

}  // namespace xrpl
