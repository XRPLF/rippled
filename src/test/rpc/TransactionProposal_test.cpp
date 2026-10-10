#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/delegate.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/pay.h>
#include <test/jtx/proposal.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ticket.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

/**
 * The transaction_proposal RPC (XLS-0103 §8).
 *
 * TransactionProposalSign is not part of this branch, so collected signatures
 * are staged straight into the stored ProposedTransaction through the
 * open-ledger overlay, in the exact shape the spec stores them (§4.2.2). The
 * RPC does not re-verify signature cryptography (§8.1.3.2), so staged
 * material carries a placeholder TxnSignature; the one case that exercises
 * the full path, testSubmitCompleteProposal, collects real signatures and
 * submits the tx_blob the RPC hands back.
 */
struct TransactionProposal_test : public beast::unit_test::Suite
{
    // "signed" is a C++ keyword, so neither the handler nor this suite can
    // name it through the JSS macro.
    static inline json::StaticString const kSigned{"signed"};

    static Blob
    placeholderSignature()
    {
        return Blob{0xDE, 0xAD, 0xBE, 0xEF};
    }

    static json::Value
    query(jtx::Env& env, json::Value const& params)
    {
        return env.rpc("json", "transaction_proposal", to_string(params))[jss::result];
    }

    static json::Value
    byID(uint256 const& proposalID)
    {
        json::Value params{json::ValueType::Object};
        params[jss::proposal_id] = to_string(proposalID);
        return params;
    }

    static json::Value
    byTicket(jtx::Account const& target, std::uint32_t ticketSeq)
    {
        json::Value params{json::ValueType::Object};
        params[jss::account] = target.human();
        params[jss::ticket_seq] = ticketSeq;
        return params;
    }

    static uint256
    proposalID(jtx::Account const& target, std::uint32_t ticketSeq)
    {
        return keylet::txProposal(target.id(), ticketSeq).key;
    }

    /**
     * Create a proposal of @p payload and close the ledger.
     *
     * @return The proposal's ID.
     */
    uint256
    propose(
        jtx::Env& env,
        jtx::Account const& proposer,
        json::Value const& payload,
        std::optional<std::uint32_t> expiration = std::nullopt)
    {
        using namespace std::chrono_literals;

        env(jtx::proposal::create(
            proposer, payload, expiration.value_or(jtx::proposal::expiration(env, 1000s))));
        env.close();

        auto const target = parseBase58<AccountID>(payload[jss::Account].asString());
        if (!target.has_value())
        {
            BEAST_EXPECTS(false, "payload names no target account");
            return {};
        }
        auto const id =
            keylet::txProposal(*target, payload[sfTicketSequence.jsonName].asUInt()).key;
        BEAST_EXPECT(env.le(keylet::txProposal(id)));
        return id;
    }

    /**
     * Change the open ledger in place. The ledger is not closed afterwards:
     * closing rebuilds it from its transactions and drops the overlay, which
     * is also what makes every staged state start from a clean slate.
     */
    void
    modifyOpenLedger(jtx::Env& env, std::function<bool(OpenView&)> const& modify)
    {
        BEAST_EXPECT(env.app().getOpenLedger().modify(
            [&](OpenView& view, beast::Journal) { return modify(view); }));
    }

    /**
     * Stage signature material into a proposal's stored ProposedTransaction.
     *
     * Starts from the entry as last validated, so every staged state is
     * independent of the one before it within the same open ledger.
     */
    void
    stage(jtx::Env& env, uint256 const& id, std::function<void(STObject&)> const& mutate)
    {
        auto const pristine = env.closed()->read(keylet::txProposal(id));
        if (!BEAST_EXPECT(pristine))
            return;
        modifyOpenLedger(env, [&](OpenView& view) {
            if (!view.exists(keylet::txProposal(id)))
                return false;
            auto replacement = std::make_shared<SLE>(*pristine);
            STObject proposedTx = replacement->getFieldObject(sfProposedTransaction);
            mutate(proposedTx);
            replacement->setFieldObject(sfProposedTransaction, proposedTx);
            view.rawReplace(replacement);
            return true;
        });
    }

    // A single signature by @p key, at whichever level @p slot is.
    static void
    singleSign(STObject& slot, PublicKey const& key)
    {
        slot.setFieldVL(sfSigningPubKey, key.slice());
        slot.setFieldVL(sfTxnSignature, placeholderSignature());
    }

    /**
     * A nested signature slot carrying its InnerObjectFormats template, as
     * every nested object of a ledger entry read back from the ledger does:
     * an optional field it does not hold reads as empty rather than absent.
     */
    static STObject
    slot(SField const& name)
    {
        auto const* const format = InnerObjectFormats::getInstance().findSOTemplateBySField(name);
        if (format == nullptr)
            Throw<std::logic_error>("no inner object format for " + name.getName());
        return STObject{*format, name};
    }

    static STObject
    signerEntry(AccountID const& account, PublicKey const& key)
    {
        STObject signer = slot(sfSigner);
        signer.setAccountID(sfAccount, account);
        signer.setFieldVL(sfSigningPubKey, key.slice());
        signer.setFieldVL(sfTxnSignature, placeholderSignature());
        return signer;
    }

    using Share = std::pair<AccountID, PublicKey>;

    static Share
    share(jtx::Account const& account)
    {
        return {account.id(), account.pk()};
    }

    // Multi-signature shares, sorted by account as the ledger stores them.
    static STArray
    signersArray(std::vector<Share> shares)
    {
        std::ranges::sort(shares, {}, &Share::first);
        STArray signers{sfSigners};
        for (auto const& [account, key] : shares)
            signers.push_back(signerEntry(account, key));
        return signers;
    }

    static void
    multiSign(STObject& slot, std::vector<Share> const& shares)
    {
        slot.setFieldArray(sfSigners, signersArray(shares));
    }

    static STObject
    batchSigner(AccountID const& account)
    {
        STObject signer = slot(sfBatchSigner);
        signer.setAccountID(sfAccount, account);
        return signer;
    }

    // BatchSigners entries, sorted by account as the ledger stores them.
    static void
    setBatchSigners(STObject& batch, std::vector<STObject> entries)
    {
        // An entry built without an account sorts first.
        std::ranges::sort(entries, {}, [](STObject const& entry) {
            return entry.isFieldPresent(sfAccount) ? entry.getAccountID(sfAccount) : AccountID{};
        });
        STArray batchSigners{sfBatchSigners};
        for (auto& entry : entries)
            batchSigners.push_back(std::move(entry));
        batch.setFieldArray(sfBatchSigners, batchSigners);
    }

    static json::Value
    rowFor(json::Value const& jrr, jtx::Account const& account)
    {
        for (auto const& row : jrr[jss::signing_status])
        {
            if (row[jss::account] == account.human())
                return row;
        }
        return {};
    }

    // The row for a required co-signer that could not be named.
    static json::Value
    unnamedRow(json::Value const& jrr)
    {
        for (auto const& row : jrr[jss::signing_status])
        {
            if (!row.isMember(jss::account))
                return row;
        }
        return {};
    }

    static json::Value
    memberFor(json::Value const& row, jtx::Account const& account)
    {
        for (auto const& member : row[jss::signers])
        {
            if (member[jss::account] == account.human())
                return member;
        }
        return {};
    }

    // A signed row carries no reason; an unsigned one carries exactly the
    // expected reason.
    static bool
    rowIs(json::Value const& row, std::optional<std::string> const& reason)
    {
        if (!row.isObject() || !row[kSigned].isBool())
            return false;
        if (!reason)
            return row[kSigned].asBool() && !row.isMember(jss::reason);
        return !row[kSigned].asBool() && row[jss::reason] == *reason;
    }

    static bool
    noSignerListDetail(json::Value const& row)
    {
        return !row.isMember(jss::quorum) && !row.isMember(jss::signers) &&
            !row.isMember(jss::signed_weight);
    }

    // Rebuild the transaction the RPC hands back for submission.
    static std::optional<STTx>
    parseTxBlob(json::Value const& jrr)
    {
        if (!jrr.isMember(jss::tx_blob) || !jrr[jss::tx_blob].isString())
            return std::nullopt;
        auto const blob = strUnHex(jrr[jss::tx_blob].asString());
        if (!blob)
            return std::nullopt;
        SerialIter sit{makeSlice(*blob)};
        return STTx{sit};
    }

    void
    testDisabled(FeatureBitset features)
    {
        testcase("amendment disabled");

        using namespace jtx;

        // The method exists whatever the amendments; without Cosign no
        // proposal can exist, so every well-formed request finds nothing.
        Env env{*this, features - featureCosign};
        Account const target{"target"};
        env.fund(XRP(10000), target);
        env.close();

        BEAST_EXPECT(query(env, byTicket(target, 1))[jss::error] == "entryNotFound");
        BEAST_EXPECT(query(env, byID(proposalID(target, 1)))[jss::error] == "entryNotFound");
    }

    void
    testMalformedRequests(FeatureBitset features)
    {
        testcase("malformed requests");

        using namespace jtx;

        Env env{*this, features};
        Account const target{"target"};
        env.fund(XRP(10000), target);
        env.close();

        auto const expectError = [&](json::Value const& params, char const* error) {
            auto const jrr = query(env, params);
            BEAST_EXPECTS(jrr[jss::error] == error, to_string(params) + " -> " + to_string(jrr));
            // The error triple always agrees with itself.
            BEAST_EXPECT(jrr.isMember(jss::error_code) && jrr.isMember(jss::error_message));
            return jrr;
        };

        // Addressing: exactly one of proposal_id or {account, ticket_seq}.
        expectError(json::Value{json::ValueType::Object}, "invalidParams");
        {
            json::Value params = byID(proposalID(target, 1));
            params[jss::account] = target.human();
            params[jss::ticket_seq] = 1;
            expectError(params, "invalidParams");
        }
        {
            json::Value params = byID(proposalID(target, 1));
            params[jss::account] = target.human();
            expectError(params, "invalidParams");
        }
        {
            json::Value params{json::ValueType::Object};
            params[jss::account] = target.human();
            expectError(params, "invalidParams");
        }
        {
            json::Value params{json::ValueType::Object};
            params[jss::ticket_seq] = 1;
            expectError(params, "invalidParams");
        }

        // proposal_id must be a 256-bit hex string.
        for (auto const& bad :
             {json::Value{"not-hex"},
              json::Value{"ABCD"},
              json::Value{42},
              json::Value{json::ValueType::Object},
              json::Value{json::ValueType::Array}})
        {
            json::Value params{json::ValueType::Object};
            params[jss::proposal_id] = bad;
            expectError(params, "malformedRequest");
        }

        // account must be an account address; the types and codes are
        // ledger_entry's.
        for (auto const& bad :
             {json::Value{"rNotAnAccount!!!"},
              json::Value{42},
              json::Value{json::ValueType::Object},
              json::Value{json::ValueType::Array}})
        {
            json::Value params{json::ValueType::Object};
            params[jss::account] = bad;
            params[jss::ticket_seq] = 1;
            expectError(params, "malformedAddress");
        }
        {
            // Present but null reads as missing.
            json::Value params{json::ValueType::Object};
            params[jss::account] = json::Value{};
            params[jss::ticket_seq] = 1;
            expectError(params, "malformedRequest");
        }

        // ticket_seq must be a non-negative number or a numeric string.
        for (auto const& bad :
             {json::Value{"one"},
              json::Value{-1},
              json::Value{json::ValueType::Object},
              json::Value{json::ValueType::Array}})
        {
            json::Value params{json::ValueType::Object};
            params[jss::account] = target.human();
            params[jss::ticket_seq] = bad;
            expectError(params, "malformedRequest");
        }
        {
            // A numeric string is accepted; it then names no proposal.
            json::Value params{json::ValueType::Object};
            params[jss::account] = target.human();
            params[jss::ticket_seq] = "12";
            expectError(params, "entryNotFound");
        }

        // Well-formed addressing that names nothing: an unused ticket, an
        // all-zero index, and an index that is another entry type.
        expectError(byTicket(target, 1), "entryNotFound");
        expectError(byID(uint256{}), "entryNotFound");
        expectError(byID(keylet::account(target.id()).key), "entryNotFound");

        // The ledger must exist...
        {
            json::Value params = byTicket(target, 1);
            params[jss::ledger_index] = 1'000'000;
            expectError(params, "lgrNotFound");
        }
        // ...but the request is checked before it is looked up, so a
        // malformed request does not carry ledger fields.
        {
            json::Value params{json::ValueType::Object};
            params[jss::account] = 42;
            params[jss::ticket_seq] = 1;
            params[jss::ledger_index] = 1'000'000;
            auto const jrr = expectError(params, "malformedAddress");
            BEAST_EXPECT(!jrr.isMember(jss::ledger_index));
        }
    }

    void
    testPendingUnsigned(FeatureBitset features)
    {
        testcase("unsigned proposal is pending");

        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        // A target with no SignerList, proposing for itself.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env, target, proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq));

            auto const check = [&](json::Value const& jrr) {
                BEAST_EXPECT(jrr[jss::proposal_id] == to_string(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(!jrr.isMember(jss::tx_blob));

                // The raw entry, as ledger_entry would return it.
                auto const& entry = jrr[jss::proposal];
                BEAST_EXPECT(entry[sfLedgerEntryType.jsonName] == jss::TransactionProposal);
                BEAST_EXPECT(entry[sfOwner.jsonName] == target.human());
                BEAST_EXPECT(entry[jss::index] == to_string(id));
                BEAST_EXPECT(entry[sfProposedTransaction.jsonName][jss::Account] == target.human());

                // One required authorization: the target's own, with nothing
                // collected and no SignerList to report against.
                auto const& rows = jrr[jss::signing_status];
                BEAST_EXPECT(rows.isArray() && rows.size() == 1);
                BEAST_EXPECT(rows[0u][jss::account] == target.human());
                BEAST_EXPECT(rowIs(rows[0u], "inadequate_signatures"));
                BEAST_EXPECT(noSignerListDetail(rows[0u]));
            };

            check(query(env, byID(id)));
            check(query(env, byTicket(target, ticketSeq)));

            // The ledger fields come from the ledger lookup, as everywhere.
            {
                json::Value params = byID(id);
                params[jss::ledger_index] = "validated";
                auto const jrr = query(env, params);
                check(jrr);
                BEAST_EXPECT(jrr[jss::validated] == true);
                BEAST_EXPECT(jrr[jss::ledger_index].asUInt() == env.closed()->header().seq);
            }
        }

        // A distinct proposer, authorized by the target's SignerList: the
        // proposal is theirs, and the target's live quorum and roster are
        // reported although nothing has been collected.
        {
            proposal::authorizeProposer(env, target, alice);
            env(signers(target, 2, {{alice, 1}, {bob, 2}}));
            env.close();

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env, alice, proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq));

            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal][sfOwner.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");

            auto const row = rowFor(jrr, target);
            BEAST_EXPECT(rowIs(row, "inadequate_signatures"));
            BEAST_EXPECT(row[jss::quorum] == 2);
            BEAST_EXPECT(!row.isMember(jss::signed_weight));
            BEAST_EXPECT(row[jss::signers].size() == 2);
            BEAST_EXPECT(
                memberFor(row, alice)[jss::weight] == 1 && memberFor(row, alice)[kSigned] == false);
            BEAST_EXPECT(
                memberFor(row, bob)[jss::weight] == 2 && memberFor(row, bob)[kSigned] == false);
        }
    }

    void
    testSingleSign(FeatureBitset features)
    {
        testcase("single signature against the live account keys");

        using namespace jtx;

        Env env{*this, features};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const outsider{"outsider"};
        // Regular keys are never funded; only their keys are needed.
        Account const regular{"regular"};
        Account const regular2{"regular2"};
        env.fund(XRP(10000), target, bob, outsider);
        env.memoize(regular);
        env.memoize(regular2);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        auto const id = propose(
            env, target, proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq));

        auto const signWith = [&](PublicKey const& key) {
            stage(env, id, [&](STObject& tx) { singleSign(tx, key); });
            return query(env, byID(id));
        };

        // The master key completes the proposal, and the stored transaction
        // comes back in submit-ready binary form.
        {
            auto const jrr = signWith(target.pk());
            BEAST_EXPECT(jrr[jss::proposal_status] == "complete");
            BEAST_EXPECT(rowIs(rowFor(jrr, target), std::nullopt));
            BEAST_EXPECT(noSignerListDetail(rowFor(jrr, target)));

            auto const tx = parseTxBlob(jrr);
            BEAST_EXPECT(tx.has_value());
            if (tx.has_value())
            {
                BEAST_EXPECT(tx->getAccountID(sfAccount) == target.id());
                BEAST_EXPECT(tx->getFieldU32(sfTicketSequence) == ticketSeq);
                BEAST_EXPECT(makeSlice(tx->getFieldVL(sfSigningPubKey)) == target.pk().slice());
                BEAST_EXPECT(tx->getFieldVL(sfTxnSignature) == placeholderSignature());
                // Byte for byte the stored transaction.
                auto const stored = env.le(keylet::txProposal(id))
                                        ->getFieldObject(sfProposedTransaction)
                                        .getSerializer();
                BEAST_EXPECT(tx->getSerializer() == stored);
            }
        }

        // A key that belongs to nobody the account trusts.
        {
            auto const jrr = signWith(outsider.pk());
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, target), "not_authorized"));
            BEAST_EXPECT(!jrr.isMember(jss::tx_blob));
        }

        // Once a regular key is set, either key authorizes...
        env(regkey(target, regular));
        env.close();
        BEAST_EXPECT(signWith(regular.pk())[jss::proposal_status] == "complete");
        BEAST_EXPECT(signWith(target.pk())[jss::proposal_status] == "complete");

        // ...until the master key is disabled: a master signature collected
        // earlier no longer authorizes, because authorization is re-read
        // from the live ledger.
        env(fset(target, asfDisableMaster), Sig(target));
        env.close();
        {
            auto const jrr = signWith(target.pk());
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, target), "master_disabled"));
        }
        BEAST_EXPECT(signWith(regular.pk())[jss::proposal_status] == "complete");

        // Rotating the regular key strands a signature by the old one.
        env(regkey(target, regular2), Sig(regular));
        env.close();
        {
            auto const jrr = signWith(regular.pk());
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, target), "not_authorized"));
        }
        BEAST_EXPECT(signWith(regular2.pk())[jss::proposal_status] == "complete");
    }

    void
    testMultiSign(FeatureBitset features)
    {
        testcase("multi-signature shares against the live SignerList");

        using namespace jtx;

        Env env{*this, features};
        Account const target{"target"};
        Account const dest{"dest"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const outsider{"outsider"};
        Account const regular{"regular"};
        Account const phantom{"phantom"};  // on a SignerList, never funded
        env.fund(XRP(10000), target, dest, bob, carol, dave, outsider);
        env.memoize(regular);
        env.memoize(phantom);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        auto const id = propose(
            env, target, proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq));

        auto const signWith = [&](std::vector<Share> const& shares) {
            stage(env, id, [&](STObject& tx) { multiSign(tx, shares); });
            return query(env, byID(id));
        };

        // Shares collected for an account with no SignerList authorize
        // nothing; there is no quorum or roster to report, and no share
        // carries weight.
        {
            auto const jrr = signWith({share(bob), share(carol)});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            auto const row = rowFor(jrr, target);
            BEAST_EXPECT(rowIs(row, "no_signer_list"));
            BEAST_EXPECT(noSignerListDetail(row));
        }

        env(signers(target, 2, {{bob, 1}, {carol, 1}, {dave, 1}}));
        env.close();

        // One of three: progress is reported, and the roster says who is
        // left to chase.
        {
            auto const jrr = signWith({share(bob)});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(!jrr.isMember(jss::tx_blob));
            auto const row = rowFor(jrr, target);
            BEAST_EXPECT(rowIs(row, "inadequate_signatures"));
            BEAST_EXPECT(row[jss::signed_weight] == 1);
            BEAST_EXPECT(row[jss::quorum] == 2);
            BEAST_EXPECT(row[jss::signers].size() == 3);
            BEAST_EXPECT(memberFor(row, bob)[kSigned] == true);
            BEAST_EXPECT(memberFor(row, carol)[kSigned] == false);
            BEAST_EXPECT(memberFor(row, dave)[kSigned] == false);
        }

        // Two of three: quorum met.
        {
            auto const jrr = signWith({share(bob), share(carol)});
            BEAST_EXPECT(jrr[jss::proposal_status] == "complete");
            auto const row = rowFor(jrr, target);
            BEAST_EXPECT(rowIs(row, std::nullopt));
            BEAST_EXPECT(row[jss::signed_weight] == 2);
            auto const tx = parseTxBlob(jrr);
            BEAST_EXPECT(tx && tx->getFieldArray(sfSigners).size() == 2);
        }

        // A share from an account the list does not name voids the whole
        // set, as submission would reject it, even though the weight of the
        // shares it does name meets the quorum.
        {
            auto const jrr = signWith({share(bob), share(carol), share(outsider)});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            auto const row = rowFor(jrr, target);
            BEAST_EXPECT(rowIs(row, "invalid_signer_set"));
            BEAST_EXPECT(row[jss::signed_weight] == 2);
            BEAST_EXPECT(row[jss::signers].size() == 3);
            BEAST_EXPECT(memberFor(row, outsider).isNull());
        }

        // A member may sign with its regular key.
        env(regkey(bob, regular));
        env.close();
        {
            auto const jrr = signWith({{bob.id(), regular.pk()}, share(carol)});
            BEAST_EXPECT(jrr[jss::proposal_status] == "complete");
            BEAST_EXPECT(rowFor(jrr, target)[jss::signed_weight] == 2);
            BEAST_EXPECT(memberFor(rowFor(jrr, target), bob)[kSigned] == true);
        }

        // A member's disabled master key voids its share and the set with it.
        env(fset(bob, asfDisableMaster), Sig(bob));
        env.close();
        {
            auto const jrr = signWith({share(bob), share(carol)});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            auto const row = rowFor(jrr, target);
            BEAST_EXPECT(rowIs(row, "master_disabled"));
            BEAST_EXPECT(row[jss::signed_weight] == 1);
            BEAST_EXPECT(memberFor(row, bob)[kSigned] == false);
            BEAST_EXPECT(memberFor(row, carol)[kSigned] == true);
        }

        // So does a key that is neither the member's master nor regular key.
        {
            auto const jrr = signWith({{bob.id(), outsider.pk()}, share(carol)});
            BEAST_EXPECT(rowIs(rowFor(jrr, target), "invalid_signer_set"));
            BEAST_EXPECT(rowFor(jrr, target)[jss::signed_weight] == 1);
        }

        // The list changed after the shares were collected: a share from a
        // member since removed voids the set, and the roster is the live one.
        env(signers(target, 2, {{bob, 1}, {dave, 1}}));
        env.close();
        {
            auto const jrr = signWith({{bob.id(), regular.pk()}, share(carol)});
            auto const row = rowFor(jrr, target);
            BEAST_EXPECT(rowIs(row, "invalid_signer_set"));
            BEAST_EXPECT(row[jss::signed_weight] == 1);
            BEAST_EXPECT(row[jss::signers].size() == 2);
            BEAST_EXPECT(memberFor(row, bob)[kSigned] == true);
            BEAST_EXPECT(memberFor(row, dave)[kSigned] == false);
            BEAST_EXPECT(memberFor(row, carol).isNull());
        }

        // A member absent from the ledger may sign with its own master key.
        env(signers(target, 1, {{phantom, 1}}));
        env.close();
        {
            auto const jrr = signWith({share(phantom)});
            BEAST_EXPECT(jrr[jss::proposal_status] == "complete");
            BEAST_EXPECT(rowFor(jrr, target)[jss::signed_weight] == 1);
            BEAST_EXPECT(memberFor(rowFor(jrr, target), phantom)[kSigned] == true);
        }

        // An account with a SignerList may still single-sign; the roster is
        // reported, with no weight because no shares were collected.
        {
            stage(env, id, [&](STObject& tx) { singleSign(tx, target.pk()); });
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "complete");
            auto const row = rowFor(jrr, target);
            BEAST_EXPECT(rowIs(row, std::nullopt));
            BEAST_EXPECT(row[jss::quorum] == 1 && row[jss::signers].size() == 1);
            BEAST_EXPECT(!row.isMember(jss::signed_weight));
        }
    }

    void
    testMalformedMaterial(FeatureBitset features)
    {
        testcase("signature material the Sign transaction never produces");

        using namespace jtx;

        Env env{*this, features};
        Account const target{"target"};
        Account const dest{"dest"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10000), target, dest, bob, carol);
        env.close();

        env(signers(target, 2, {{bob, 1}, {carol, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        auto const id = propose(
            env, target, proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq));

        auto const expectMalformed = [&](std::function<void(STObject&)> const& mutate) {
            stage(env, id, mutate);
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            auto const row = rowFor(jrr, target);
            BEAST_EXPECTS(rowIs(row, "malformed"), to_string(row));
            // Shapes that cannot be evaluated report no weight.
            BEAST_EXPECT(!row.isMember(jss::signed_weight));
        };

        Blob const notAKey{0x01, 0x02, 0x03};

        // A single signature missing one half, or with a key of no known type.
        expectMalformed([&](STObject& tx) { tx.setFieldVL(sfSigningPubKey, target.pk().slice()); });
        expectMalformed(
            [&](STObject& tx) { tx.setFieldVL(sfTxnSignature, placeholderSignature()); });
        expectMalformed([&](STObject& tx) {
            tx.setFieldVL(sfSigningPubKey, notAKey);
            tx.setFieldVL(sfTxnSignature, placeholderSignature());
        });

        // Shares alongside a single signature, or with a filled SigningPubKey.
        expectMalformed([&](STObject& tx) {
            multiSign(tx, {share(bob)});
            tx.setFieldVL(sfTxnSignature, placeholderSignature());
        });
        expectMalformed([&](STObject& tx) {
            multiSign(tx, {share(bob)});
            tx.setFieldVL(sfSigningPubKey, target.pk().slice());
        });

        // An empty Signers array.
        expectMalformed([&](STObject& tx) { tx.setFieldArray(sfSigners, STArray{sfSigners}); });

        // A share that is not a usable signer entry: no account, no key, no
        // signature, an empty key, or a key of no known type.
        auto const withShare = [&](std::function<void(STObject&)> const& damage) {
            return [&, damage](STObject& tx) {
                STObject signer = signerEntry(bob.id(), bob.pk());
                damage(signer);
                STArray signers{sfSigners};
                signers.push_back(signer);
                tx.setFieldArray(sfSigners, signers);
            };
        };
        expectMalformed(withShare([](STObject& signer) { signer.makeFieldAbsent(sfAccount); }));
        expectMalformed(
            withShare([](STObject& signer) { signer.makeFieldAbsent(sfSigningPubKey); }));
        expectMalformed(
            withShare([](STObject& signer) { signer.makeFieldAbsent(sfTxnSignature); }));
        expectMalformed(
            withShare([](STObject& signer) { signer.setFieldVL(sfSigningPubKey, Blob{}); }));
        expectMalformed(
            withShare([&](STObject& signer) { signer.setFieldVL(sfSigningPubKey, notAKey); }));

        // Well-formed shares against a SignerList that does not deserialize:
        // nothing about the list is reported, and the verdict fails the way
        // submission would.
        {
            modifyOpenLedger(env, [&](OpenView& view) {
                auto const sle = view.read(keylet::signerList(target.id()));
                if (!sle)
                    return false;
                auto replacement = std::make_shared<SLE>(*sle);
                STArray badEntries;
                badEntries.push_back(STObject{sfSignerEntry});  // no sfAccount
                replacement->setFieldArray(sfSignerEntries, badEntries);
                view.rawReplace(replacement);
                return true;
            });
            stage(env, id, [&](STObject& tx) { multiSign(tx, {share(bob), share(carol)}); });

            auto const jrr = query(env, byID(id));
            auto const row = rowFor(jrr, target);
            BEAST_EXPECTS(rowIs(row, "malformed"), to_string(row));
            BEAST_EXPECT(noSignerListDetail(row));
        }
    }

    void
    testDelegate(FeatureBitset features)
    {
        testcase("delegated proposed transaction");

        using namespace jtx;

        Env env{*this, features};
        Account const target{"target"};
        Account const delegateAcct{"delegateAcct"};
        Account const dest{"dest"};
        Account const ds{"ds"};  // on the delegate's SignerList
        env.fund(XRP(10000), target, delegateAcct, dest, ds);
        env.close();

        env(delegate::set(target, delegateAcct, {"Payment"}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        json::Value payload = pay(target, dest, XRP(1));
        payload[sfDelegate.jsonName] = delegateAcct.human();
        auto const id =
            propose(env, delegateAcct, proposal::unsignedPayload(env, payload, ticketSeq));

        // The delegate, not the target, is the required signer.
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::signing_status].size() == 1);
            BEAST_EXPECT(rowIs(rowFor(jrr, delegateAcct), "inadequate_signatures"));
            BEAST_EXPECT(rowFor(jrr, target).isNull());
        }

        // The delegate's own signature satisfies it; the target's does not.
        stage(env, id, [&](STObject& tx) { singleSign(tx, delegateAcct.pk()); });
        BEAST_EXPECT(query(env, byID(id))[jss::proposal_status] == "complete");

        stage(env, id, [&](STObject& tx) { singleSign(tx, target.pk()); });
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, delegateAcct), "not_authorized"));
        }

        // And it is the delegate's SignerList that shares are scored against.
        env(signers(delegateAcct, 1, {{ds, 1}}));
        env.close();
        stage(env, id, [&](STObject& tx) { multiSign(tx, {share(ds)}); });
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "complete");
            auto const row = rowFor(jrr, delegateAcct);
            BEAST_EXPECT(row[jss::quorum] == 1 && row[jss::signed_weight] == 1);
            BEAST_EXPECT(memberFor(row, ds)[kSigned] == true);
        }

        // Once the target revokes the grant the delegate cannot act, however
        // it signed; submission would answer terNO_DELEGATE_PERMISSION.
        env(delegate::set(target, delegateAcct, {}));
        env.close();
        stage(env, id, [&](STObject& tx) { singleSign(tx, delegateAcct.pk()); });
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(!jrr.isMember(jss::tx_blob));
            BEAST_EXPECT(rowIs(rowFor(jrr, delegateAcct), "no_delegate_permission"));
        }
        // A grant for another transaction type does not cover this one.
        env(delegate::set(target, delegateAcct, {"TrustSet"}));
        env.close();
        stage(env, id, [&](STObject& tx) { singleSign(tx, delegateAcct.pk()); });
        BEAST_EXPECT(rowIs(rowFor(query(env, byID(id)), delegateAcct), "no_delegate_permission"));

        // A stored payload that cannot be rebuilt as a transaction cannot be
        // checked for permission at all.
        stage(env, id, [&](STObject& tx) {
            singleSign(tx, delegateAcct.pk());
            tx.setFieldU16(sfTransactionType, 9999);
        });
        BEAST_EXPECT(rowIs(rowFor(query(env, byID(id)), delegateAcct), "malformed"));

        // A grant of only granular permissions is judged by the transaction
        // type's own semantics, as submission does: PaymentMint does not cover
        // a payment of XRP.
        {
            Account const gw{"gw"};
            Account const minter{"minter"};
            env.fund(XRP(10000), gw, minter);
            env.close();
            env(delegate::set(gw, minter, {"PaymentMint"}));
            env.close();

            std::uint32_t const gwTicketSeq = proposal::createTicket(env, gw);
            json::Value xrpPayment = pay(gw, dest, XRP(1));
            xrpPayment[sfDelegate.jsonName] = minter.human();
            auto const gwId =
                propose(env, gw, proposal::unsignedPayload(env, xrpPayment, gwTicketSeq));

            stage(env, gwId, [&](STObject& tx) { singleSign(tx, minter.pk()); });
            {
                auto const jrr = query(env, byID(gwId));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(rowIs(rowFor(jrr, minter), "no_delegate_permission"));
            }

            env(delegate::set(gw, minter, {"Payment"}));
            env.close();
            stage(env, gwId, [&](STObject& tx) { singleSign(tx, minter.pk()); });
            BEAST_EXPECT(query(env, byID(gwId))[jss::proposal_status] == "complete");
        }
    }

    // The rules each inner transaction's own preclaim applies at submission:
    // its Delegate must hold the grant, and its sponsorship must be one
    // submission accepts, whether the Sponsor co-signs or not.
    void
    testBatchInnerRules(FeatureBitset features)
    {
        testcase("proposed Batch inner delegation and sponsorship");

        using namespace jtx;

        Env env{*this, features};
        Account const target{"target"};  // the outer account
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const patron{"patron"};
        Account const ghost{"ghost"};  // never funded
        // A proposed Batch holds ten owner-reserve increments, and this case
        // creates a couple of dozen of them against the target.
        env.fund(XRP(100000), target, bob, carol, patron);
        env.memoize(ghost);
        env.close();

        auto const propose2 = [&](json::Value const& inner) {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            return propose(
                env,
                target,
                proposal::unsignedBatch(
                    env,
                    target,
                    ticketSeq,
                    tfAllOrNothing,
                    {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target) + 1),
                     proposal::innerTx(inner, env.seq(carol))}));
        };
        auto const signAll = [&](uint256 const& id, std::vector<Account> const& participants) {
            stage(env, id, [&](STObject& tx) {
                singleSign(tx, target.pk());
                std::vector<STObject> entries;
                for (auto const& participant : participants)
                {
                    STObject entry = batchSigner(participant.id());
                    singleSign(entry, participant.pk());
                    entries.push_back(std::move(entry));
                }
                setBatchSigners(tx, std::move(entries));
            });
            return query(env, byID(id));
        };

        // An inner Delegate without carol's grant: the participant is bob, and
        // his row fails on permission however he signed. The grant cures it.
        {
            json::Value delegated = pay(carol, target, XRP(1));
            delegated[sfDelegate.jsonName] = bob.human();
            auto const id = propose2(delegated);

            auto const jrr = signAll(id, {bob});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, bob), "no_delegate_permission"));
            BEAST_EXPECT(rowFor(jrr, carol).isNull());

            // The grant cures it. Carol's DelegateSet moved her Sequence past
            // the inner's, so a fresh proposal carries the inner.
            env(delegate::set(carol, bob, {"Payment"}));
            env.close();
            BEAST_EXPECT(signAll(propose2(delegated), {bob})[jss::proposal_status] == "complete");
        }

        // A co-signing inner Sponsor on a delegated, reserve-sponsored inner:
        // the sponsorship rule rejects the inner outright, so the sponsor's
        // participant row cannot be satisfied by its signature.
        {
            json::Value inner = pay(carol, target, XRP(1));
            inner[sfDelegate.jsonName] = bob.human();
            inner[sfSponsor.jsonName] = patron.human();
            inner[sfSponsorFlags.jsonName] = spfSponsorReserve;
            inner[sfSponsorSignature.jsonName] = json::Value{json::ValueType::Object};
            auto const id = propose2(inner);

            auto const jrr = signAll(id, {bob, patron});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(!jrr.isMember(jss::tx_blob));
            BEAST_EXPECT(rowIs(rowFor(jrr, bob), std::nullopt));
            BEAST_EXPECT(rowIs(rowFor(jrr, patron), "invalid_sponsorship"));
        }

        // A co-signing inner Sponsor that does not exist cannot act, unless an
        // earlier inner creates it: then its inner is judged on a view where
        // it exists, and its own master key authorizes its entry.
        {
            json::Value inner = pay(carol, target, XRP(1));
            inner[sfSponsor.jsonName] = ghost.human();
            inner[sfSponsorFlags.jsonName] = spfSponsorReserve;
            inner[sfSponsorSignature.jsonName] = json::Value{json::ValueType::Object};
            auto const id = propose2(inner);

            auto const jrr = signAll(id, {carol, ghost});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, carol), std::nullopt));
            BEAST_EXPECT(rowIs(rowFor(jrr, ghost), "account_not_found"));

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const created = propose(
                env,
                target,
                proposal::unsignedBatch(
                    env,
                    target,
                    ticketSeq,
                    tfAllOrNothing,
                    {proposal::innerTx(pay(target, ghost, XRP(1000)), env.seq(target) + 1),
                     proposal::innerTx(inner, env.seq(carol))}));
            auto const jrr2 = signAll(created, {carol, ghost});
            BEAST_EXPECT(jrr2[jss::proposal_status] == "complete");
            BEAST_EXPECT(rowIs(rowFor(jrr2, ghost), std::nullopt));
        }

        // An inner delegated to the outer account itself needs no BatchSigners
        // entry (the outer signature covers it), so a failed grant lands on
        // the outer account's own row.
        {
            json::Value inner = pay(carol, target, XRP(1));
            inner[sfDelegate.jsonName] = target.human();
            auto const id = propose2(inner);

            auto const jrr = signAll(id, {});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(jrr[jss::signing_status].size() == 1);
            BEAST_EXPECT(rowIs(rowFor(jrr, target), "no_delegate_permission"));

            env(delegate::set(carol, target, {"Payment"}));
            env.close();
            BEAST_EXPECT(signAll(propose2(inner), {})[jss::proposal_status] == "complete");
        }

        // The gates each inner's preclaim applies before any signature rule.
        // A Sequence its account has moved past, a Ticket that does not
        // exist, a LastLedgerSequence that has passed, an AccountTxnID that
        // does not match: each leaves the inner unable to take its turn, so
        // its authorizer's row cannot be satisfied however it signed.
        {
            auto const id = propose2(pay(carol, target, XRP(1)));
            BEAST_EXPECT(signAll(id, {carol})[jss::proposal_status] == "complete");

            // Any transaction of carol's after the proposal moves her
            // Sequence past the inner's.
            env(pay(carol, bob, XRP(1)));
            env.close();
            {
                auto const jrr = signAll(id, {carol});
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(!jrr.isMember(jss::tx_blob));
                BEAST_EXPECT(rowIs(rowFor(jrr, carol), "sequence_mismatch"));
            }

            auto const withInnerField = [&](std::function<void(STObject&)> const& mutate) {
                auto const fresh = propose2(pay(carol, target, XRP(1)));
                stage(env, fresh, [&](STObject& tx) {
                    singleSign(tx, target.pk());
                    STObject entry = batchSigner(carol.id());
                    singleSign(entry, carol.pk());
                    setBatchSigners(tx, {entry});
                    mutate(tx.peekFieldArray(sfRawTransactions)[1]);
                });
                auto const jrr = query(env, byID(fresh));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(rowIs(rowFor(jrr, carol), "sequence_mismatch"));
            };
            withInnerField([](STObject& inner) { inner.setFieldU32(sfLastLedgerSequence, 1); });
            withInnerField([](STObject& inner) {
                inner.setFieldU32(sfSequence, 0);
                inner.setFieldU32(sfTicketSequence, 999999);
            });
            withInnerField([](STObject& inner) { inner.setFieldH256(sfAccountTxnID, uint256{1}); });
        }

        // Tickets this Batch itself spends before the inner runs: the outer
        // transaction's own, and one an earlier inner of the same account
        // takes. The second case needs tfIndependent; preflight already
        // rejects the duplicate under tfAllOrNothing.
        {
            std::uint32_t const outerTicketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                proposal::unsignedBatch(
                    env,
                    target,
                    outerTicketSeq,
                    tfAllOrNothing,
                    {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target) + 1),
                     batch::Inner{pay(target, bob, XRP(2)), 0, outerTicketSeq}.getTxn()}));
            auto const jrr = signAll(id, {});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, target), "sequence_mismatch"));
        }
        {
            std::uint32_t const carolTicketSeq = proposal::createTicket(env, carol);
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                proposal::unsignedBatch(
                    env,
                    target,
                    ticketSeq,
                    tfIndependent,
                    {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target) + 1),
                     batch::Inner{pay(carol, target, XRP(1)), 0, carolTicketSeq}.getTxn(),
                     batch::Inner{pay(carol, target, XRP(2)), 0, carolTicketSeq}.getTxn()}));
            auto const jrr = signAll(id, {carol});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, carol), "sequence_mismatch"));
        }

        // A Ticket another live proposal reserves is held for that proposal's
        // own transaction, so the inner cannot spend it until that proposal
        // is cancelled; the remedy differs, so the reason does too.
        {
            std::uint32_t const bobTicketSeq = proposal::createTicket(env, bob);
            auto const other = propose(
                env, bob, proposal::unsignedPayload(env, pay(bob, carol, XRP(1)), bobTicketSeq));
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                proposal::unsignedBatch(
                    env,
                    target,
                    ticketSeq,
                    tfAllOrNothing,
                    {proposal::innerTx(pay(target, carol, XRP(1)), env.seq(target) + 1),
                     batch::Inner{pay(bob, target, XRP(1)), 0, bobTicketSeq}.getTxn()}));
            {
                auto const jrr = signAll(id, {bob});
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(!jrr.isMember(jss::tx_blob));
                BEAST_EXPECT(rowIs(rowFor(jrr, bob), "ticket_reserved"));
            }
            env(proposal::cancel(bob, other));
            env.close();
            BEAST_EXPECT(signAll(id, {bob})[jss::proposal_status] == "complete");
        }

        // Precedence across inners: once an authorizer's row reports a turn
        // gate, a later inner's grant verdict does not replace it, since the
        // Batch fails on the earlier inner first.
        {
            Account const erin{"erin"};
            env.fund(XRP(10000), erin);
            env.close();
            json::Value first = pay(carol, target, XRP(1));
            first[sfDelegate.jsonName] = bob.human();  // carol's grant to bob exists
            json::Value second = pay(erin, target, XRP(1));
            second[sfDelegate.jsonName] = bob.human();  // erin never granted bob anything
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                proposal::unsignedBatch(
                    env,
                    target,
                    ticketSeq,
                    tfAllOrNothing,
                    {proposal::innerTx(first, env.seq(carol)),
                     proposal::innerTx(second, env.seq(erin))}));
            env(pay(carol, bob, XRP(1)));  // moves carol's Sequence past the first inner's
            env.close();
            auto const jrr = signAll(id, {bob});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, bob), "sequence_mismatch"));
        }

        // AccountTxnID is judged only for an account's first transaction in
        // the Batch: a later one would have to name the earlier one's hash.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                proposal::unsignedBatch(
                    env,
                    target,
                    ticketSeq,
                    tfAllOrNothing,
                    {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target) + 1),
                     proposal::innerTx(pay(carol, target, XRP(1)), env.seq(carol)),
                     proposal::innerTx(pay(carol, target, XRP(2)), env.seq(carol) + 1)}));
            stage(env, id, [&](STObject& tx) {
                singleSign(tx, target.pk());
                STObject entry = batchSigner(carol.id());
                singleSign(entry, carol.pk());
                setBatchSigners(tx, {entry});
                tx.peekFieldArray(sfRawTransactions)[2].setFieldH256(sfAccountTxnID, uint256{1});
            });
            BEAST_EXPECT(query(env, byID(id))[jss::proposal_status] == "complete");
        }

        // A TicketCreate inner consumes its Sequence and then advances the
        // account by its TicketCount, so the next inner of that account takes
        // the Sequence after the Tickets, not the next one; and the Tickets it
        // creates may be spent by later inners of the same Batch.
        {
            auto const proposeAfterTicketCreate = [&](std::uint32_t paymentSeq) {
                std::uint32_t const ticketSeq = proposal::createTicket(env, target);
                std::uint32_t const next = env.seq(target) + 1;
                return propose(
                    env,
                    target,
                    proposal::unsignedBatch(
                        env,
                        target,
                        ticketSeq,
                        tfAllOrNothing,
                        {proposal::innerTx(ticket::create(target, 2), next),
                         proposal::innerTx(pay(target, bob, XRP(1)), next + paymentSeq)}));
            };
            // Naively the next Sequence; in fact two Tickets lie in between.
            {
                auto const jrr = signAll(proposeAfterTicketCreate(1), {});
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(rowIs(rowFor(jrr, target), "sequence_mismatch"));
            }
            BEAST_EXPECT(
                signAll(proposeAfterTicketCreate(3), {})[jss::proposal_status] == "complete");
        }
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            std::uint32_t const next = env.seq(target) + 1;
            // The Sequence-based TicketCreate at `next` creates Ticket next + 1.
            auto const id = propose(
                env,
                target,
                proposal::unsignedBatch(
                    env,
                    target,
                    ticketSeq,
                    tfAllOrNothing,
                    {proposal::innerTx(ticket::create(target, 1), next),
                     batch::Inner{pay(target, bob, XRP(1)), 0, next + 1}.getTxn()}));
            BEAST_EXPECT(signAll(id, {})[jss::proposal_status] == "complete");
        }
        {
            // A Ticket-based TicketCreate creates Tickets numbered from the
            // Sequence the account holds, without consuming one.
            std::uint32_t const ticketSeq = proposal::createTicket(env, target, 2);
            std::uint32_t const spareTicketSeq = ticketSeq + 1;
            std::uint32_t const next = env.seq(target) + 1;
            auto const id = propose(
                env,
                target,
                proposal::unsignedBatch(
                    env,
                    target,
                    ticketSeq,
                    tfAllOrNothing,
                    {batch::Inner{ticket::create(target, 1), 0, spareTicketSeq}.getTxn(),
                     batch::Inner{pay(target, bob, XRP(1)), 0, next}.getTxn(),
                     proposal::innerTx(pay(target, bob, XRP(2)), next + 1)}));
            BEAST_EXPECT(signAll(id, {})[jss::proposal_status] == "complete");
        }

        // Two inners of one account take consecutive Sequences; an inner on
        // a Ticket that exists takes its turn too.
        {
            std::uint32_t const carolTicketSeq = proposal::createTicket(env, carol);
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                proposal::unsignedBatch(
                    env,
                    target,
                    ticketSeq,
                    tfAllOrNothing,
                    {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target) + 1),
                     proposal::innerTx(pay(carol, target, XRP(1)), env.seq(carol)),
                     proposal::innerTx(pay(carol, target, XRP(2)), env.seq(carol) + 1),
                     batch::Inner{pay(carol, target, XRP(3)), 0, carolTicketSeq}.getTxn()}));
            BEAST_EXPECT(signAll(id, {carol})[jss::proposal_status] == "complete");
        }

        // An inner that cannot be rebuilt as a transaction cannot be judged by
        // either rule.
        {
            json::Value inner = pay(carol, target, XRP(1));
            inner[sfDelegate.jsonName] = bob.human();
            inner[sfSponsor.jsonName] = patron.human();
            inner[sfSponsorFlags.jsonName] = spfSponsorReserve;
            inner[sfSponsorSignature.jsonName] = json::Value{json::ValueType::Object};
            auto const id = propose2(inner);

            stage(env, id, [&](STObject& tx) {
                singleSign(tx, target.pk());
                tx.peekFieldArray(sfRawTransactions)[1].setFieldU16(sfTransactionType, 9999);
            });
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, bob), "malformed"));
            BEAST_EXPECT(rowIs(rowFor(jrr, patron), "malformed"));
        }

        // A Sponsor that does not co-sign on a delegated, reserve-sponsored
        // inner is rejected by the same rule, on a row of its own.
        {
            json::Value inner = pay(carol, target, XRP(1));
            inner[sfDelegate.jsonName] = bob.human();
            inner[sfSponsor.jsonName] = patron.human();
            inner[sfSponsorFlags.jsonName] = spfSponsorReserve;
            auto const id = propose2(inner);

            auto const jrr = signAll(id, {bob});
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, bob), std::nullopt));
            BEAST_EXPECT(rowIs(rowFor(jrr, patron), "invalid_sponsorship"));
        }
    }

    void
    testCounterparty(FeatureBitset features)
    {
        testcase("counterparty co-signature");

        using namespace jtx;

        Env env{*this, features};
        Account const borrower{"borrower"};
        Account const lender{"lender"};
        Account const lenderSigner{"lenderSigner"};
        Account const bystander{"bystander"};
        Account const ghost{"ghost"};  // never funded
        env.fund(XRP(10000), borrower, lender, lenderSigner, bystander);
        env.memoize(ghost);
        env.close();

        // An explicit Counterparty is a required signer in its own right.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, borrower);
            json::Value payload = loan::set(borrower, UInt256{1}, 1'000);
            payload[sfCounterparty.jsonName] = lender.human();
            auto const id =
                propose(env, borrower, proposal::unsignedPayload(env, payload, ticketSeq));

            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                auto const& rows = jrr[jss::signing_status];
                BEAST_EXPECT(rows.size() == 2);
                // The account row first, then the counterparty.
                BEAST_EXPECT(rows[0u][jss::account] == borrower.human());
                BEAST_EXPECT(rows[1u][jss::account] == lender.human());
                BEAST_EXPECT(rowIs(rows[1u], "inadequate_signatures"));
            }

            auto const counterSign = [&](STObject& tx, PublicKey const& key) {
                STObject signature = slot(sfCounterpartySignature);
                singleSign(signature, key);
                tx.setFieldObject(sfCounterpartySignature, signature);
            };

            // The lender co-signs through CounterpartySignature; the borrower
            // is still outstanding.
            stage(env, id, [&](STObject& tx) { counterSign(tx, lender.pk()); });
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(rowIs(rowFor(jrr, lender), std::nullopt));
                BEAST_EXPECT(rowIs(rowFor(jrr, borrower), "inadequate_signatures"));
            }

            // Both: complete, and the blob carries both signatures.
            stage(env, id, [&](STObject& tx) {
                counterSign(tx, lender.pk());
                singleSign(tx, borrower.pk());
            });
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "complete");
                auto const tx = parseTxBlob(jrr);
                BEAST_EXPECT(tx && tx->isFieldPresent(sfCounterpartySignature));
            }

            // A co-signature by someone else is not the lender's.
            stage(env, id, [&](STObject& tx) { counterSign(tx, bystander.pk()); });
            BEAST_EXPECT(rowIs(rowFor(query(env, byID(id)), lender), "not_authorized"));

            // A multi-signing lender accumulates shares inside the slot, scored
            // against the lender's own SignerList.
            env(signers(lender, 1, {{lenderSigner, 1}}));
            env.close();
            stage(env, id, [&](STObject& tx) {
                STObject signature = slot(sfCounterpartySignature);
                multiSign(signature, {share(lenderSigner)});
                tx.setFieldObject(sfCounterpartySignature, signature);
            });
            {
                auto const row = rowFor(query(env, byID(id)), lender);
                BEAST_EXPECT(rowIs(row, std::nullopt));
                BEAST_EXPECT(row[jss::quorum] == 1 && row[jss::signed_weight] == 1);
                BEAST_EXPECT(memberFor(row, lenderSigner)[kSigned] == true);
            }

            // A counterparty that does not exist cannot have authorized
            // anything.
            stage(env, id, [&](STObject& tx) {
                tx.setAccountID(sfCounterparty, ghost.id());
                counterSign(tx, ghost.pk());
            });
            BEAST_EXPECT(rowIs(rowFor(query(env, byID(id)), ghost), "account_not_found"));
            stage(env, id, [&](STObject& tx) {
                tx.setAccountID(sfCounterparty, ghost.id());
                STObject signature = slot(sfCounterpartySignature);
                multiSign(signature, {share(lenderSigner)});
                tx.setFieldObject(sfCounterpartySignature, signature);
            });
            BEAST_EXPECT(rowIs(rowFor(query(env, byID(id)), ghost), "account_not_found"));
        }

        // A LoanSet without a Counterparty requires the owner of the
        // LoanBroker it names, resolved from the live ledger.
        {
            UInt256 const brokerID{7};
            std::uint32_t const ticketSeq = proposal::createTicket(env, borrower);
            auto const id = propose(
                env,
                borrower,
                proposal::unsignedPayload(env, loan::set(borrower, brokerID, 1'000), ticketSeq));

            // No such broker: the co-signer is still required but cannot be
            // named, and submission fails the LoanSet with temBAD_SIGNER, so
            // the proposal cannot complete however the borrower signs.
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(jrr[jss::signing_status].size() == 2);
                auto const row = unnamedRow(jrr);
                BEAST_EXPECT(rowIs(row, "counterparty_unresolvable"));
                BEAST_EXPECT(noSignerListDetail(row));
            }
            stage(env, id, [&](STObject& tx) { singleSign(tx, borrower.pk()); });
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(!jrr.isMember(jss::tx_blob));
                BEAST_EXPECT(rowIs(rowFor(jrr, borrower), std::nullopt));
                BEAST_EXPECT(rowIs(unnamedRow(jrr), "counterparty_unresolvable"));
            }

            // With the broker on the ledger its owner is the co-signer. The
            // evaluation reads only the broker's Owner.
            modifyOpenLedger(env, [&](OpenView& view) {
                auto broker = std::make_shared<SLE>(keylet::loanBroker(brokerID));
                broker->setAccountID(sfOwner, lender.id());
                view.rawInsert(broker);
                return true;
            });
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::signing_status].size() == 2);
                BEAST_EXPECT(unnamedRow(jrr).isNull());
                BEAST_EXPECT(rowIs(rowFor(jrr, lender), "inadequate_signatures"));
            }
            stage(env, id, [&](STObject& tx) {
                singleSign(tx, borrower.pk());
                STObject signature = slot(sfCounterpartySignature);
                singleSign(signature, lender.pk());
                tx.setFieldObject(sfCounterpartySignature, signature);
            });
            BEAST_EXPECT(query(env, byID(id))[jss::proposal_status] == "complete");
        }

        // The implicit rule is LoanSet's alone: another transaction naming a
        // LoanBrokerID requires no counterparty.
        {
            UInt256 const brokerID{7};
            std::uint32_t const ticketSeq = proposal::createTicket(env, borrower);
            auto const id = propose(
                env,
                borrower,
                proposal::unsignedPayload(
                    env,
                    loan_broker::coverDeposit(borrower, brokerID, XRP(100).value()),
                    ticketSeq));
            modifyOpenLedger(env, [&](OpenView& view) {
                auto broker = std::make_shared<SLE>(keylet::loanBroker(brokerID));
                broker->setAccountID(sfOwner, lender.id());
                view.rawInsert(broker);
                return true;
            });
            BEAST_EXPECT(query(env, byID(id))[jss::signing_status].size() == 1);
        }
    }

    void
    testSponsor(FeatureBitset features)
    {
        testcase("sponsor co-signature or pre-authorization");

        using namespace jtx;

        Env env{*this, features};
        Account const target{"target"};
        Account const dest{"dest"};
        Account const patron{"patron"};  // the sponsor
        Account const patronSigner{"patronSigner"};
        Account const outsider{"outsider"};
        Account const regular{"regular"};
        Account const ghost{"ghost"};  // never funded
        env.fund(XRP(10000), target, dest, patron, patronSigner, outsider);
        env.memoize(regular);
        env.memoize(ghost);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        json::Value payload = pay(target, dest, XRP(1));
        payload[sfSponsor.jsonName] = patron.human();
        payload[sfSponsorFlags.jsonName] = spfSponsorFee;
        auto const id = propose(env, target, proposal::unsignedPayload(env, payload, ticketSeq));

        auto const sponsorSign = [&](STObject& tx, PublicKey const& key) {
            STObject signature = slot(sfSponsorSignature);
            singleSign(signature, key);
            tx.setFieldObject(sfSponsorSignature, signature);
        };

        // The sponsor is a required signer: with neither a SponsorSignature
        // nor a Sponsorship entry it is outstanding even once the target has
        // signed.
        stage(env, id, [&](STObject& tx) { singleSign(tx, target.pk()); });
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            auto const& rows = jrr[jss::signing_status];
            BEAST_EXPECT(rows.size() == 2);
            BEAST_EXPECT(rows[0u][jss::account] == target.human());
            BEAST_EXPECT(rows[1u][jss::account] == patron.human());
            BEAST_EXPECT(rowIs(rows[0u], std::nullopt));
            BEAST_EXPECT(rowIs(rows[1u], "inadequate_signatures"));
        }

        // The sponsor's own signature satisfies it, and is judged separately
        // from the target's: a bad one leaves the target's row intact.
        stage(env, id, [&](STObject& tx) {
            singleSign(tx, target.pk());
            sponsorSign(tx, patron.pk());
        });
        BEAST_EXPECT(query(env, byID(id))[jss::proposal_status] == "complete");

        stage(env, id, [&](STObject& tx) {
            singleSign(tx, target.pk());
            sponsorSign(tx, outsider.pk());
        });
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, target), std::nullopt));
            BEAST_EXPECT(rowIs(rowFor(jrr, patron), "not_authorized"));
        }

        // A sponsor that does not exist, with or without a signature.
        stage(env, id, [&](STObject& tx) { tx.setAccountID(sfSponsor, ghost.id()); });
        BEAST_EXPECT(rowIs(rowFor(query(env, byID(id)), ghost), "account_not_found"));
        stage(env, id, [&](STObject& tx) {
            tx.setAccountID(sfSponsor, ghost.id());
            sponsorSign(tx, ghost.pk());
        });
        BEAST_EXPECT(rowIs(rowFor(query(env, byID(id)), ghost), "account_not_found"));
        stage(env, id, [&](STObject& tx) {
            tx.setAccountID(sfSponsor, ghost.id());
            STObject signature = slot(sfSponsorSignature);
            multiSign(signature, {share(patronSigner)});
            tx.setFieldObject(sfSponsorSignature, signature);
        });
        BEAST_EXPECT(rowIs(rowFor(query(env, byID(id)), ghost), "account_not_found"));

        // A Sponsorship entry whose flags demand a co-signature for what this
        // transaction sponsors does not pre-authorize it...
        env(sponsor::set(
                patron,
                tfSponsorshipSetRequireSignForFee | tfSponsorshipSetRequireSignForReserve,
                100,
                XRP(100),
                XRP(1)),
            Fee(XRP(1)),
            sponsor::SponseeAcc(target));
        env.close();
        stage(env, id, [&](STObject& tx) { singleSign(tx, target.pk()); });
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, patron), "awaiting_sponsorship_signature"));
        }

        // ...unless the demand is for something else: a fee-only sponsorship
        // needs no signature when only the reserve requires one, and the
        // other way round.
        env(sponsor::set(patron, tfSponsorshipClearRequireSignForFee),
            Fee(XRP(1)),
            sponsor::SponseeAcc(target));
        env.close();
        stage(env, id, [&](STObject& tx) { singleSign(tx, target.pk()); });
        BEAST_EXPECT(query(env, byID(id))[jss::proposal_status] == "complete");

        stage(env, id, [&](STObject& tx) {
            singleSign(tx, target.pk());
            tx.setFieldU32(sfSponsorFlags, spfSponsorReserve);
        });
        BEAST_EXPECT(rowIs(rowFor(query(env, byID(id)), patron), "awaiting_sponsorship_signature"));

        env(sponsor::set(patron, tfSponsorshipClearRequireSignForReserve),
            Fee(XRP(1)),
            sponsor::SponseeAcc(target));
        env.close();
        stage(env, id, [&](STObject& tx) {
            singleSign(tx, target.pk());
            tx.setFieldU32(sfSponsorFlags, spfSponsorReserve);
        });
        BEAST_EXPECT(query(env, byID(id))[jss::proposal_status] == "complete");

        // A SponsorSignature that no longer authorizes is not rescued by the
        // entry: submission validates a present SponsorSignature regardless.
        env(regkey(patron, regular));
        env(fset(patron, asfDisableMaster), Sig(patron));
        env.close();
        stage(env, id, [&](STObject& tx) {
            singleSign(tx, target.pk());
            sponsorSign(tx, patron.pk());
        });
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, patron), "master_disabled"));
        }
        stage(env, id, [&](STObject& tx) {
            singleSign(tx, target.pk());
            sponsorSign(tx, regular.pk());
        });
        BEAST_EXPECT(query(env, byID(id))[jss::proposal_status] == "complete");

        // A stored payload that cannot be rebuilt as a transaction (here, a
        // TransactionType no format exists for) cannot be judged by the
        // sponsorship rule at all.
        stage(env, id, [&](STObject& tx) {
            singleSign(tx, target.pk());
            tx.setFieldU16(sfTransactionType, 9999);
        });
        BEAST_EXPECT(rowIs(rowFor(query(env, byID(id)), patron), "malformed"));

        // A multi-signing sponsor accumulates shares inside the slot.
        env(signers(patron, 1, {{patronSigner, 1}}), Sig(regular));
        env.close();
        stage(env, id, [&](STObject& tx) {
            singleSign(tx, target.pk());
            STObject signature = slot(sfSponsorSignature);
            multiSign(signature, {share(patronSigner)});
            tx.setFieldObject(sfSponsorSignature, signature);
        });
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "complete");
            auto const row = rowFor(jrr, patron);
            BEAST_EXPECT(row[jss::quorum] == 1 && row[jss::signed_weight] == 1);
            BEAST_EXPECT(memberFor(row, patronSigner)[kSigned] == true);
        }
    }

    // Reserve sponsorship of a delegated transaction is rejected by
    // Transactor::checkSponsor before any signature is looked at, so no
    // sponsor signature or Sponsorship entry can ever satisfy that row.
    void
    testRejectedSponsorship(FeatureBitset features)
    {
        testcase("sponsorship submission rejects outright");

        using namespace jtx;

        Env env{*this, features};
        Account const target{"target"};
        Account const delegateAcct{"delegateAcct"};
        Account const dest{"dest"};
        Account const patron{"patron"};
        env.fund(XRP(10000), target, delegateAcct, dest, patron);
        env.close();

        env(delegate::set(target, delegateAcct, {"Payment"}));
        env(sponsor::set(patron, 0, 100, XRP(100), XRP(1)),
            Fee(XRP(1)),
            sponsor::SponseeAcc(delegateAcct));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        json::Value payload = pay(target, dest, XRP(1));
        payload[sfDelegate.jsonName] = delegateAcct.human();
        payload[sfSponsor.jsonName] = patron.human();
        payload[sfSponsorFlags.jsonName] = spfSponsorReserve;
        auto const id =
            propose(env, delegateAcct, proposal::unsignedPayload(env, payload, ticketSeq));

        // Neither the pre-authorizing entry...
        stage(env, id, [&](STObject& tx) { singleSign(tx, delegateAcct.pk()); });
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(!jrr.isMember(jss::tx_blob));
            BEAST_EXPECT(rowIs(rowFor(jrr, delegateAcct), std::nullopt));
            BEAST_EXPECT(rowIs(rowFor(jrr, patron), "invalid_sponsorship"));
        }

        // ...nor the sponsor's own signature satisfies it.
        stage(env, id, [&](STObject& tx) {
            singleSign(tx, delegateAcct.pk());
            STObject signature = slot(sfSponsorSignature);
            singleSign(signature, patron.pk());
            tx.setFieldObject(sfSponsorSignature, signature);
        });
        {
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, patron), "invalid_sponsorship"));
        }
    }

    void
    testBatch(FeatureBitset features)
    {
        testcase("proposed Batch participants");

        using namespace jtx;

        Env env{*this, features};
        Account const target{"target"};  // the outer account
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const delegateAcct{"delegateAcct"};
        Account const lender{"lender"};
        Account const patron{"patron"};
        Account const phantom{"phantom"};  // never funded
        env.fund(XRP(10000), target, bob, carol, dave, delegateAcct, lender, patron);
        env.memoize(phantom);
        env.close();

        auto const batchOf = [&](std::uint32_t ticketSeq, std::vector<json::Value> const& inners) {
            return proposal::unsignedBatch(env, target, ticketSeq, tfAllOrNothing, inners);
        };

        // The outer account and one other participant. Bob's SignerList is
        // set up front: a later SignerListSet would move his Sequence past
        // his inner's.
        {
            env(signers(bob, 2, {{carol, 1}, {dave, 1}}));
            env.close();
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                batchOf(
                    ticketSeq,
                    {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target) + 1),
                     proposal::innerTx(pay(bob, target, XRP(1)), env.seq(bob))}));

            // The outer account's own inner adds no row.
            {
                auto const jrr = query(env, byID(id));
                auto const& rows = jrr[jss::signing_status];
                BEAST_EXPECT(rows.size() == 2);
                BEAST_EXPECT(rows[0u][jss::account] == target.human());
                BEAST_EXPECT(rows[1u][jss::account] == bob.human());
                BEAST_EXPECT(rowIs(rows[1u], "inadequate_signatures"));
            }

            // The outer account signs the Batch itself; bob is outstanding.
            stage(env, id, [&](STObject& tx) { singleSign(tx, target.pk()); });
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(rowIs(rowFor(jrr, target), std::nullopt));
                BEAST_EXPECT(rowIs(rowFor(jrr, bob), "inadequate_signatures"));
            }

            // Bob's single-signed BatchSigners entry completes it.
            stage(env, id, [&](STObject& tx) {
                singleSign(tx, target.pk());
                STObject entry = batchSigner(bob.id());
                singleSign(entry, bob.pk());
                setBatchSigners(tx, {entry});
            });
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "complete");
                auto const tx = parseTxBlob(jrr);
                BEAST_EXPECT(tx && tx->getFieldArray(sfBatchSigners).size() == 1);
            }

            // Entries submission rejects outright, which the Sign transaction
            // never stores: one for an account the Batch does not require,
            // one for the outer account, and a duplicate. Each is an
            // unsatisfiable row of its own, so the proposal cannot complete.
            auto const withExtraEntry = [&](STObject extra) {
                stage(env, id, [&](STObject& tx) {
                    singleSign(tx, target.pk());
                    STObject bobEntry = batchSigner(bob.id());
                    singleSign(bobEntry, bob.pk());
                    setBatchSigners(tx, {bobEntry, std::move(extra)});
                });
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(!jrr.isMember(jss::tx_blob));
                BEAST_EXPECT(jrr[jss::signing_status].size() == 3);
                BEAST_EXPECT(rowIs(rowFor(jrr, bob), std::nullopt));
                return jrr;
            };
            {
                STObject entry = batchSigner(dave.id());
                singleSign(entry, dave.pk());
                auto const jrr = withExtraEntry(entry);
                BEAST_EXPECT(rowIs(rowFor(jrr, dave), "malformed"));
                BEAST_EXPECT(noSignerListDetail(rowFor(jrr, dave)));
            }
            {
                STObject entry = batchSigner(target.id());
                singleSign(entry, target.pk());
                auto const jrr = withExtraEntry(entry);
                // The outer account's own row is first and satisfied; its
                // rejected entry is a separate row after the participants.
                auto const& rows = jrr[jss::signing_status];
                BEAST_EXPECT(rowIs(rows[0u], std::nullopt));
                BEAST_EXPECT(rows[2u][jss::account] == target.human());
                BEAST_EXPECT(rowIs(rows[2u], "malformed"));
            }
            {
                STObject entry = batchSigner(bob.id());
                singleSign(entry, bob.pk());
                auto const jrr = withExtraEntry(entry);
                auto const& rows = jrr[jss::signing_status];
                BEAST_EXPECT(rows[2u][jss::account] == bob.human());
                BEAST_EXPECT(rowIs(rows[2u], "malformed"));
            }

            // Bob authorizes through his own SignerList inside his entry,
            // scored per participant.
            auto const bobShares = [&](std::vector<Share> const& shares) {
                stage(env, id, [&](STObject& tx) {
                    singleSign(tx, target.pk());
                    STObject entry = batchSigner(bob.id());
                    multiSign(entry, shares);
                    setBatchSigners(tx, {entry});
                });
                return query(env, byID(id));
            };
            {
                auto const jrr = bobShares({share(carol)});
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                auto const row = rowFor(jrr, bob);
                BEAST_EXPECT(rowIs(row, "inadequate_signatures"));
                BEAST_EXPECT(row[jss::signed_weight] == 1 && row[jss::quorum] == 2);
                BEAST_EXPECT(memberFor(row, carol)[kSigned] == true);
                BEAST_EXPECT(memberFor(row, dave)[kSigned] == false);
            }
            BEAST_EXPECT(
                bobShares({share(carol), share(dave)})[jss::proposal_status] == "complete");
        }

        // A participant whose account does not exist yet: an earlier inner
        // may create it, so only its own master key may authorize it.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                batchOf(
                    ticketSeq,
                    {proposal::innerTx(pay(target, phantom, XRP(1000)), env.seq(target) + 1),
                     proposal::innerTx(pay(phantom, target, XRP(1)), 1)}));

            auto const phantomSigns = [&](std::function<void(STObject&)> const& sign) {
                stage(env, id, [&](STObject& tx) {
                    singleSign(tx, target.pk());
                    STObject entry = batchSigner(phantom.id());
                    sign(entry);
                    setBatchSigners(tx, {entry});
                });
                return query(env, byID(id));
            };

            BEAST_EXPECT(phantomSigns([&](STObject& e) {
                             singleSign(e, phantom.pk());
                         })[jss::proposal_status] == "complete");
            BEAST_EXPECT(rowIs(
                rowFor(phantomSigns([&](STObject& e) { singleSign(e, bob.pk()); }), phantom),
                "not_authorized"));
            BEAST_EXPECT(rowIs(
                rowFor(phantomSigns([&](STObject& e) { multiSign(e, {share(bob)}); }), phantom),
                "no_signer_list"));
        }

        // Without an earlier inner that creates it, with the creating inner
        // after the one it must authorize, with a creating payment below the
        // account reserve, or with one into a permissioned domain (in both
        // of which Payment::preclaim refuses to create), the participant does
        // not exist as far as submission is concerned.
        enum class Creator { None, Later, BelowReserve, Domain };
        for (auto const creator :
             {Creator::None, Creator::Later, Creator::BelowReserve, Creator::Domain})
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            std::vector<json::Value> inners;
            if (creator == Creator::BelowReserve)
            {
                BEAST_EXPECT(XRP(100).value() < STAmount{env.current()->fees().reserve});
                inners.push_back(
                    proposal::innerTx(pay(target, phantom, XRP(100)), env.seq(target) + 1));
            }
            else if (creator == Creator::Domain)
            {
                json::Value creating = pay(target, phantom, XRP(1000));
                creating[sfDomainID.jsonName] = to_string(uint256{1});
                inners.push_back(proposal::innerTx(creating, env.seq(target) + 1));
            }
            inners.push_back(proposal::innerTx(pay(phantom, target, XRP(1)), 1));
            if (creator == Creator::Later)
            {
                inners.push_back(
                    proposal::innerTx(pay(target, phantom, XRP(1000)), env.seq(target) + 1));
            }
            else if (creator == Creator::None)
            {
                inners.push_back(proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target) + 1));
            }
            auto const id = propose(env, target, batchOf(ticketSeq, inners));

            stage(env, id, [&](STObject& tx) {
                singleSign(tx, target.pk());
                STObject entry = batchSigner(phantom.id());
                singleSign(entry, phantom.pk());
                setBatchSigners(tx, {entry});
            });
            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(rowIs(rowFor(jrr, phantom), "account_not_found"));
        }

        // A delegated inner is authorized by its Delegate in the account's
        // place; an inner Counterparty is a participant too.
        {
            // bob's grant keeps this case about row membership and order; a
            // missing grant is testBatchInnerRules' subject.
            env(delegate::set(bob, delegateAcct, {"Payment"}));
            env.close();
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            json::Value delegated = pay(bob, target, XRP(1));
            delegated[sfDelegate.jsonName] = delegateAcct.human();
            json::Value loanSet = loan::set(carol, UInt256{1}, 1'000);
            loanSet[sfCounterparty.jsonName] = lender.human();
            auto const id = propose(
                env,
                target,
                batchOf(
                    ticketSeq,
                    {proposal::innerTx(delegated, env.seq(bob)),
                     proposal::innerTx(loanSet, env.seq(carol))}));

            auto const jrr = query(env, byID(id));
            BEAST_EXPECT(jrr[jss::signing_status].size() == 4);
            BEAST_EXPECT(rowFor(jrr, bob).isNull());
            BEAST_EXPECT(rowIs(rowFor(jrr, delegateAcct), "inadequate_signatures"));
            BEAST_EXPECT(rowIs(rowFor(jrr, carol), "inadequate_signatures"));
            BEAST_EXPECT(rowIs(rowFor(jrr, lender), "inadequate_signatures"));
            // Participants follow the account row in ascending account order.
            auto const& rows = jrr[jss::signing_status];
            BEAST_EXPECT(rows[0u][jss::account] == target.human());
            std::vector<std::string> participants;
            for (unsigned i = 1; i < rows.size(); ++i)
                participants.push_back(rows[i][jss::account].asString());
            BEAST_EXPECT(std::ranges::is_sorted(participants, [](auto const& a, auto const& b) {
                return *parseBase58<AccountID>(a) < *parseBase58<AccountID>(b);
            }));
        }

        // An inner Sponsor is a participant only when it co-signs, which the
        // inner marks by carrying a SponsorSignature slot. Fee sponsorship is
        // not allowed inside a Batch, so the stored form is staged directly.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                batchOf(
                    ticketSeq,
                    {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target) + 1),
                     proposal::innerTx(pay(bob, target, XRP(1)), env.seq(bob))}));

            auto const withSponsoredInner = [&](bool coSigning) {
                json::Value inner = pay(bob, target, XRP(1));
                inner[sfSponsor.jsonName] = patron.human();
                inner[sfSponsorFlags.jsonName] = spfSponsorReserve;
                if (coSigning)
                    inner[sfSponsorSignature.jsonName] = json::Value{json::ValueType::Object};
                auto const jt = env.jt(
                    proposal::create(
                        target,
                        batchOf(
                            ticketSeq,
                            {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target)),
                             proposal::innerTx(inner, env.seq(bob))}),
                        proposal::expiration(env, std::chrono::seconds{1000})));
                STObject const stored = jt.stx->getFieldObject(sfProposedTransaction);
                stage(env, id, [&](STObject& tx) { tx = stored; });
                return query(env, byID(id));
            };

            {
                auto const jrr = withSponsoredInner(true);
                BEAST_EXPECT(jrr[jss::signing_status].size() == 3);
                BEAST_EXPECT(rowIs(rowFor(jrr, patron), "inadequate_signatures"));
            }

            // An inner Sponsor that does not co-sign is authorized only by a
            // Sponsorship entry toward the inner's account, as that inner's
            // own preclaim demands: none, one whose flags require a signature
            // nothing can collect, one that pre-authorizes it.
            {
                auto const jrr = withSponsoredInner(false);
                BEAST_EXPECT(jrr[jss::signing_status].size() == 3);
                BEAST_EXPECT(rowIs(rowFor(jrr, patron), "sponsorship_entry_required"));
            }
            env(sponsor::set(patron, tfSponsorshipSetRequireSignForReserve, 100, XRP(100), XRP(1)),
                Fee(XRP(1)),
                sponsor::SponseeAcc(bob));
            env.close();
            BEAST_EXPECT(
                rowIs(rowFor(withSponsoredInner(false), patron), "sponsorship_entry_required"));
            env(sponsor::set(patron, tfSponsorshipClearRequireSignForReserve),
                Fee(XRP(1)),
                sponsor::SponseeAcc(bob));
            env.close();
            BEAST_EXPECT(rowIs(rowFor(withSponsoredInner(false), patron), std::nullopt));
        }

        // BatchSigners entries submission rejects for their shape rather than
        // their account: one carrying no account at all, and two required
        // entries out of order.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                batchOf(
                    ticketSeq,
                    {proposal::innerTx(pay(bob, target, XRP(1)), env.seq(bob)),
                     proposal::innerTx(pay(carol, target, XRP(1)), env.seq(carol))}));

            stage(env, id, [&](STObject& tx) {
                singleSign(tx, target.pk());
                STObject bobEntry = batchSigner(bob.id());
                singleSign(bobEntry, bob.pk());
                STObject carolEntry = batchSigner(carol.id());
                singleSign(carolEntry, carol.pk());
                setBatchSigners(tx, {bobEntry, carolEntry, STObject{sfBatchSigner}});
            });
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(jrr[jss::signing_status].size() == 4);
                BEAST_EXPECT(rowIs(rowFor(jrr, bob), std::nullopt));
                BEAST_EXPECT(rowIs(rowFor(jrr, carol), std::nullopt));
                BEAST_EXPECT(rowIs(unnamedRow(jrr), "malformed"));
            }

            stage(env, id, [&](STObject& tx) {
                singleSign(tx, target.pk());
                STObject bobEntry = batchSigner(bob.id());
                singleSign(bobEntry, bob.pk());
                STObject carolEntry = batchSigner(carol.id());
                singleSign(carolEntry, carol.pk());
                // Deliberately out of order: the later account first.
                STArray batchSigners{sfBatchSigners};
                if (bob.id() < carol.id())
                {
                    batchSigners.push_back(carolEntry);
                    batchSigners.push_back(bobEntry);
                }
                else
                {
                    batchSigners.push_back(bobEntry);
                    batchSigners.push_back(carolEntry);
                }
                tx.setFieldArray(sfBatchSigners, batchSigners);
            });
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
                BEAST_EXPECT(jrr[jss::signing_status].size() == 4);
                auto const& rows = jrr[jss::signing_status];
                BEAST_EXPECT(rowIs(rows[3u], "malformed"));
                BEAST_EXPECT(
                    rows[3u][jss::account] == (bob.id() < carol.id() ? bob : carol).human());
            }
        }
    }

    void
    testExpiry(FeatureBitset features)
    {
        testcase("terminal proposals report expired");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), target, bob);
        env.close();

        // Expiration reached: expired whatever was collected, and a fully
        // signed one still hands back its submittable transaction.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const id = propose(
                env,
                target,
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq),
                proposal::expiration(env, 60s));

            BEAST_EXPECT(query(env, byID(id))[jss::proposal_status] == "pending");

            env.close(120s);
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "expired");
                BEAST_EXPECT(!jrr.isMember(jss::tx_blob));
                BEAST_EXPECT(rowIs(rowFor(jrr, target), "inadequate_signatures"));
            }

            stage(env, id, [&](STObject& tx) { singleSign(tx, target.pk()); });
            {
                auto const jrr = query(env, byID(id));
                BEAST_EXPECT(jrr[jss::proposal_status] == "expired");
                BEAST_EXPECT(rowIs(rowFor(jrr, target), std::nullopt));
                BEAST_EXPECT(parseTxBlob(jrr).has_value());
            }
        }

        // LastLedgerSequence passed: the transaction can enter no ledger.
        // The bound is the earliest ledger it could still enter, which is the
        // queried ledger itself while open and the next one once closed.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            std::uint32_t const lastLedgerSeq = env.current()->header().seq + 2;
            json::Value payload = pay(target, bob, XRP(1));
            payload[sfLastLedgerSequence.jsonName] = lastLedgerSeq;
            auto const id =
                propose(env, target, proposal::unsignedPayload(env, payload, ticketSeq));
            env.close();
            BEAST_EXPECT(env.current()->header().seq == lastLedgerSeq);

            auto const at = [&](char const* shortcut) {
                json::Value params = byID(id);
                params[jss::ledger_index] = shortcut;
                return query(env, params)[jss::proposal_status];
            };
            auto const atSeq = [&](std::uint32_t seq) {
                json::Value params = byID(id);
                params[jss::ledger_index] = seq;
                return query(env, params)[jss::proposal_status];
            };

            // Open ledger at the bound: still enterable. The validated ledger
            // before it: its successor is the bound, so still enterable.
            BEAST_EXPECT(at("current") == "pending");
            BEAST_EXPECT(at("validated") == "pending");

            env.close();
            // Open ledger past the bound: expired. The validated ledger at
            // the bound: its successor is past it, so expired as well. The
            // one before is unchanged.
            BEAST_EXPECT(at("current") == "expired");
            BEAST_EXPECT(at("validated") == "expired");
            BEAST_EXPECT(atSeq(lastLedgerSeq - 1) == "pending");
        }
    }

    void
    testSubmitCompleteProposal(FeatureBitset features)
    {
        testcase("a complete proposal's tx_blob is submittable");

        using namespace jtx;

        Env env{*this, features};
        Account const target{"target"};
        Account const dest{"dest"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10000), target, dest, bob, carol);
        env.close();

        env(signers(target, 2, {{bob, 1}, {carol, 1}}));
        env.close();

        // The fee is fixed at creation and must cover the two shares to come.
        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        json::Value payload = pay(target, dest, XRP(100));
        payload[jss::Fee] = std::to_string((env.current()->fees().base * 3).drops());
        payload = proposal::unsignedPayload(env, payload, ticketSeq);
        auto const id = propose(env, target, payload);

        // Real shares over the stored transaction, as TransactionProposalSign
        // would record them.
        auto const signedTx = env.jt(payload, Msig(bob, carol)).stx;
        if (!BEAST_EXPECT(signedTx))
            return;
        stage(env, id, [&](STObject& tx) {
            tx.setFieldArray(sfSigners, signedTx->getFieldArray(sfSigners));
        });

        auto const jrr = query(env, byID(id));
        BEAST_EXPECT(jrr[jss::proposal_status] == "complete");
        auto const tx = parseTxBlob(jrr);
        if (!tx.has_value())
        {
            BEAST_EXPECTS(false, "no tx_blob in " + to_string(jrr));
            return;
        }

        // The blob is the signed transaction, down to its hash.
        BEAST_EXPECT(tx->getTransactionID() == signedTx->getTransactionID());

        // It applies: the Ticket is consumed, which also deletes the proposal.
        auto const submitted = env.rpc("submit", jrr[jss::tx_blob].asString())[jss::result];
        BEAST_EXPECTS(submitted[jss::engine_result] == "tesSUCCESS", to_string(submitted));
        env.close();
        BEAST_EXPECT(env.balance(dest) == XRP(10100));
        BEAST_EXPECT(!env.le(keylet::txProposal(id)));
        BEAST_EXPECT(query(env, byID(id))[jss::error] == "entryNotFound");
    }

    void
    run() override
    {
        using namespace jtx;
        FeatureBitset const all{testableAmendments()};

        testDisabled(all);
        testMalformedRequests(all);
        testPendingUnsigned(all);
        testSingleSign(all);
        testMultiSign(all);
        testMalformedMaterial(all);
        testDelegate(all);
        testCounterparty(all);
        testSponsor(all);
        testRejectedSponsorship(all);
        testBatch(all);
        testBatchInnerRules(all);
        testExpiry(all);
        testSubmitCompleteProposal(all);
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposal, rpc, xrpl);

}  // namespace xrpl::test
