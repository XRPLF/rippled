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
#include <xrpl/protocol/SeqProxy.h>
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
#include <utility>
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
        std::optional<std::uint32_t> liveTicketSeq;
        Preclose const precloseLiveProposal =
            [&](Account const& owner, Account const& destination, Env& env) {
                auto const ticketSeq = proposal::createTicket(env, owner);
                auto const proposedTx =
                    proposal::unsignedPayload(env, pay(owner, destination, XRP(1)), ticketSeq);
                liveProposalKeylet = keylet::txProposal(owner.id(), ticketSeq);
                liveTicketSeq = ticketSeq;
                env(proposal::create(owner, proposedTx, proposal::expiration(env, 100s)));
                return BEAST_EXPECT(env.le(*liveProposalKeylet));
            };

        auto peekLiveProposal = [&](ApplyContext& ac) -> SLE::pointer {
            if (!liveProposalKeylet)
                return nullptr;
            return ac.view().peek(*liveProposalKeylet);
        };

        // Deletes the live proposal as deleteProposal would: out of the
        // owner's directory, with the owner's reserve released.
        auto deleteLiveProposal = [&](Account const& owner, ApplyContext& ac) {
            auto const sle = peekLiveProposal(ac);
            if (!sle)
                return false;
            auto const account = ac.view().peek(keylet::account(owner.id()));
            if (!account)
                return false;
            account->at(sfOwnerCount) -=
                xrpl::proposal::proposalOwnerCount(sle->getFieldObject(sfProposedTransaction));
            ac.view().update(account);
            if (!ac.view().dirRemove(
                    keylet::ownerDir(owner.id()), sle->getFieldU64(sfOwnerNode), sle->key(), false))
                return false;
            ac.view().erase(sle);
            return true;
        };

        // Consumes one of the owner's Tickets as ticketDelete would.
        auto deleteTicket = [&](Account const& owner, std::uint32_t ticketSeq, ApplyContext& ac) {
            auto const ticket =
                ac.view().peek(keylet::ticket(owner.id(), SeqProxy::rawTicket(ticketSeq)));
            auto const account = ac.view().peek(keylet::account(owner.id()));
            if (!ticket || !account)
                return false;
            if (!ac.view().dirRemove(
                    keylet::ownerDir(owner.id()),
                    ticket->getFieldU64(sfOwnerNode),
                    ticket->key(),
                    false))
                return false;
            if (auto const ticketCount = account->getFieldU32(sfTicketCount); ticketCount > 1)
            {
                account->setFieldU32(sfTicketCount, ticketCount - 1);
            }
            else
            {
                account->makeFieldAbsent(sfTicketCount);
            }
            account->at(sfOwnerCount) -= 1;
            ac.view().update(account);
            ac.view().erase(ticket);
            return true;
        };

        // A transaction outside the whitelist may not modify a proposal at
        // all; the invariant rejects it on the count, whatever field changed.
        testcase("TransactionProposal modified outside the whitelist");
        doInvariantCheck(
            {{"TransactionProposal changes do not match transaction result"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = peekLiveProposal(ac);
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

        // Inserts the staged proposal as doApply would, with the owner's
        // reserve charged.
        auto insertStagedProposal = [&](Account const& owner, ApplyContext& ac) {
            if (!stagedPayload || !stagedProposalKeylet)
                return false;

            auto const sle = std::make_shared<SLE>(*stagedProposalKeylet);
            sle->setAccountID(sfOwner, owner.id());
            sle->setFieldObject(sfProposedTransaction, *stagedPayload);
            sle->setFieldU32(sfExpiration, 1);
            sle->setFieldU64(sfOwnerNode, 0);
            ac.view().insert(sle);

            auto const account = ac.view().peek(keylet::account(owner.id()));
            if (!account)
                return false;
            account->at(sfOwnerCount) += xrpl::proposal::proposalOwnerCount(*stagedPayload);
            ac.view().update(account);
            return true;
        };

        testcase("TransactionProposal effect whitelist");
        doInvariantCheck(
            {{"TransactionProposal changes do not match transaction result"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return insertStagedProposal(owner, ac);
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
                return insertStagedProposal(owner, ac);
            },
            XRPAmount{},
            STTx{ttTRANSACTION_PROPOSAL_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseStagedPayload,
            TxAccount::None,
            std::source_location::current(),
            tecNO_PERMISSION);

        // Reassigns the live proposal's reserve to a sponsor as a
        // SponsorshipTransfer would. The sponsored and sponsoring counts move
        // with it so that SponsorshipOwnerCountsMatch stays quiet.
        auto const reassignLiveProposal =
            [&](Account const& owner, Account const& sponsor, ApplyContext& ac) {
                auto const sle = peekLiveProposal(ac);
                if (!sle)
                    return false;
                auto const reserve =
                    xrpl::proposal::proposalOwnerCount(sle->getFieldObject(sfProposedTransaction));
                sle->setAccountID(sfSponsor, sponsor.id());
                ac.view().update(sle);

                auto const account = ac.view().peek(keylet::account(owner.id()));
                auto const sponsorRoot = ac.view().peek(keylet::account(sponsor.id()));
                if (!account || !sponsorRoot)
                    return false;
                account->at(sfSponsoredOwnerCount) += reserve;
                ac.view().update(account);
                sponsorRoot->at(sfSponsoringOwnerCount) += reserve;
                ac.view().update(sponsorRoot);
                return true;
            };

        // A failed transfer must not have reassigned anything. The counters are
        // kept consistent so only the result mismatch is left to catch.
        testcase("TransactionProposal failed sponsorship transfer still reassigns");
        doInvariantCheck(
            {{"TransactionProposal changes do not match transaction result"}},
            reassignLiveProposal,
            XRPAmount{},
            STTx{ttSPONSORSHIP_TRANSFER, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposal,
            TxAccount::None,
            std::source_location::current(),
            tecNO_PERMISSION);

        // A proposal leaves the ledger only through TransactionProposalCancel
        // or together with the Ticket it is keyed to. The harness runs no
        // transactor, so each deletion below is staged by hand, releasing the
        // reserve as a real one would, to leave the deletion itself as the
        // only possible violation.
        testcase("TransactionProposal deletion outside the whitelist");
        doInvariantCheck(
            {{"TransactionProposal changes do not match transaction result"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return deleteLiveProposal(owner, ac);
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposal);

        // The owner holds a second Ticket, one no proposal is keyed to.
        std::optional<std::uint32_t> spareTicketSeq;
        Preclose const precloseLiveProposalAndSpareTicket =
            [&](Account const& owner, Account const& destination, Env& env) {
                spareTicketSeq = proposal::createTicket(env, owner);
                return precloseLiveProposal(owner, destination, env);
            };

        // Consuming some other Ticket of the owner's is no reason for the
        // proposal to go.
        testcase("TransactionProposal deleted with an unrelated ticket");
        doInvariantCheck(
            {{"TransactionProposal changes do not match transaction result"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return spareTicketSeq && deleteTicket(owner, *spareTicketSeq, ac) &&
                    deleteLiveProposal(owner, ac);
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposalAndSpareTicket);

        // And a deleted Ticket takes the proposal keyed to it along: here the
        // Ticket went and the proposal stayed behind.
        testcase("TransactionProposal outlives its ticket");
        doInvariantCheck(
            {{"TransactionProposal outlived the Ticket it is keyed to"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return liveTicketSeq && deleteTicket(owner, *liveTicketSeq, ac);
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposal);

        // A Cancel names the live proposal and may pay with the owner's spare
        // Ticket. Both exist only once the preclose has run, so the Env is
        // built here rather than by the harness.
        struct CancelOptions
        {
            // The Cancel pays with A1's spare Ticket.
            bool paySpareTicket = false;
            TER initialResult = tesSUCCESS;
        };
        auto const checkCancel = [&](std::vector<std::string> const& expectLogs,
                                     Precheck const& precheck,
                                     std::initializer_list<TER> ters,
                                     Preclose const& preclose,
                                     CancelOptions const& options = CancelOptions(),
                                     std::source_location const& loc =
                                         std::source_location::current()) {
            Account const a1{"A1"};
            Account const a2{"A2"};
            Env env = makeEnv(testableAmendments());
            env.fund(XRP(1000), a1, a2);
            if (!BEAST_EXPECT(preclose(a1, a2, env)) || !BEAST_EXPECT(liveProposalKeylet))
                return;
            env.close();

            STTx const tx{ttTRANSACTION_PROPOSAL_CANCEL, [&](STObject& obj) {
                              obj.setAccountID(sfAccount, a1.id());
                              obj.setFieldH256(sfProposalID, liveProposalKeylet->key);
                              if (options.paySpareTicket && spareTicketSeq)
                              {
                                  obj.setFieldU32(sfSequence, 0);
                                  obj.setFieldU32(sfTicketSequence, *spareTicketSeq);
                              }
                          }};
            doInvariantCheck(
                std::move(env),
                a1,
                a2,
                expectLogs,
                precheck,
                XRPAmount{},
                tx,
                ters,
                loc,
                options.initialResult);
        };

        auto const cancelDeletesLiveProposal =
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return deleteLiveProposal(owner, ac);
            };

        // A Cancel that did not succeed deleted nothing.
        testcase("TransactionProposal failed cancel still deletes");
        {
            CancelOptions options;
            options.initialResult = tecNO_PERMISSION;
            checkCancel(
                {{"TransactionProposal changes do not match transaction result"}},
                cancelDeletesLiveProposal,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLiveProposal,
                options);
        }

        // A Cancel cannot spend a reserved Ticket, so the proposal it deletes
        // never goes with its Ticket.
        testcase("TransactionProposal cancel consumes the reserved ticket");
        checkCancel(
            {{"TransactionProposal changes do not match transaction result"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return liveTicketSeq && deleteTicket(owner, *liveTicketSeq, ac) &&
                    deleteLiveProposal(owner, ac);
            },
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
        auto const testBrokenSignerArray = [&](std::string const& name, auto const& breakPayload) {
            testcase("TransactionProposal canonical signer arrays: " + name);
            doInvariantCheck(
                {{"TransactionProposal signer arrays are not canonical"}},
                [&](Account const&, Account const& signerAccount, ApplyContext& ac) {
                    auto const sle = peekLiveProposal(ac);
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
                return insertStagedProposal(owner, ac);
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

        // A Cancel deletes the proposal it names.
        testcase("TransactionProposal cancelled");
        checkCancel({}, cancelDeletesLiveProposal, passTers, precloseLiveProposal);

        // The Ticket a Cancel pays with is not the one its proposal is keyed
        // to, so the proposal did not go with its Ticket.
        testcase("TransactionProposal cancel paid with the canceller's own ticket");
        {
            CancelOptions options;
            options.paySpareTicket = true;
            checkCancel(
                {},
                [&](Account const& owner, Account const&, ApplyContext& ac) {
                    return spareTicketSeq && deleteTicket(owner, *spareTicketSeq, ac) &&
                        deleteLiveProposal(owner, ac);
                },
                passTers,
                precloseLiveProposalAndSpareTicket,
                options);
        }

        // A SponsorshipTransfer reassigns the Sponsor.
        testcase("TransactionProposal sponsor reassigned");
        doInvariantCheck(
            {},
            reassignLiveProposal,
            XRPAmount{},
            STTx{ttSPONSORSHIP_TRANSFER, [](STObject&) {}},
            passTers,
            precloseLiveProposal);

        // The proposed transaction executing spends the proposal's Ticket and
        // takes the proposal with it.
        testcase("TransactionProposal deleted with its ticket");
        doInvariantCheck(
            {},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return liveTicketSeq && deleteTicket(owner, *liveTicketSeq, ac) &&
                    deleteLiveProposal(owner, ac);
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
