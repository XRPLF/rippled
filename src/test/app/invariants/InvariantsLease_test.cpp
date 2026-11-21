#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <functional>
#include <memory>
#include <string>

namespace xrpl::test {

/**
 * ValidLease (AccountLeasing): a lease loan, its lease account and the
 * accounts the lease sponsors.
 */
class InvariantsLease_test : public InvariantsBase
{
    // Set by createLease: the lease loan, its lease account and one leased
    // account.
    Keylet loanKeylet_{ltLOAN, beast::kZero};
    AccountID leaseAccount_;
    AccountID leased_;

    // a: vault and broker owner (lender); b: borrower.
    Preclose
    createLease()
    {
        return [this](jtx::Account const& a, jtx::Account const& b, jtx::Env& env) {
            using namespace jtx;
            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            // The harness funds a and b with 1,000 XRP; the vault needs more.
            env(pay(env.master, a, XRP(100'000)));
            env(pay(env.master, b, XRP(10'000)));
            env.close();

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = a, .asset = xrpAsset});
            env(tx);
            env.close();
            env(vault.deposit({.depositor = a, .id = vaultKeylet.key, .amount = xrpAsset(10'000)}));
            env.close();

            auto const brokerKeylet = keylet::loanBroker(a.id(), SeqProxy::rawSequence(env.seq(a)));
            env(loan_broker::set(a, vaultKeylet.key));
            env.close();

            auto const brokerSle = env.le(brokerKeylet);
            if (!BEAST_EXPECT(brokerSle))
                return false;
            loanKeylet_ = keylet::loan(
                brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
            XRPAmount const reserve = env.current()->fees().reserve;
            env(loan::set(b, brokerKeylet.key, Number{reserve.drops() * 2}, tfLoanLease),
                loan::kCounterparty(a),
                Sig(sfCounterpartySignature, a),
                Fee(env.current()->fees().base * 5));
            env.close();

            auto const loanSle = env.le(loanKeylet_);
            if (!BEAST_EXPECT(loanSle && loanSle->at(~sfAccount)))
                return false;
            leaseAccount_ = *loanSle->at(~sfAccount);

            Account const user{"user"};
            env.memoize(user);
            env(pay(b, user, XRP(1)),
                Txflags(tfSponsorCreatedAccount),
                Json(sfLoanID.jsonName, to_string(loanKeylet_.key)),
                Fee(XRP(1)));
            env.close();
            leased_ = user.id();
            return BEAST_EXPECT(env.le(keylet::account(leased_)));
        };
    }

    void
    check(std::string const& expected, Precheck const& precheck, TxType txType = ttACCOUNT_SET)
    {
        // The harness's default set has LendingProtocolV1_1, which needs a
        // closed-ended vault for a broker; this test uses an open-ended one.
        doInvariantCheck(
            makeEnv(
                jtx::testableAmendments() - featureLendingProtocolV1_1 -
                featureLendingProtocolV1_2),
            {{expected}},
            precheck,
            XRPAmount{},
            STTx{txType, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createLease());
    }

    void
    testLeaseAccount()
    {
        testcase("ValidLease: lease account");
        using namespace jtx;

        // Balance below the leased accounts' units.
        check(
            "lease account holds less than its leased accounts' units",
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::account(leaseAccount_));
                if (!sle)
                    return false;
                sle->at(sfBalance) = STAmount{XRPAmount{1}};
                ac.view().update(sle);
                return true;
            },
            ttLOAN_PAY);

        // A sponsored lease account.
        check(
            "lease account owns objects or is sponsored",
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::account(leaseAccount_));
                if (!sle)
                    return false;
                sle->at(~sfSponsor) = a1.id();
                ac.view().update(sle);
                return true;
            });

        // Balance moved by a transaction that may not move it.
        check(
            "lease account balance changed by a transaction that may not move it",
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto lease = ac.view().peek(keylet::account(leaseAccount_));
                auto other = ac.view().peek(keylet::account(a1.id()));
                if (!lease || !other)
                    return false;
                lease->at(sfBalance) = lease->at(sfBalance) + STAmount{XRPAmount{1}};
                other->at(sfBalance) = other->at(sfBalance) - STAmount{XRPAmount{1}};
                ac.view().update(lease);
                ac.view().update(other);
                return true;
            });

        // Balance moved by a SponsorshipTransfer that is not the takeover.
        check(
            "lease account balance changed by a transaction that may not move it",
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto lease = ac.view().peek(keylet::account(leaseAccount_));
                auto other = ac.view().peek(keylet::account(a1.id()));
                if (!lease || !other)
                    return false;
                lease->at(sfBalance) = lease->at(sfBalance) - STAmount{XRPAmount{1}};
                other->at(sfBalance) = other->at(sfBalance) + STAmount{XRPAmount{1}};
                ac.view().update(lease);
                ac.view().update(other);
                return true;
            },
            ttSPONSORSHIP_TRANSFER);

        // A lease account with an owner directory.
        check(
            "lease account owns objects or is sponsored",
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto lease = ac.view().peek(keylet::account(leaseAccount_));
                if (!lease)
                    return false;
                auto const dirKeylet = keylet::ownerDir(leaseAccount_);
                auto dir = std::make_shared<SLE>(dirKeylet);
                dir->setAccountID(sfOwner, leaseAccount_);
                dir->setFieldH256(sfRootIndex, dirKeylet.key);
                ac.view().insert(dir);
                ac.view().update(lease);
                return true;
            });

        // A lease account with no lease loan.
        check(
            "lease account without its lease loan",
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::account(leaseAccount_));
                if (!sle)
                    return false;
                sle->at(sfLoanID) = UInt256{1};
                ac.view().update(sle);
                return true;
            });
    }

    void
    testLeaseLoan()
    {
        testcase("ValidLease: lease loan");
        using namespace jtx;

        // The loan's terms change.
        check(
            "lease loan terms changed",
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(loanKeylet_);
                if (!sle)
                    return false;
                sle->at(~sfLeaseUnit) = Number{1};
                ac.view().update(sle);
                return true;
            },
            ttLOAN_MANAGE);

        // Claims on a loan that is not defaulted.
        check(
            "lease claims set on a loan that is not defaulted",
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(loanKeylet_);
                if (!sle)
                    return false;
                sle->at(sfLeaseClaimVault) = Number{1};
                ac.view().update(sle);
                return true;
            },
            ttLOAN_MANAGE);

        // The loan is erased and its lease account stays.
        check(
            "lease loan deleted without its lease account",
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(loanKeylet_);
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return true;
            },
            ttLOAN_DELETE);
    }

    void
    testLeasedAccount()
    {
        testcase("ValidLease: leased account");
        using namespace jtx;

        // An existing account whose Sponsor becomes a lease account.
        check(
            "leased account created by a transaction other than Payment",
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                sle->setAccountID(sfSponsor, leaseAccount_);
                ac.view().update(sle);
                return true;
            },
            ttPAYMENT);

        // A leased account created by a transaction other than Payment.
        check(
            "leased account created by a transaction other than Payment",
            [&](Account const&, Account const&, ApplyContext& ac) {
                Account const other{"other"};
                auto sle = std::make_shared<SLE>(keylet::account(other.id()));
                sle->setAccountID(sfAccount, other.id());
                sle->setFieldU32(sfSequence, ac.view().seq());
                sle->setFieldAmount(sfBalance, STAmount{XRPAmount{0}});
                sle->setAccountID(sfSponsor, leaseAccount_);
                ac.view().insert(sle);
                return true;
            });
    }

public:
    void
    run() override
    {
        testLeaseAccount();
        testLeaseLoan();
        testLeasedAccount();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsLease, app, xrpl);

}  // namespace xrpl::test
