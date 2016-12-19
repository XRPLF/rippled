# Consensus Algorithm

This directory holds the types and classes needed
to connect consensus to rippled.

## Types

All types must be copy constructible and assignable.

* `LgrID_t`
  Represents a ledger identifier.
  Typically a 256-bit hash of the ledger header.

* `TxID_t`
  Represents a transaction identifier.
  Typically a 256-bit hash of the transaction data.

* `TxSetID_t`
  Represents an identifier of a set of transactions.
  Typically a 256-bit hash of the set's root tree node.

* `NodeID_t`
  Represents an identifier for a node that can take positions during
  the consenus process.

* `Time_t`
  Encodes absolute times. Used for the close times of ledgers and the
  expiration times of positions.

* `Pos_t`
  Represents a position on a consensus taken by a participant.
  Typically it encodes the previous ledger identifier, the transaction
  set identifier, the participant, and a sequence number. It also includes
  either the time it was signed or the time it was first seen. It may also
  include additional information such as the participant's public key or
  signature

* `Tx_t`
  Represent a transaction. Has an identifier and also whatever information
  is needed to add it to a set.

* `TxSet_t`
  Represents a set of transactions. It has an identifier and can report
  which transactions it has and provide the actual transaction data.
  If non-const, it can be modified.


* `Callback_t`
  Supplies the callouts that the consensus logic makes to request
  information or inform of changes.

## `Pos_t`

Represents a position taken by a validator during a consensus round.
Must provide:

static std::uint32_t seqInitial;

static std::uint32_t seqLeave;

std::uint32_t getSequence() const;

Time_t getCloseTime() const;

Time_t getSeenTime() const;

bool isStale (Time_t) const;

NodeID_t getNodeID() const;

TxSetID_t getPosition() const;

LgrID_t getPrevLedger() const;

bool isInitial() const;

bool isBowOut() const;

Json::Value getJson() const;

bool changePosition (TxSetID_t const& position, Time_t closeTime, Time_t now);

bool bowOut (Time_t now);


### `Tx_t`

Represents a transaction.
Must provide:

TxID_t getID() const;


### TxSet_t

Represents a set of transactions.
Must provide:

TxSet_t (TxSet_t::mutable_t const&);

TxSetID_t getID() const;

bool hasEntry (TxID_t const&) const;

bool hasEntry (Tx_t const&) const;

boost::optional <Tx_t const> const getEntry (TxID_t const&) const;

std::map <TxID_t, bool> getDifferences(TxSet_t const&) const;

## TxSet_t::mutable_t

Represents a set of transactions that can be modified.
Must provide:

TxSet_t::mutable_t (TxSet_t const &);

bool addEntry (Tx_t const&);

bool removeEntry (TxID_t const&);


## Callback_t

Must provide the following functions the consensus algorithm may need toinvoke

std::pair <LgrID_t, bool> getLCL (boost::optional<LgrID_t>);
Returns the current LCL ID and whether or not that ledger is available.
Takes an optional favored ledger

void consensusViewChange ();
Reports a change in the view of the consensus process

bool anyTX ();
Returns true if there are any pending transactions in the open ledger

int getNodesAfter (LgrID_t const&);
Returns the number of trusted validators that have passed the specified
ledger

doAccept

newTX

newProposal

newStatus

makeInitialPosition

replayProposals

relayProposal

validationCheck
