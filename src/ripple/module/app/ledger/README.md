
# Ledger Process

## Introduction

## Life Cycle

Every server always has an open ledger. All received new transactions are
applied to the open ledger. The open ledger can't close until we reach
a consensus on the previous ledger, and either: there is at least one
transaction in the open ledger, or the ledger's close time has been reached.

When the open ledger is closed the transactions in the open ledger become
the initial proposal. Validators will send the proposal (non-validators will
simply not send the proposal). The open ledger contains the set of transactions
the server thinks should go into the next ledger.

Once the ledger is closed, servers avalanche to consensus on the candidate
transaction set and the close time. When consensus is reached, servers build
a new last closed ledger by starting with the previous last closed ledger and
applying the consensus transaction set. In the normal case, servers will all
agree on both the last closed ledger and the consensus transaction set. The
goal is to give the network the highest chances of arriving at the same
conclusion on all servers.

At all times during the consensus process the open ledger remains open with the
same transaction set, and has new transactions applied. It will likely have
transactions that are also in the new last closed ledger. Valid transactions
received during the consensus process will only be in the open ledger.

Validators now publish validations of the new last closed ledger. Servers now
build a new open ledger from the last closed ledger. First, all transactions
that were candidates in the previous consensus round but didn't make it into
the consensus set are applied. Next, transactions in the current open ledger
are applied. This covers transactions received during the previous consensus
round. This is a "rebase": now that we know the real history, the current open
ledger is rebased against the last closed ledger.

The purpose of the open ledger is as follows:
- Forms the basis of the initial proposal during consensus
- Used to decide if we can reject the transaction without relaying it

## Byzantine Failures

Byzantine failures are resolved as follows. If there is a supermajority ledger,
then a minority of validators will discover that the consensus round is
proceeding on a different ledger than they thought. These validators will
become desynced, and switch to a strategy of trying to acquire the consensus
ledger.

If there is no majority ledger, then starting on the next consensus round there
will not be a consensus on the last closed ledger. Another avalanche process
is started.

## Validators

The only meaningful difference between a validator and a 'regular' server is
that the validator sends its proposals and validations to the network.

---

# Definitions

## Open Ledger

The open ledger is the ledger that the server applies all new incoming
transactions to.

## Last Validated Ledger

The most recent ledger that the server is certain will always remain part
of the permanent, public history.

## Last Closed Ledger

The most recent ledger that the server believes the network reached consensus
on. Different servers can arrive at a different conclusion about the last
closed ledger. This is a consequence of Byzantanine failure. The purpose of
validations is to resolve the differences between servers and come to a common
conclusion about which last closed ledger is authoritative.

## Consensus

A distributed agreement protocol. Ripple uses the consensus process to solve
the problem of double-spending.

## Validation

A signed statement indicating that it built a particular ledger as a result
of the consensus process.

## Proposal

A signed statement of which transactions it believes should be included in
the next consensus ledger.

## Ledger Header ##

The "ledger header" is the chunk of data that hashes to the
ledger's hash. It contains the sequence number, parent hash,
hash of the previous ledger, hash of the root node of the
state tree, and so on.

## Ledger Base ##

The term "ledger base" refers to a particular type of query
and response used in the ledger fetch process that includes
the ledger header but may also contain other information
such as the root node of the state tree.

---

# Ledger Structures #

## Account Root ##

**Account:** A 160-bit account ID.

**Balance:** Balance in the account.

**Flags:** ???

**LedgerEntryType:** "AccountRoot"

**OwnerCount:** The number of items the account owns that are charged to the
account.  Offers are charged to the account.  Trust lines may be charged to
the account (but not necessarily).  The OwnerCount determines the reserve on
the account.

**PreviousTxnID:** 256-bit index of the previous transaction on this account.

**PreviousTxnLgrSeq:** Ledger number sequence number of the previous
transaction on this account.

**Sequence:** Must be a value of 1 for the account to process a valid
transaction.  The value initially matches the sequence number of the state
tree of the account that signed the transaction.  The process of executing
the transaction increments the sequence number.  This is how ripple prevents
a transaction from executing more than once.

**index:** 256-bit hash of this AccountRoot.


## Trust Line ##

The trust line acts as an edge connecting two accounts: the accounts
represented by the HighNode and the LowNode.  Which account is "high" and
"low" is determined by the values of the two 160-bit account IDs.  The
account with the smaller 160-bit ID is always the low account.  This
ordering makes the hash of a trust line between accounts A and B have the
same value as a trust line between accounts B and A.

**Balance:**
 - **currency:** String identifying a valid currency, e.g., "BTC".
 - **issuer:** There is no issuer, really, this entry is "NoAccount".
 - **value:** 

**Flags:** ???

**HighLimit:**
 - **currency:** Same as for Balance.
 - **issuer:** A 160-bit account ID.
 - **value:** The largest amount this issuer will accept of the currency.

**HighNode:** A deletion hint.

**LedgerEntryType:** "RippleState".

**LowLimit:**
 - **currency:** Same as for Balance.
 - **issuer:** A 160-bit account ID.
 - **value:** The largest amount of the currency this issuer will accept.

**LowNode:** A deletion hint

**PreviousTxnID:** 256-bit hash of the previous transaction on this account.

**PreviousTxnLgrSeq:** Ledger number sequence number of the previous
transaction on this account.

**index:** 256-bit hash of this RippleState.


## Ledger Hashes ##

**Flags:** ???

**Hashes:** A list of the hashes of the previous 256 ledgers.

**LastLedgerSequence:**

**LedgerEntryType:** "LedgerHashes".

**index:** 256-bit hash of this LedgerHashes.


## Owner Directory ##

Lists all of the offers and trust lines that are associated with an account.

**Flags:** ???

**Indexes:** A list of hashes of items owned by the account.

**LedgerEntryType:** "DirectoryNode".

**Owner:** 160-bit ID of the owner account.

**RootIndex:**

**index:** A hash of the owner account.


## Book Directory ##

Lists one or more offers that have the same quality.

If a pair of Currency and Issuer fields are all zeros, then that pair is
dealing in XRP.

The code, at the moment, does not recognize that the Currency and Issuer
fields are currencies and issuers.  So those values are presented in hex,
rather than as accounts and currencies.  That's a bug and should be fixed
at some point.

**ExchangeRate:** A 64-bit value.  The first 8-bits is the exponent and the
remaining bits are the mantissa.  The format is such that a bigger 64-bit
value always represents a higher exchange rate.

Each type can compute its own hash.  The hash of a book directory contains,
as its lowest 64 bits, the exchange rate.  This means that if there are
multiple *almost* identical book directories, but with different exchange
rates, then these book directories will sit together in the ledger.  The best
exchange rate will be the first in the sequence of Book Directories.

**Flags:** ???

**Indexes:** 256-bit hashes of offers that match the exchange rate and
currencies described by this BookDirectory.

**LedgerEntryType:** "DirectoryNode".

**RootIndex:** Always identical to the index.

**TakerGetsCurrency:** Type of currency taker receives.

**TakerGetsIssuer:** Issuer of the GetsCurrency.

**TakerPaysCurrency:** Type of currency taker pays.

**TakerPaysIssuer:** Issuer of the PaysCurrency.

**index:** A 256-bit hash computed using the TakerGetsCurrency, TakerGetsIssuer,
TakerPaysCurrency, and TakerPaysIssuer in the top 192 bits.  The lower 64-bits
are occupied by the exchange rate.


---

# References #

