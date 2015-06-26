//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

  Permission to use, copy, modify, and/or distribute this software for any
  purpose  with  or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
  MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>               // getApp
#include <ripple/app/ledger/tests/common_ledger.h>
#include <ripple/test/jtx.h>


namespace ripple {
namespace test {

//------------------------------------------------------------------------------

/** All information that can be associated with a Ticket. */
struct TicketInfo
{
    AccountID owner;
    std::uint32_t seq;
    AccountID target;
    std::uint32_t expiration;

    TicketInfo (AccountID const& ownerID, std::uint32_t sequence)
    : owner (ownerID)
    , seq (sequence)
    , target {}
    , expiration (std::numeric_limits<std::uint32_t>::max()) { }

    TicketInfo (AccountID const& ownerID, std::uint32_t sequence,
        AccountID const& targetID, std::uint32_t expry)
    : owner (ownerID)
    , seq (sequence)
    , target (targetID)
    , expiration (expry) { }
};

/** Return information on all Tickets in Ledger owned by acct. */
std::vector <TicketInfo>
getTicketsOnAccount (
    std::shared_ptr<Ledger>& ledger, jtx::Account const& acct)
{
    std::vector <TicketInfo> tickets;

    forEachItem (*ledger, acct,
        [&tickets](const std::shared_ptr<const SLE>& sleCur)
        {
            // If sleCur is an ltTICKET save it.
            if (sleCur && sleCur->getType () == ltTICKET)
            {
                AccountID const acct = sleCur->getAccountID (sfAccount);
                std::uint32_t const seq = sleCur->getFieldU32 (sfSequence);
                AccountID target {};
                std::uint32_t expiration =
                    std::numeric_limits<std::uint32_t>::max();

                // Deal with optional Ticket fields.
                if (sleCur->isFieldPresent (sfTarget))
                    target = sleCur->getAccountID (sfTarget);
                if (sleCur->isFieldPresent (sfExpiration))
                    expiration = sleCur->getFieldU32 (sfExpiration);

                tickets.emplace_back (acct, seq, target, expiration);
            }
        });
    return tickets;
}

//------------------------------------------------------------------------------

// EnvTicket -- A specialized jtx::Env that supports transaction retries.
//
// Initially Tickets had problems with 'ter' and 'tec' transaction errors.
// They would get bollixed up when the retry occurred.  I saw instances where
// the Fee was applied twice on a `tec`.
//
// In order to test for regressions of these problems, the Env for Tickets
// is enhanced so it supports retrying of transactions.

class EnvTicket : public jtx::Env
{
public:
    /** The local transactions. */
    std::unique_ptr<LocalTxs> local_txs;

    EnvTicket(beast::unit_test::suite& test_)
    : Env (test_)
    , local_txs (LocalTxs::New())
    {
    }

    ~EnvTicket() override = default;

    /** Submit an existing JTx.
        This calls postconditions.
    */
    void
    submit (jtx::JTx const& jt) override;
};

void
EnvTicket::submit (jtx::JTx const& jt)
{
    auto const stx = st(jt);
    TER ter;
    bool didApply;
    if (stx)
    {
        // Save the transaction for retries.
        local_txs->push_back (
            ledger->getLedgerSeq(), std::make_shared<STTx>(*stx));

        TransactionEngine txe (ledger, tx_enable_test);
        std::tie(ter, didApply) = txe.applyTransaction(
            *stx, tapOPEN_LEDGER |
                (true ? tapNONE : tapNO_CHECK_SIGN));
    }
    else
    {
        // Convert the exception into a TER so that
        // callers can expect it using ter(temMALFORMED)
        ter = temMALFORMED;
        didApply = false;
    }
    if (! test.expect(ter == jt.ter,
        "apply: " + transToken(ter) +
            " (" + transHuman(ter) + ")"))
    {
        test.log << pretty(jt.jv);
        // Don't check postconditions if
        // we didn't get the expected result.
        return;
    }
    for (auto const& f : jt.requires)
        f(*this);
}

//------------------------------------------------------------------------------

namespace jtx {
namespace ticket {
/** Return JSON for a TicketCancel transaction. */
Json::Value
cancel (jtx::Account const& account, TicketInfo const& ticketInfo)
{
    Json::Value jv;
    auto& ticketID = jv["TicketID"];
    ticketID[jss::Account] = to_string(ticketInfo.owner);
    ticketID[jss::Sequence] = ticketInfo.seq;

    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "TicketCancel";
    return jv;
}

} // ticket
} // jtx

//------------------------------------------------------------------------------

/** Funclet to set a Ticket on a transaction in the jtx framework.

I would prefer to call this funclet "ticket", but that name was taken
by the jtx::ticket namespace.  So "tckt" will have to do.
*/
class tckt
{
private:
    AccountID owner_;
    std::uint32_t seq_;

public:
    explicit
    tckt (TicketInfo const& ticketInf)
        : owner_ (ticketInf.owner)
        , seq_ (ticketInf.seq)
    {
    }

    void
    operator()(jtx::Env const&, jtx::JTx& tx) const;
};

void
tckt::operator()(jtx::Env const&, jtx::JTx& tx) const
{
    auto& ticketID = tx["TicketID"];
    ticketID[jss::Account] = to_string(owner_);
    ticketID[jss::Sequence] = seq_;

    // A transaction with a Ticket always has a Sequence of zero.
    tx[jss::Sequence] = 0;
}

//------------------------------------------------------------------------------

/** Funclet to set LastLedgerSequence on a transaction in the JTx framework. */
class last_ledger_seq
{
private:
    std::uint32_t last_seq_;

public:
    last_ledger_seq (std::uint32_t last_seq)
        : last_seq_(last_seq)
    {
    }

    void
    operator()(jtx::Env const&, jtx::JTx& tx) const;
};

void
last_ledger_seq::operator() (jtx::Env const&, jtx::JTx& tx) const
{
    tx[jss::LastLedgerSequence] = last_seq_;
}

//------------------------------------------------------------------------------

class Ticket_test : public beast::unit_test::suite
{
public:
    // Used to generate a 'tel' error.
    static Json::Value
    set_message_key (jtx::Account const& account, std::string const& key)
    {
        Json::Value jv;
        jv[jss::Account] = account.human();
        jv[jss::MessageKey] = key;
        jv[jss::TransactionType] = "AccountSet";
        return jv;
    }

    void
    testTicket()
    {
        using namespace jtx;
        namespace ticket = jtx::ticket;

        EnvTicket env(*this);

        // We need to be able to advance the ledger to test Ticket expiration.
        std::shared_ptr<Ledger const> lastClosedLedger =
            std::make_shared<Ledger>(false, *env.ledger);

        // This lambda makes it easy to advance the ledger
        auto advanceLedger = [this, &env, &lastClosedLedger] ()
        {
            // Advance time enough so we have a new ledger time.
            close_and_advance (
                env.ledger, lastClosedLedger,
                env.ledger->getCloseResolution(), env.local_txs);
        };

        Account const alice ("alice", KeyType::ed25519);
        Account const becky ("becky", KeyType::secp256k1);
        Account const cheri ("cheri", KeyType::ed25519);

        env.fund (XRP(10000), alice, becky, cheri);

        advanceLedger();

        // Get alice's tickets.  Should be empty.
        auto aliceTickets = getTicketsOnAccount (env.ledger, alice);
        env.require (owners (alice, 0), tickets (alice, 0));

        // Have alice create a Ticket.
        env (ticket::create (alice));

        advanceLedger();

        aliceTickets = getTicketsOnAccount (env.ledger, alice);
        env.require (owners (alice, 1), tickets (alice, 1));

        std::uint64_t const baseFee = env.ledger->getBaseFee();
        // Use that Ticket to submit a transaction.
        {
            std::uint32_t const aliceSeq = env.seq (alice);
            STAmount const aliceOldBalance = env.balance (alice, XRP);
            STAmount const alicePays = drops (1000) - baseFee;
            env (pay (alice, env.master, alicePays),
                fee (baseFee), tckt (aliceTickets[0]));

            // The transaction should have consumed alice's Ticket.
            advanceLedger();

            env.require (owners (alice, 0), tickets (alice, 0));
            auto const aliceNewBalance = env.balance (alice, XRP);
            expect (aliceOldBalance == aliceNewBalance + drops (1000));

            // Since we used a Ticket, alice's Sequence should be unchanged.
            expect (aliceSeq == env.seq (alice));
        }

        //----------------------------------------------------------------------
        // It should not be possible to re-use the Ticket.
        env (pay (alice, env.master, drops (1000)),
            tckt (aliceTickets[0]), ter (tefNO_ENTRY));

        //----------------------------------------------------------------------
        // Have alice create a couple of Tickets with cheri as the target.
        env (ticket::create (alice, cheri));
        env (ticket::create (alice, cheri));
        advanceLedger();
        aliceTickets = getTicketsOnAccount (env.ledger, alice);
        env.require (owners (alice, 2), tickets (alice, 2));

        // becky should not be able to use those Tickets.
        env (pay (becky, env.master, drops (1000)),
            tckt (aliceTickets[0]), ter (tefNO_PERMISSION));
        advanceLedger();

        // alice's Tickets should still be available.
        env.require (owners (alice, 2), tickets (alice, 2));

        // Have alice and cheri use the Tickets.  Should work.  Since they
        // are using Tickets the Sequence on the accounts should not change.
        {
            std::uint32_t const aliceSeq = env.seq (alice);
            std::uint32_t const cheriSeq = env.seq (cheri);

            env (pay (alice, env.master, drops (1000)),
                tckt (aliceTickets[0]));
            env (pay (cheri, env.master, drops (1000)),
                tckt (aliceTickets[1]));
            advanceLedger();

            // Both of alice's Tickets should be consumed and the account
            // sequences should not have moved.
            env.require (owners (alice, 0), tickets (alice, 0));
            expect (aliceSeq == env.seq (alice));
            expect (cheriSeq == env.seq (cheri));
        }

        // Test tickets with expirations.
        std::uint32_t const expResolution = env.ledger->getCloseResolution();
        std::uint32_t const halfResolution = expResolution / 2;
        assert (halfResolution > 0); // Sanity check on assumptions.

        // Create a Ticket with an expiration time that has already passed.
        // Should succeed but no Ticket should be created.
        {
            std::uint32_t const now = env.ledger->getParentCloseTimeNC();
            env (ticket::create (alice, now - halfResolution));
            env.require (owners (alice, 0), tickets (alice, 0));
        }

        // Create a couple of Tickets with expirations.  Consume one in a
        // timely fashion.  Let the other expire and then use it.
        {
            std::uint32_t const now = env.ledger->getParentCloseTimeNC();
            env (ticket::create (alice, now + halfResolution + expResolution));
            env (ticket::create (alice, now + halfResolution + expResolution));
            advanceLedger();

            std::uint32_t const aliceSeq = env.seq (alice);
            aliceTickets = getTicketsOnAccount (env.ledger, alice);
            env.require (owners (alice, 2), tickets (alice, 2));
            env (pay (alice, env.master, drops (1000)),
                tckt (aliceTickets[1]));

            // Advancing the ledger causes time to pass.  The remaining Ticket
            // should now expire.
            advanceLedger();

            STAmount const aliceOldBalance = env.balance (alice, XRP);
            env (pay (alice, env.master, drops (1000)), fee (baseFee),
                tckt (aliceTickets[0]), ter (tecEXPIRED_TICKET));

            advanceLedger();

            // Since the error was a 'tec' make sure that the Fee was charged.
            // Charging the Fee should also consume the Ticket.
            expect (aliceOldBalance == env.balance (alice, XRP) + baseFee);
            env.require (owners (alice, 0), tickets (alice, 0));
            expect (aliceSeq == env.seq (alice));
        }

        // Create a couple of Tickets with a targets and an expiration.
        // Consume one in a timely fashion.  Use the other after it expires.
        {
            std::uint32_t const expry = halfResolution + expResolution +
                env.ledger->getParentCloseTimeNC();

            env (ticket::create (alice, cheri, expry));
            env (ticket::create (alice, cheri, expry));
            advanceLedger();

            std::uint32_t const aliceSeq = env.seq (alice);
            std::uint32_t const cheriSeq = env.seq (cheri);
            aliceTickets = getTicketsOnAccount (env.ledger, alice);
            expect (aliceTickets.size () == 2);
            env (pay (cheri, env.master, drops (1000)),
                tckt (aliceTickets[1]));

            // Advancing the ledger causes time to pass.  The remaining Ticket
            // should now expire.
            advanceLedger();

            STAmount const aliceOldBalance = env.balance (alice, XRP);
            STAmount const cheriOldBalance = env.balance (cheri, XRP);
            env (pay (cheri, env.master, drops (1000)), fee (baseFee),
                tckt (aliceTickets[0]), ter (tecEXPIRED_TICKET));

            advanceLedger();

            // Since the error was a 'tec' make sure that the Fee was charged.
            // Charging the Fee should also consume the Ticket.
            expect (aliceOldBalance == env.balance (alice, XRP));
            expect (cheriOldBalance == env.balance (cheri, XRP) + baseFee);
            expect (aliceSeq == env.seq (alice));
            expect (cheriSeq == env.seq (cheri));
            env.require (owners (alice, 0), tickets (alice, 0));
        }

        // See if retries really work.  To simulate a network anomaly:
        //  a. Construct a Ticket representation that isn't created yet.
        //  b. Submit a transaction using that Ticket.  Should get a `ter`.
        //  c. Advance the ledger.
        //  d. Create the Ticket.
        //  e. Advance the ledger.
        //  f. The Ticket should be consumed and the transaction completed
        {
            TicketInfo const futureTicket {alice, env.seq (alice)};

            STAmount const aliceOldBalance = env.balance (alice, XRP);
            STAmount const alicePays = drops (1000) - (2 * baseFee);
            env (pay (alice, env.master, alicePays),
                fee (baseFee), tckt (futureTicket), ter (terPRE_TICKET));
            advanceLedger();

            expect (aliceOldBalance == env.balance (alice, XRP));
            env (ticket::create (alice), fee (baseFee));
            aliceTickets = getTicketsOnAccount (env.ledger, alice);
            advanceLedger();

            env.require (owners (alice, 0), tickets (alice, 0));
            expect (aliceOldBalance == env.balance (alice, XRP) + drops (1000));
        }

        //----------------------------------------------------------------------
        //  It should not be possible to create a Ticket using a Ticket.
        env (ticket::create (alice));
        aliceTickets = getTicketsOnAccount (env.ledger, alice);
        env (ticket::create (alice),
            tckt (aliceTickets[0]), ter (temMALFORMED));

        //  Consume the Ticket so there are no leftovers for the next tests.
        env (noop (alice), tckt (aliceTickets[0]));

        //----------------------------------------------------------------------
        // Let's cancel some Tickets.
        // Create two Tickets with a Target.
        //  a. A cancel transaction from neither should fail.
        //  b. A cancel transaction from the Target should succeed.
        //  c. A cancel transaction from the owner should succeed.
        //  d. Canceling an already canceled Ticket should succeed.
        env (ticket::create (alice, cheri));
        env (ticket::create (alice, cheri));
        advanceLedger();

        aliceTickets = getTicketsOnAccount (env.ledger, alice);
        env (ticket::cancel (becky, aliceTickets[0]), ter (tefNO_PERMISSION));
        env (ticket::cancel (cheri, aliceTickets[0]));
        env (ticket::cancel (alice, aliceTickets[1]));
        advanceLedger();

        // Canceling a consumed ticket should be an error
        env.require (owners (alice, 0), tickets (alice, 0));
        env (ticket::cancel (alice, aliceTickets[0]), ter (tefNO_ENTRY));

        // The rule is that anyone, not just the owner and target, can cancel
        // an expired ticket.
        {
            std::uint32_t const now = env.ledger->getParentCloseTimeNC();
            env (ticket::create (alice, cheri, now + halfResolution));
            env (ticket::create (alice, cheri, now + halfResolution));
            env (ticket::create (alice, cheri, now + halfResolution));

            // Advancing the ledger should make all three tickets expire.
            advanceLedger();

            aliceTickets = getTicketsOnAccount (env.ledger, alice);
            env.require (owners (alice, 3), tickets (alice, 3));

            // Anyone should be able to cancel the expired Tickets.
            env (ticket::cancel (alice, aliceTickets[0]));
            env (ticket::cancel (becky, aliceTickets[1]));
            env (ticket::cancel (cheri, aliceTickets[2]));
            advanceLedger();

            aliceTickets = getTicketsOnAccount (env.ledger, alice);
            env.require (owners (alice, 0), tickets (alice, 0));
        }

        //----------------------------------------------------------------------
        // Calling TicketCancel with a Sequence of 0 should fail.
        env (ticket::create (alice));
        aliceTickets = getTicketsOnAccount (env.ledger, alice);
        advanceLedger();

        env (ticket::cancel (alice, aliceTickets[0]),
            seq (0), ter (temBAD_SEQUENCE));
        advanceLedger();

        env.require (owners (alice, 1), tickets (alice, 1));
        env (ticket::cancel (alice, aliceTickets[0]));
        advanceLedger();

        env.require (owners (alice, 0), tickets (alice, 0));

        // Try each of the transaction error ranges: tel, tem, tef, ter, tec.

        //----------------------------------------------------------------------
        // Generate a "telBAD_PUBLIC_KEY" by setting a long MessageKey.
        env (ticket::create (alice));
        aliceTickets = getTicketsOnAccount (env.ledger, alice);
        advanceLedger();

        env (set_message_key (alice,
            "012345789ABCDEF0123456789ABCDEF0123456789ABCDEF123456789ABCDEF"
            "0123456789ABCDEF0123456789ABCDEF"), tckt (aliceTickets[0]),
            ter (telBAD_PUBLIC_KEY));
        advanceLedger();

        // The tckt should be unaffected and usable.
        env (noop (alice), tckt (aliceTickets[0]));
        advanceLedger();

        env.require (owners (alice, 0), tickets (alice, 0));

        //----------------------------------------------------------------------
        // Generate a "temINVALID_FLAG" by setting funky flags.
        env (ticket::create (alice));
        aliceTickets = getTicketsOnAccount (env.ledger, alice);
        advanceLedger();

        env (noop (alice), txflags (0x80000001), ter (temINVALID_FLAG));
        advanceLedger();

        // The ticket should be unaffected and usable.
        env (noop (alice), tckt (aliceTickets[0]));
        advanceLedger();

        env.require (owners (alice, 0), tickets (alice, 0));

        //----------------------------------------------------------------------
        // Generate a tefMAX_LEDGER.
        env (ticket::create (alice));
        aliceTickets = getTicketsOnAccount (env.ledger, alice);
        advanceLedger();

        env (noop (alice), last_ledger_seq (1), ter (tefMAX_LEDGER));
        advanceLedger();

        // The ticket should be unaffected and usable.
        env (noop (alice), tckt (aliceTickets[0]));
        advanceLedger();

        env.require (owners (alice, 0), tickets (alice, 0));

        //----------------------------------------------------------------------
        // Force terINSUF_FEE_B with a transaction without funds to pay the Fee.
        Account const piker ("piker", KeyType::secp256k1);
        env.fund (XRP (200), piker);
        advanceLedger();

        env (ticket::create (alice, piker));
        aliceTickets = getTicketsOnAccount (env.ledger, alice);
        advanceLedger();

        {
            // We'll only use Tickets, so piker's sequence should not change.
            std::uint32_t const aliceSeq = env.seq (alice);
            std::uint32_t const pikerSeq = env.seq (piker);

            // Give piker a transaction with a fee higher than the balance.
            env (noop (piker), fee (drops (200001000)),
                tckt (aliceTickets[0]), ter (terINSUF_FEE_B));

            // Let the transaction circulate a few ledgers.
            advanceLedger();
            advanceLedger();
            advanceLedger();
            expect (env.balance (piker, XRP) == XRP (200));
            expect (env.seq (piker) == pikerSeq);
            env.require (owners (alice, 1), tickets (alice, 1));

            // Fund piker enough to pay the fee.
            env (pay (env.master, piker, drops (1020)));
            advanceLedger();
            expect (env.balance (piker, XRP) == drops (20));
            expect (env.seq (alice) == aliceSeq);
            expect (env.seq (piker) == pikerSeq);
            env.require (owners (alice, 0), tickets (alice, 0));

            // Just on principle, advance a few more times.  There used to
            // be a problem that the retry would re-apply and cause havoc.
            advanceLedger();
            advanceLedger();
            expect (env.balance (piker, XRP) == drops (20));
            expect (env.seq (alice) == aliceSeq);
            expect (env.seq (piker) == pikerSeq);
            env.require (owners (alice, 0), tickets (alice, 0));
        }

        //----------------------------------------------------------------------
        // Cause an tecUNFUNDED_PAYMENT since piker hasn't got the funds.
        env (ticket::create (alice, piker));
        aliceTickets = getTicketsOnAccount (env.ledger, alice);
        advanceLedger();

        {
            // We'll only use Tickets, so piker's sequence should not change.
            std::uint32_t const aliceSeq = env.seq (alice);
            std::uint32_t const pikerSeq = env.seq (piker);

            env (pay (piker, env.master, drops (1000)),
                tckt (aliceTickets[0]),
                fee (drops (10)),
                ter (tecUNFUNDED_PAYMENT));
            advanceLedger();

            // alice's ticket should be consumed by the 'tec'.
            env.require (owners (alice, 0), tickets (alice, 0));

            // piker's balance should be reduced by the fee.
            expect (env.balance (piker, XRP) == drops (10));

            // Nobody's sequences should have moved.
            expect (env.seq (alice) == aliceSeq);
            expect (env.seq (piker) == pikerSeq);
        }
        env.require (owners (alice, 0), owners (becky, 0), owners (cheri, 0));
        env.require (owners (piker, 0));
    }

    void
    run()
    {
        testTicket();
    }
};

BEAST_DEFINE_TESTSUITE(Ticket,app,ripple)

} // test
} // ripple
