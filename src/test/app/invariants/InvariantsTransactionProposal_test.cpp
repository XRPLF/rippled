#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/proposal.h>
#include <test/jtx/utility.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
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

        // The same proposal, but terminal by the time the transaction applies:
        // its LastLedgerSequence is the ledger after the one it is created in.
        // The harness closes once more before applying, so the extra close
        // here puts the open ledger past that bound.
        Preclose const precloseTerminalProposal =
            [&](Account const& owner, Account const& destination, Env& env) {
                auto const ticketSeq = proposal::createTicket(env, owner);
                json::Value proposedTx =
                    proposal::unsignedPayload(env, pay(owner, destination, XRP(1)), ticketSeq);
                proposedTx[sfLastLedgerSequence.getJsonName()] = env.current()->seq() + 1;
                liveProposalKeylet = keylet::txProposal(owner.id(), ticketSeq);
                liveTicketSeq = ticketSeq;
                env(proposal::create(owner, proposedTx, proposal::expiration(env, 100s)));
                env.close();
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

        // Mints a Ticket for the owner, as TicketCreate would.
        auto insertTicket = [&](Account const& owner, std::uint32_t ticketSeq, ApplyContext& ac) {
            auto const account = ac.view().peek(keylet::account(owner.id()));
            if (!account)
                return false;
            auto const ticket =
                std::make_shared<SLE>(keylet::ticket(owner.id(), SeqProxy::rawTicket(ticketSeq)));
            ticket->setAccountID(sfAccount, owner.id());
            ticket->setFieldU32(sfTicketSequence, ticketSeq);
            ticket->setFieldU64(sfOwnerNode, 0);
            ac.view().insert(ticket);
            account->setFieldU32(
                sfTicketCount,
                account->isFieldPresent(sfTicketCount) ? account->getFieldU32(sfTicketCount) + 1
                                                       : 1);
            account->at(sfOwnerCount) += 1;
            ac.view().update(account);
            return true;
        };

        testcase("TransactionProposal immutable payload");
        doInvariantCheck(
            {{"TransactionProposal immutable fields changed"}},
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

        // With a sponsor, the entry is stamped and the owner's sponsored count
        // moves with the owner count; charging the sponsor is left to the caller.
        auto insertStagedProposal = [&](Account const& owner,
                                        ApplyContext& ac,
                                        bool updateOwnerCount,
                                        std::optional<AccountID> const& sponsor = std::nullopt) {
            if (!stagedPayload || !stagedProposalKeylet)
                return false;

            auto const sle = std::make_shared<SLE>(*stagedProposalKeylet);
            sle->setAccountID(sfOwner, owner.id());
            sle->setFieldObject(sfProposedTransaction, *stagedPayload);
            sle->setFieldU32(sfExpiration, 1);
            sle->setFieldU64(sfOwnerNode, 0);
            if (sponsor)
                sle->setAccountID(sfSponsor, *sponsor);
            ac.view().insert(sle);

            if (updateOwnerCount)
            {
                auto const account = ac.view().peek(keylet::account(owner.id()));
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
                auto const wrongSponsor = ac.view().peek(keylet::account(bystander.id()));
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

        // Everything the proposal should move does, and a bystander's owner
        // count moves too. Only a check for unexpected entries can catch that.
        testcase("TransactionProposal reserve moved on an unrelated account");
        doInvariantCheck(
            {{"TransactionProposal reserve accounting is inconsistent"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                if (!insertStagedProposal(owner, ac, true))
                    return false;
                auto const unrelated = ac.view().peek(keylet::account(bystander.id()));
                if (!unrelated)
                    return false;
                unrelated->at(sfOwnerCount) += 1;
                ac.view().update(unrelated);
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
                auto const ownerRoot = ac.view().peek(keylet::account(owner.id()));
                auto const sponsorRoot = ac.view().peek(keylet::account(sponsor.id()));
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

        // Reassigns the live proposal's reserve to a sponsor as a
        // SponsorshipTransfer would, with the counters kept consistent.
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

        // The staged payload, plus a spare Ticket of the owner's.
        Preclose const precloseStagedPayloadAndSpareTicket =
            [&](Account const& owner, Account const& destination, Env& env) {
                spareTicketSeq = proposal::createTicket(env, owner);
                return precloseStagedPayload(owner, destination, env);
            };

        // The live proposal, plus two spare Tickets of the owner's: the one
        // spareTicketSeq names and the next.
        Preclose const precloseLiveProposalAndTwoSpareTickets =
            [&](Account const& owner, Account const& destination, Env& env) {
                spareTicketSeq = proposal::createTicket(env, owner, 2);
                return precloseLiveProposal(owner, destination, env);
            };

        // Everything a Create or Cancel may do is modelled exactly, so each
        // case below stages one deviation from it. The Env is built here
        // rather than by the harness, so that a Cancel can name the live
        // proposal and either may pay with the spare Ticket: both exist only
        // once the preclose has run.
        struct ProposalTxOptions
        {
            // A2, the payment's destination, submits instead of A1. The live
            // proposal is A1's against A1's own Ticket, so A2 is neither its
            // Owner nor its target.
            bool stranger = false;
            // The transaction pays with A1's spare Ticket.
            bool paySpareTicket = false;
            // A Cancel names a proposal other than the live one.
            bool nameAnotherProposal = false;
            TER initialResult = tesSUCCESS;
        };
        auto const checkProposalTx = [&](TxType type,
                                         std::vector<std::string> const& expectLogs,
                                         Precheck const& precheck,
                                         std::initializer_list<TER> ters,
                                         Preclose const& preclose,
                                         ProposalTxOptions const& options = ProposalTxOptions(),
                                         std::source_location const& loc =
                                             std::source_location::current()) {
            Account const a1{"A1"};
            Account const a2{"A2"};
            Env env = makeEnv(testableAmendments());
            env.fund(XRP(1000), a1, a2);
            if (!BEAST_EXPECT(preclose(a1, a2, env)))
                return;
            bool const isCancel = type == ttTRANSACTION_PROPOSAL_CANCEL;
            if (isCancel && !BEAST_EXPECT(liveProposalKeylet))
                return;
            env.close();

            STTx const tx{type, [&](STObject& obj) {
                              obj.setAccountID(sfAccount, (options.stranger ? a2 : a1).id());
                              if (isCancel)
                              {
                                  auto proposalKey = liveProposalKeylet->key;
                                  if (options.nameAnotherProposal)
                                  {
                                      proposalKey =
                                          keylet::txProposal(a1.id(), *liveTicketSeq + 1).key;
                                  }
                                  obj.setFieldH256(sfProposalID, proposalKey);
                              }
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

        std::string const otherTicketMessage{
            "TransactionProposal transaction changed a Ticket other than the one it pays with"};

        // A Cancel that did not succeed deleted nothing.
        testcase("TransactionProposal failed cancel still deletes");
        {
            ProposalTxOptions options;
            options.initialResult = tecNO_PERMISSION;
            checkProposalTx(
                ttTRANSACTION_PROPOSAL_CANCEL,
                {{"TransactionProposal changes do not match transaction result"}},
                cancelDeletesLiveProposal,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLiveProposal,
                options);
        }

        // Nor did it release a reserve. No proposal changed here, so only an
        // exact comparison for the transaction type can catch the release.
        testcase("TransactionProposal failed cancel still releases the reserve");
        {
            ProposalTxOptions options;
            options.initialResult = tecNO_PERMISSION;
            checkProposalTx(
                ttTRANSACTION_PROPOSAL_CANCEL,
                {{"TransactionProposal reserve accounting is inconsistent"}},
                [&](Account const& owner, Account const&, ApplyContext& ac) {
                    auto const sle = peekLiveProposal(ac);
                    auto const account = ac.view().peek(keylet::account(owner.id()));
                    if (!sle || !account)
                        return false;
                    account->at(sfOwnerCount) -= xrpl::proposal::proposalOwnerCount(
                        sle->getFieldObject(sfProposedTransaction));
                    ac.view().update(account);
                    return true;
                },
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLiveProposal,
                options);
        }

        // A Cancel cannot spend a reserved Ticket, so the proposal it deletes
        // never goes with its Ticket.
        testcase("TransactionProposal cancel consumes the reserved ticket");
        checkProposalTx(
            ttTRANSACTION_PROPOSAL_CANCEL,
            {{"TransactionProposal changes do not match transaction result"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return liveTicketSeq && deleteTicket(owner, *liveTicketSeq, ac) &&
                    deleteLiveProposal(owner, ac);
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposal);

        // Nor any other Ticket it does not pay with: this Cancel spends a
        // Sequence, yet the Owner's spare Ticket went.
        testcase("TransactionProposal cancel consumes an unrelated ticket");
        checkProposalTx(
            ttTRANSACTION_PROPOSAL_CANCEL,
            {otherTicketMessage},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return spareTicketSeq && deleteTicket(owner, *spareTicketSeq, ac) &&
                    deleteLiveProposal(owner, ac);
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposalAndSpareTicket);

        // Nor mint one. The owner count moves with the Ticket, as it would
        // for a real one, so the Ticket itself is all that is left to catch.
        testcase("TransactionProposal cancel creates a ticket");
        checkProposalTx(
            ttTRANSACTION_PROPOSAL_CANCEL,
            {otherTicketMessage},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return liveTicketSeq && insertTicket(owner, *liveTicketSeq + 100, ac) &&
                    deleteLiveProposal(owner, ac);
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposal);

        // The proposal that went is not the one the Cancel names.
        testcase("TransactionProposal cancel deletes another proposal");
        {
            ProposalTxOptions options;
            options.nameAnotherProposal = true;
            checkProposalTx(
                ttTRANSACTION_PROPOSAL_CANCEL,
                {{"TransactionProposalCancel deleted a proposal other than the one it names"}},
                cancelDeletesLiveProposal,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLiveProposal,
                options);
        }

        // A live proposal may be cancelled only by its Owner or its target.
        testcase("TransactionProposal live proposal cancelled by a stranger");
        {
            ProposalTxOptions options;
            options.stranger = true;
            checkProposalTx(
                ttTRANSACTION_PROPOSAL_CANCEL,
                {{"TransactionProposalCancel of a live proposal by an unauthorized account"}},
                cancelDeletesLiveProposal,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLiveProposal,
                options);
        }

        // Paying with its own Ticket does not license a second one.
        testcase("TransactionProposal cancel paid with its own ticket also consumes another");
        {
            ProposalTxOptions options;
            options.paySpareTicket = true;
            checkProposalTx(
                ttTRANSACTION_PROPOSAL_CANCEL,
                {otherTicketMessage},
                [&](Account const& owner, Account const&, ApplyContext& ac) {
                    return spareTicketSeq && deleteTicket(owner, *spareTicketSeq, ac) &&
                        deleteTicket(owner, *spareTicketSeq + 1, ac) &&
                        deleteLiveProposal(owner, ac);
                },
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseLiveProposalAndTwoSpareTickets,
                options);
        }

        // The Create side of the same two rules: it too may consume only the
        // Ticket it pays with, and a failed one may move no reserve.
        testcase("TransactionProposal create consumes an unrelated ticket");
        checkProposalTx(
            ttTRANSACTION_PROPOSAL_CREATE,
            {otherTicketMessage},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                return spareTicketSeq && deleteTicket(owner, *spareTicketSeq, ac) &&
                    insertStagedProposal(owner, ac, true);
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseStagedPayloadAndSpareTicket);

        testcase("TransactionProposal failed create still charges the reserve");
        {
            ProposalTxOptions options;
            options.initialResult = tecNO_PERMISSION;
            checkProposalTx(
                ttTRANSACTION_PROPOSAL_CREATE,
                {{"TransactionProposal reserve accounting is inconsistent"}},
                [&](Account const& owner, Account const&, ApplyContext& ac) {
                    auto const account = ac.view().peek(keylet::account(owner.id()));
                    if (!stagedPayload || !account)
                        return false;
                    account->at(sfOwnerCount) += xrpl::proposal::proposalOwnerCount(*stagedPayload);
                    ac.view().update(account);
                    return true;
                },
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                precloseStagedPayload,
                options);
        }

        // And the third exactly reconciled transaction: a SponsorshipTransfer
        // reassigning a proposal is held to the same Ticket rule.
        testcase("TransactionProposal sponsorship transfer consumes an unrelated ticket");
        checkProposalTx(
            ttSPONSORSHIP_TRANSFER,
            {otherTicketMessage},
            [&](Account const& owner, Account const& sponsor, ApplyContext& ac) {
                return spareTicketSeq && deleteTicket(owner, *spareTicketSeq, ac) &&
                    reassignLiveProposal(owner, sponsor, ac);
            },
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseLiveProposalAndSpareTicket);

        // A SponsorshipTransfer may reassign the Sponsor and nothing else.
        testcase("TransactionProposal sponsorship transfer touches another field");
        doInvariantCheck(
            {{"TransactionProposal immutable fields changed"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = peekLiveProposal(ac);
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

        // A Cancel by the Owner deletes the proposal it names.
        testcase("TransactionProposal cancelled");
        checkProposalTx(
            ttTRANSACTION_PROPOSAL_CANCEL,
            {},
            cancelDeletesLiveProposal,
            passTers,
            precloseLiveProposal);

        // The one Ticket a Cancel may consume is the one it pays with.
        testcase("TransactionProposal cancel paid with the canceller's own ticket");
        {
            ProposalTxOptions options;
            options.paySpareTicket = true;
            checkProposalTx(
                ttTRANSACTION_PROPOSAL_CANCEL,
                {},
                [&](Account const& owner, Account const&, ApplyContext& ac) {
                    return spareTicketSeq && deleteTicket(owner, *spareTicketSeq, ac) &&
                        deleteLiveProposal(owner, ac);
                },
                passTers,
                precloseLiveProposalAndSpareTicket,
                options);
        }

        // Once the proposal is terminal anyone may delete it.
        testcase("TransactionProposal terminal proposal cancelled by a stranger");
        {
            ProposalTxOptions options;
            options.stranger = true;
            checkProposalTx(
                ttTRANSACTION_PROPOSAL_CANCEL,
                {},
                cancelDeletesLiveProposal,
                passTers,
                precloseTerminalProposal,
                options);
        }

        // A failed Cancel paid with a Ticket consumes that Ticket and nothing
        // else: the one owner-count move the exact comparison has to allow.
        testcase("TransactionProposal failed cancel paid with a ticket");
        {
            ProposalTxOptions options;
            options.paySpareTicket = true;
            options.initialResult = tecNO_PERMISSION;
            checkProposalTx(
                ttTRANSACTION_PROPOSAL_CANCEL,
                {},
                [&](Account const& owner, Account const&, ApplyContext& ac) {
                    return spareTicketSeq && deleteTicket(owner, *spareTicketSeq, ac);
                },
                {tecNO_PERMISSION, tecNO_PERMISSION},
                precloseLiveProposalAndSpareTicket,
                options);
        }

        // A SponsorshipTransfer paid with its own Ticket consumes that Ticket
        // and reassigns the proposal.
        testcase("TransactionProposal sponsorship transfer paid with its own ticket");
        {
            ProposalTxOptions options;
            options.paySpareTicket = true;
            checkProposalTx(
                ttSPONSORSHIP_TRANSFER,
                {},
                [&](Account const& owner, Account const& sponsor, ApplyContext& ac) {
                    return spareTicketSeq && deleteTicket(owner, *spareTicketSeq, ac) &&
                        reassignLiveProposal(owner, sponsor, ac);
                },
                passTers,
                precloseLiveProposalAndSpareTicket,
                options);
        }

        // A Create paid with the proposer's own Ticket consumes that Ticket
        // and inserts the proposal.
        testcase("TransactionProposal create paid with the proposer's own ticket");
        {
            ProposalTxOptions options;
            options.paySpareTicket = true;
            checkProposalTx(
                ttTRANSACTION_PROPOSAL_CREATE,
                {},
                [&](Account const& owner, Account const&, ApplyContext& ac) {
                    return spareTicketSeq && deleteTicket(owner, *spareTicketSeq, ac) &&
                        insertStagedProposal(owner, ac, true);
                },
                passTers,
                precloseStagedPayloadAndSpareTicket,
                options);
        }

        // The proposed transaction executing spends the proposal's Ticket and
        // takes the proposal with it, whatever else it does to owner counts:
        // here another account's count moved too, as the proposed
        // transaction's own effects may make it.
        testcase("TransactionProposal deleted with its ticket");
        doInvariantCheck(
            {},
            [&](Account const& owner, Account const& other, ApplyContext& ac) {
                if (!liveTicketSeq || !deleteTicket(owner, *liveTicketSeq, ac) ||
                    !deleteLiveProposal(owner, ac))
                    return false;
                auto const account = ac.view().peek(keylet::account(other.id()));
                if (!account)
                    return false;
                account->at(sfOwnerCount) += 1;
                ac.view().update(account);
                return true;
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
