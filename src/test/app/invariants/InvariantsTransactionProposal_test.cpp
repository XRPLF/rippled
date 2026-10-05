#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/proposal.h>
#include <test/jtx/utility.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/ProposalHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <vector>

namespace xrpl::test {

class InvariantsTransactionProposal_test : public InvariantsBase
{
    void
    testTransactionProposalInvariants()
    {
        using namespace jtx;
        using namespace std::chrono_literals;

        // Submits a real proposal, so the entry is in the closed ledger before
        // the precheck runs and the precheck can modify it.
        std::optional<Keylet> liveProposalKeylet;
        Preclose const precloseLiveProposal =
            [&](Account const& owner, Account const& destination, Env& env) {
                auto const ticketSeq = proposal::createTicket(env, owner);
                auto const proposedTx =
                    proposal::unsignedPayload(env, pay(owner, destination, XRP(1)), ticketSeq);
                liveProposalKeylet = keylet::txProposal(owner.id(), ticketSeq);
                env(proposal::create(owner, proposedTx, proposal::expiration(env, 100s)));
                return BEAST_EXPECT(env.le(*liveProposalKeylet));
            };

        auto peekLiveProposal = [&](ApplyContext& ac) -> SLE::pointer {
            if (!liveProposalKeylet)
                return nullptr;
            return ac.view().peek(*liveProposalKeylet);
        };

        testcase("TransactionProposal immutable payload");
        doInvariantCheck(
            {{"TransactionProposal immutable fields changed"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sle = peekLiveProposal(ac);
                if (!sle)
                    return false;
                auto proposedTx = sle->getFieldObject(sfProposedTransaction);
                proposedTx.setFieldU32(
                    sfTicketSequence, proposedTx.getFieldU32(sfTicketSequence) + 1);
                sle->setFieldObject(sfProposedTransaction, proposedTx);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposal);

        // Only stages the payload and the key it would live under; nothing
        // reaches the ledger, so a precheck that inserts the entry by hand
        // presents the invariant with a creation.
        std::optional<STObject> stagedPayload;
        std::optional<Keylet> stagedProposalKeylet;
        Preclose const precloseStagedPayload =
            [&](Account const& owner, Account const& destination, Env& env) {
                auto const ticketSeq = proposal::createTicket(env, owner);
                auto const proposedJson =
                    proposal::unsignedPayload(env, pay(owner, destination, XRP(1)), ticketSeq);
                stagedPayload.emplace(parse(proposedJson));
                stagedProposalKeylet = keylet::txProposal(owner.id(), ticketSeq);
                return true;
            };

        // With a sponsor, the entry is stamped and the owner's sponsored count
        // moves with the owner count; charging the sponsor is left to the caller.
        auto insertStagedProposal = [&](Account const& owner,
                                        ApplyContext& ac,
                                        bool updateOwnerCount,
                                        std::optional<AccountID> const& sponsor = std::nullopt) {
            if (!stagedPayload || !stagedProposalKeylet)
                return false;

            auto sle = std::make_shared<SLE>(*stagedProposalKeylet);
            sle->setAccountID(sfOwner, owner.id());
            sle->setFieldObject(sfProposedTransaction, *stagedPayload);
            sle->setFieldU32(sfExpiration, 1);
            sle->setFieldU64(sfOwnerNode, 0);
            if (sponsor)
                sle->setAccountID(sfSponsor, *sponsor);
            ac.view().insert(sle);

            if (updateOwnerCount)
            {
                auto account = ac.view().peek(keylet::account(owner.id()));
                if (!account)
                    return false;
                auto const reserve = xrpl::proposal::proposalOwnerCount(*stagedPayload);
                account->at(sfOwnerCount) += reserve;
                if (sponsor)
                    account->at(sfSponsoredOwnerCount) += reserve;
                ac.view().update(account);
            }
            return true;
        };

        testcase("TransactionProposal reserve accounting");
        doInvariantCheck(
            {{"TransactionProposal reserve accounting is inconsistent"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return insertStagedProposal(owner, ac, false);
            },
            XRPAmount{},
            STTx{ttTRANSACTION_PROPOSAL_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseStagedPayload);

        // The totals still balance when a sponsored proposal's reserve is
        // charged to the wrong sponsor, so SponsorshipOwnerCountsMatch stays
        // quiet and only this invariant's per-account SponsoringOwnerCount
        // comparison can catch it.
        Account const bystander{"bystander"};
        Preclose const precloseStagedPayloadAndBystander =
            [&](Account const& owner, Account const& destination, Env& env) {
                env.fund(XRP(1000), bystander);
                env.close();
                return precloseStagedPayload(owner, destination, env);
            };

        testcase("TransactionProposal reserve charged to the wrong sponsor");
        doInvariantCheck(
            {{"TransactionProposal reserve accounting is inconsistent"}},
            [&](Account const& owner, Account const& sponsor, ApplyContext& ac) {
                if (!insertStagedProposal(owner, ac, true, sponsor.id()))
                    return false;
                auto wrongSponsor = ac.view().peek(keylet::account(bystander.id()));
                if (!wrongSponsor)
                    return false;
                wrongSponsor->at(sfSponsoringOwnerCount) +=
                    xrpl::proposal::proposalOwnerCount(*stagedPayload);
                ac.view().update(wrongSponsor);
                return true;
            },
            XRPAmount{},
            STTx{ttTRANSACTION_PROPOSAL_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseStagedPayloadAndBystander);

        // The sponsor is charged and the owner count moves, but the owner's
        // sponsored count never does.
        testcase("TransactionProposal sponsored count not raised");
        doInvariantCheck(
            {{"TransactionProposal reserve accounting is inconsistent"}},
            [&](Account const& owner, Account const& sponsor, ApplyContext& ac) {
                if (!insertStagedProposal(owner, ac, false, sponsor.id()))
                    return false;
                auto const reserve = xrpl::proposal::proposalOwnerCount(*stagedPayload);
                auto ownerRoot = ac.view().peek(keylet::account(owner.id()));
                auto sponsorRoot = ac.view().peek(keylet::account(sponsor.id()));
                if (!ownerRoot || !sponsorRoot)
                    return false;
                ownerRoot->at(sfOwnerCount) += reserve;
                ac.view().update(ownerRoot);
                sponsorRoot->at(sfSponsoringOwnerCount) += reserve;
                ac.view().update(sponsorRoot);
                return true;
            },
            XRPAmount{},
            STTx{ttTRANSACTION_PROPOSAL_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseStagedPayload);

        testcase("TransactionProposal effect whitelist");
        doInvariantCheck(
            {{"TransactionProposal changes do not match transaction result"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return insertStagedProposal(owner, ac, true);
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseStagedPayload);

        // The harness runs no transactor, so a failed result has to be fed in
        // directly. A failed create must leave no trace.
        testcase("TransactionProposal failed create still inserts");
        doInvariantCheck(
            {{"TransactionProposal changes do not match transaction result"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return insertStagedProposal(owner, ac, true);
            },
            XRPAmount{},
            STTx{ttTRANSACTION_PROPOSAL_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseStagedPayload,
            TxAccount::None,
            std::source_location::current(),
            tecNO_PERMISSION);

        // A failed transfer must not have reassigned anything. The counters are
        // kept consistent so only the result mismatch is left to catch.
        testcase("TransactionProposal failed sponsorship transfer still reassigns");
        doInvariantCheck(
            {{"TransactionProposal changes do not match transaction result"}},
            [&](Account const& owner, Account const& sponsor, ApplyContext& ac) {
                auto sle = peekLiveProposal(ac);
                if (!sle)
                    return false;
                auto const reserve =
                    xrpl::proposal::proposalOwnerCount(sle->getFieldObject(sfProposedTransaction));
                sle->setAccountID(sfSponsor, sponsor.id());
                ac.view().update(sle);

                auto account = ac.view().peek(keylet::account(owner.id()));
                auto sponsorRoot = ac.view().peek(keylet::account(sponsor.id()));
                if (!account || !sponsorRoot)
                    return false;
                account->at(sfSponsoredOwnerCount) += reserve;
                ac.view().update(account);
                sponsorRoot->at(sfSponsoringOwnerCount) += reserve;
                ac.view().update(sponsorRoot);
                return true;
            },
            XRPAmount{},
            STTx{ttSPONSORSHIP_TRANSFER, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposal,
            TxAccount::None,
            std::source_location::current(),
            tecNO_PERMISSION);

        // No transaction type on this amendment branch deletes a proposal yet,
        // so a deletion has to be staged by hand. Releasing the reserve, as a
        // real deletion would, leaves the deletion itself as the only violation.
        testcase("TransactionProposal deletion outside the whitelist");
        doInvariantCheck(
            {{"TransactionProposal changes do not match transaction result"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                auto sle = peekLiveProposal(ac);
                if (!sle)
                    return false;
                auto account = ac.view().peek(keylet::account(owner.id()));
                if (!account)
                    return false;
                account->at(sfOwnerCount) -=
                    xrpl::proposal::proposalOwnerCount(sle->getFieldObject(sfProposedTransaction));
                ac.view().update(account);
                if (!ac.view().dirRemove(
                        keylet::ownerDir(owner.id()),
                        sle->getFieldU64(sfOwnerNode),
                        sle->key(),
                        false))
                    return false;
                ac.view().erase(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposal);

        // A SponsorshipTransfer may reassign the Sponsor and nothing else.
        testcase("TransactionProposal sponsorship transfer touches another field");
        doInvariantCheck(
            {{"TransactionProposal immutable fields changed"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sle = peekLiveProposal(ac);
                if (!sle)
                    return false;
                sle->setFieldU32(sfExpiration, sle->getFieldU32(sfExpiration) + 1);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttSPONSORSHIP_TRANSFER, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposal);

        // Distinct AccountIDs in ascending order, so an array built from them
        // violates only the bound on its length.
        auto ascendingAccounts = [](std::size_t count) {
            std::vector<AccountID> accounts;
            accounts.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                AccountID account{beast::kZero};
                account.data()[0] = static_cast<std::uint8_t>(i + 1);
                accounts.push_back(account);
            }
            return accounts;
        };

        auto signerArray = [](std::vector<AccountID> const& accounts) {
            STArray signers{sfSigners};
            for (auto const& account : accounts)
            {
                STObject signer{sfSigner};
                signer.setAccountID(sfAccount, account);
                signers.pushBack(signer);
            }
            return signers;
        };

        // No transaction type on this amendment branch collects signatures yet,
        // so every rejection below has to be provoked by editing the stored
        // payload directly.
        auto testBrokenSignerArray = [&](std::string const& name, auto const& breakPayload) {
            testcase("TransactionProposal canonical signer arrays: " + name);
            doInvariantCheck(
                {{"TransactionProposal signer arrays are not canonical"}},
                [&](Account const&, Account const& signerAccount, ApplyContext& ac) {
                    auto sle = peekLiveProposal(ac);
                    if (!sle)
                        return false;
                    auto proposedTx = sle->getFieldObject(sfProposedTransaction);
                    breakPayload(proposedTx, signerAccount);
                    sle->setFieldObject(sfProposedTransaction, proposedTx);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLiveProposal);
        };

        testBrokenSignerArray("duplicate entries", [&](STObject& tx, Account const& signer) {
            tx.setFieldArray(sfSigners, signerArray({signer.id(), signer.id()}));
        });

        testBrokenSignerArray("descending order", [&](STObject& tx, Account const&) {
            auto accounts = ascendingAccounts(2);
            std::ranges::reverse(accounts);
            tx.setFieldArray(sfSigners, signerArray(accounts));
        });

        testBrokenSignerArray("past the multi-sign limit", [&](STObject& tx, Account const&) {
            tx.setFieldArray(sfSigners, signerArray(ascendingAccounts(STTx::kMaxMultiSigners + 1)));
        });

        testBrokenSignerArray("entry without an Account", [](STObject& tx, Account const&) {
            STArray signers{sfSigners};
            signers.pushBack(STObject{sfSigner});
            tx.setFieldArray(sfSigners, signers);
        });

        for (auto const* field :
             std::array<SField const*, 2>{&sfCounterpartySignature, &sfSponsorSignature})
        {
            testBrokenSignerArray(
                "duplicates under " + field->getName(), [&](STObject& tx, Account const& signer) {
                    STObject signature{*field};
                    signature.setFieldArray(sfSigners, signerArray({signer.id(), signer.id()}));
                    tx.setFieldObject(*field, signature);
                });
        }

        testBrokenSignerArray("past the batch-signer limit", [&](STObject& tx, Account const&) {
            STArray batchSigners{sfBatchSigners};
            for (auto const& account : ascendingAccounts(kMaxBatchSigners + 1))
            {
                STObject batchSigner{sfBatchSigner};
                batchSigner.setAccountID(sfAccount, account);
                batchSigners.pushBack(batchSigner);
            }
            tx.setFieldArray(sfBatchSigners, batchSigners);
        });

        testBrokenSignerArray(
            "duplicates under a BatchSigner", [&](STObject& tx, Account const& signer) {
                STObject batchSigner{sfBatchSigner};
                batchSigner.setAccountID(sfAccount, signer.id());
                batchSigner.setFieldArray(sfSigners, signerArray({signer.id(), signer.id()}));

                STArray batchSigners{sfBatchSigners};
                batchSigners.pushBack(batchSigner);
                tx.setFieldArray(sfBatchSigners, batchSigners);
            });

        // Negative controls. These share the setup of the failure cases above,
        // so they confirm each of those failures came from the defect it names
        // rather than from the hand-built ledger state.
        std::initializer_list<TER> const passTers = {tesSUCCESS, tesSUCCESS};

        testcase("TransactionProposal well-formed create");
        doInvariantCheck(
            {},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return insertStagedProposal(owner, ac, true);
            },
            XRPAmount{},
            STTx{ttTRANSACTION_PROPOSAL_CREATE, [](STObject&) {}},
            passTers,
            precloseStagedPayload);

        testcase("TransactionProposal left untouched");
        doInvariantCheck(
            {},
            [&](Account const&, Account const&, ApplyContext& ac) {
                return liveProposalKeylet && ac.view().read(*liveProposalKeylet) != nullptr;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            passTers,
            precloseLiveProposal);
    }

    void
    run() override
    {
        testTransactionProposalInvariants();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsTransactionProposal, app, xrpl);

}  // namespace xrpl::test
