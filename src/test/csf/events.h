#ifndef XRPL_TEST_CSF_EVENTS_H_INCLUDED
#define XRPL_TEST_CSF_EVENTS_H_INCLUDED

#include <test/csf/Proposal.h>
#include <test/csf/Tx.h>
#include <test/csf/Validation.h>
#include <test/csf/ledgers.h>

#include <chrono>

namespace ripple {
namespace test {
namespace csf {

// Events are emitted by peers at a variety of points during the simulation.
// Each event is emitted by a particular peer at a particular time. Collectors
// process these events, perhaps calculating statistics or storing events to
// a log for post-processing.
//
// The Event types can be arbitrary, but should be copyable and lightweight.
//
// Example collectors can be found in collectors.h, but have the general
// interface:
//
// @code
//     template <class T>
//     struct Collector
//     {
//        template <class Event>
//        void
//        on(peerID who, SimTime when, Event e);
//     };
// @endcode
//
// CollectorRef.f defines a type-erased holder for arbitrary Collectors.  If
// any new events are added, the interface there needs to be updated.

/** A value to be flooded to all other peers starting from this peer.
 */
template <class V>
struct Share
{
    //! Event that is shared
    V val;
};

/** A value relayed to another peer as part of flooding
 */
template <class V>
struct Relay
{
    //! Peer relaying to
    PeerID to;

    //! The value to relay
    V val;
};

/** A value received from another peer as part of flooding
 */
template <class V>
struct Receive
{
    //! Peer that sent the value
    PeerID from;

    //! The received value
    V val;
};

/** A transaction submitted to a peer */
struct SubmitTx
{
    //! The submitted transaction
    Tx tx;
};

/** Peer starts a new consensus round
 */
struct StartRound
{
    //! The preferred ledger for the start of consensus
    Ledger::ID bestLedger;

    //! The prior ledger on hand
    Ledger prevLedger;
};

/** Peer closed the open ledger
 */
struct CloseLedger
{
    // The ledger closed on
    Ledger prevLedger;

    // Initial txs for including in ledger
    TxSetType txs;
};

//! Peer accepted consensus results
struct AcceptLedger
{
    // The newly created ledger
    Ledger ledger;

    // The prior ledger (this is a jump if prior.id() != ledger.parentID())
    Ledger prior;
};

//! Peer detected a wrong prior ledger during consensus
struct WrongPrevLedger
{
    // ID of wrong ledger we had
    Ledger::ID wrong;
    // ID of what we think is the correct ledger
    Ledger::ID right;
};

//! Peer fully validated a new ledger
struct FullyValidateLedger
{
    //! The new fully validated ledger
    Ledger ledger;

    //! The prior fully validated ledger
    //! This is a jump if prior.id() != ledger.parentID()
    Ledger prior;
};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
