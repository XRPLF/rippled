
# Ledger Process #

## Introduction ##

## Life Cycle ##

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

## Byzantine Failures ##

Byzantine failures are resolved as follows. If there is a supermajority ledger,
then a minority of validators will discover that the consensus round is
proceeding on a different ledger than they thought. These validators will
become desynced, and switch to a strategy of trying to acquire the consensus
ledger.

If there is no majority ledger, then starting on the next consensus round there
will not be a consensus on the last closed ledger. Another avalanche process
is started.

## Validators ##

The only meaningful difference between a validator and a 'regular' server is
that the validator sends its proposals and validations to the network.

---

# The Ledger Stream #

## Ledger Priorities ##

There are two ledgers that are the most important for a rippled server to have:

 - The consensus ledger and
 - The last validated ledger.

If we need either of those two ledgers they are fetched with the highest
priority.  Also, when they arrive, they replace their earlier counterparts
(if they exist).

The `LedgerMaster` object tracks
 - the last published ledger,
 - the last validated ledger, and
 - ledger history.
So the `LedgerMaster` is at the center of fetching historical ledger data.

In specific, the `LedgerMaster::doAdvance()` method triggers the code that
fetches historical data and controls the state machine for ledger acquisition.

The server tries to publish an on-going stream of consecutive ledgers to its
clients.  After the server has started and caught up with network
activity, say when ledger 500 is being settled, then the server puts its best
effort into publishing validated ledger 500 followed by validated ledger 501
and then 502.  This effort continues until the server is shut down.

But loading or network connectivity may sometimes interfere with that ledger
stream.  So suppose the server publishes validated ledger 600 and then
receives validated ledger 603.  Then the server wants to back fill its ledger
history with ledgers 601 and 602.

The server prioritizes keeping up with current ledgers.  But if it is caught
up on the current ledger, and there are no higher priority demands on the
server, then it will attempt to back fill its historical ledgers.  It fills
in the historical ledger data first by attempting to retrieve it from the
local database.  If the local database does not have all of the necessary data
then the server requests the remaining information from network peers.

Suppose the server is missing multiple historical ledgers.  Take the previous
example where we have ledgers 603 and 600, but we're missing 601 and 602.  In
that case the server requests information for ledger 602 first, before
back-filling ledger 601.  We want to expand the contiguous range of
most-recent ledgers that the server has locally.  There's also a limit to
how much historical ledger data is useful.  So if we're on ledger 603, but
we're missing ledger 4 we may not bother asking for ledger 4.

## Assembling a Ledger ##

When data for a ledger arrives from a peer, it may take a while before the
server can apply that data.  So when ledger data arrives we schedule a job
thread to apply that data.  If more data arrives before the job starts we add
that data to the job.  We defer requesting more ledger data until all of the
data we have for that ledger has been processed.  Once all of that data is
processed we can intelligently request only the additional data that we need
to fill in the ledger.  This reduces network traffic and minimizes the load
on peers supplying the data.

If we receive data for a ledger that is not currently under construction,
we don't just throw the data away.  In particular the AccountStateNodes
may be useful, since they can be re-used across ledgers.  This data is
stashed in memory (not the database) where the acquire process can find
it.

Peers deliver ledger data in the order in which the data can be validated.
Data arrives in the following order:

 1. The hash of the ledger header
 2. The ledger header
 3. The root nodes of the transaction tree and state tree
 4. The lower (non-root) nodes of the state tree
 5. The lower (non-root) nodes of the transaction tree

Inner-most nodes are supplied before outer nodes.  This allows the
requesting server to hook things up (and validate) in the order in which
data arrives.

If this process fails, then a server can also ask for ledger data by hash,
rather than by asking for specific nodes in a ledger.  Asking for information
by hash is less efficient, but it allows a peer to return the information
even if the information is not assembled into a tree.  All the peer needs is
the raw data.

## Which Peer To Ask ##

Peers go though state transitions as the network goes through its state
transitions.  Peer's provide their state to their directly connected peers.
By monitoring the state of each connected peer a server can tell which of
its peers has the information that it needs.

Therefore if a server suffers a byzantine failure the server can tell which
of its peers did not suffer that same failure.  So the server knows which
peer(s) to ask for the missing information.

Peers also report their contiguous range of ledgers.  This is another way that
a server can determine which peer to ask for a particular ledger or piece of
a ledger.

There are also indirect peer queries.  If there have been timeouts while
acquiring ledger data then a server may issue indirect queries.  In that
case the server receiving the indirect query passes the query along to any
of its peers that may have the requested data.  This is important if the
network has a byzantine failure.  If also helps protect the validation
network.  A validator may need to get a peer set from one of the other
validators, and indirect queries improve the likelihood of success with
that.

## Kinds of Fetch Packs ##

A FetchPack is the way that peers send partial ledger data to other peers
so the receiving peer can reconstruct a ledger.

A 'normal' FetchPack is a bucket of nodes indexed by hash.  The server
building the FetchPack puts information into the FetchPack that the
destination server is likely to need.  Normally they contain all of the
missing nodes needed to fill in a ledger.

A 'compact' FetchPack, on the other hand, contains only leaf nodes, no
inner nodes.  Because there are no inner nodes, the ledger information that
it contains cannot be validated as the ledger is assembled.  We have to,
initially, take the accuracy of the FetchPack for granted and assemble the
ledger.  Once the entire ledger is assembled the entire ledger can be
validated.  But if the ledger does not validate then there's nothing to be
done but throw the entire FetchPack away; there's no way to save a portion
of the FetchPack.

The FetchPacks just described could be termed 'reverse FetchPacks.'  They
only provide historical data.  There may be a use for what could be called a
'forward FetchPack.'  A forward FetchPack would contain the information that
is needed to build a new ledger out of the preceding ledger.

A forward compact FetchPack would need to contain:
 - The header for the new ledger,
 - The leaf nodes of the transaction tree (if there is one),
 - The index of deleted nodes in the state tree,
 - The index and data for new nodes in the state tree, and
 - The index and new data of modified nodes in the state tree.

---

# Definitions #

## Open Ledger ##

The open ledger is the ledger that the server applies all new incoming
transactions to.

## Last Validated Ledger ##

The most recent ledger that the server is certain will always remain part
of the permanent, public history.

## Last Closed Ledger ##

The most recent ledger that the server believes the network reached consensus
on. Different servers can arrive at a different conclusion about the last
closed ledger. This is a consequence of Byzantanine failure. The purpose of
validations is to resolve the differences between servers and come to a common
conclusion about which last closed ledger is authoritative.

## Consensus ##

A distributed agreement protocol. Ripple uses the consensus process to solve
the problem of double-spending.

## Validation ##

A signed statement indicating that it built a particular ledger as a result
of the consensus process.

## Proposal ##

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

# Ledger Publication #

## Overview ##

The Ripple server permits clients to subscribe to a continuous stream of
fully-validated ledgers. The publication code maintains this stream.

The server attempts to maintain this continuous stream unless it falls
too far behind, in which case it jumps to the current fully-validated
ledger and then attempts to resume a continuous stream.

## Implementation ##

`LedgerMaster::doAdvance` is invoked when work may need to be done to
publish ledgers to clients. This code loops until it cannot make further
progress.

`LedgerMaster::findNewLedgersToPublish` is called first. If the last
fully-valid ledger's sequence number is greater than the last published
ledger's sequence number, it attempts to publish those ledgers, retrieving
them if needed.

If there are no new ledgers to publish, `doAdvance` determines if it can
backfill history. If the publication is not caught up, backfilling is not
attempted to conserve resources.

If history can be backfilled, the missing ledger with the highest
sequence number is retrieved first. If a historical ledger is retrieved,
and its predecessor is in the database, `tryFill` is invoked to update
the list of resident ledgers.

---

# The Ledger Cleaner #

## Overview ##

The ledger cleaner checks and, if necessary, repairs the SQLite ledger and
transaction databases.  It can also check for pieces of a ledger that should
be in the node back end but are missing.  If it detects this case, it
triggers a fetch of the ledger.  The ledger cleaner only operates by manual
request. It is never started automatically.

## Operations ##

The ledger cleaner can operate on a single ledger or a range of ledgers. It
always validates the ledger chain itself, ensuring that the SQLite database
contains a consistent chain of ledgers from the last validated ledger as far
back as the database goes.

If requested, it can additionally repair the SQLite entries for transactions
in each checked ledger.  This was primarily intended to repair incorrect
entries created by a bug (since fixed) that could cause transasctions from a
ledger other than the fully-validated ledger to appear in the SQLite
databases in addition to the transactions from the correct ledger.

If requested, it can additionally check the ledger for missing entries
in the account state and transaction trees.

To prevent the ledger cleaner from saturating the available I/O bandwidth
and excessively polluting caches with ancient information, the ledger
cleaner paces itself and does not attempt to get its work done quickly.

## Commands ##

The ledger cleaner can be controlled and monitored with the **ledger_cleaner**
RPC command. With no parameters, this command reports on the status of the
ledger cleaner. This includes the range of ledgers it has been asked to process,
the checks it is doing, and the number of errors it has found.

The ledger cleaner can be started, stopped, or have its behavior changed by
the following RPC parameters:

**stop**: Stops the ledger cleaner gracefully

**ledger**: Asks the ledger cleaner to clean a specific ledger, by sequence number

**min_ledger**, **max_ledger**: Sets or changes the range of ledgers cleaned

**full**: Requests all operations to be done on the specified ledger(s)

**fix_txns**: A boolean indicating whether to replace the SQLite transaction
entries unconditionally

**check_nodes**: A boolean indicating whether to check the specified
ledger(s) for missing nodes in the back end node store

---

# References #

