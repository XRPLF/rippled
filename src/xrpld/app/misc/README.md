# Fee Voting

The Ripple payment protocol enforces a fee schedule expressed in units of the
native currency, XRP. Fees for transactions are paid directly from the account
owner. There are also reserve requirements for each item that occupies storage
in the ledger. The reserve fee schedule contains both a per-account reserve,
and a per-owned-item reserve. The items an account may own include active
offers, trust lines, and tickets.

Validators may vote to increase fees if they feel that the network is charging
too little. They may also vote to decrease fees if the fees are too costly
relative to the value the network provides. One common case where a validator
may want to change fees is when the value of the native currency XRP fluctuates
relative to other currencies.

The fee voting mechanism takes place every 256 ledgers ("voting ledgers"). In
a voting ledger, each validator takes a position on what they think the fees
should be. The consensus process converges on the majority position, and in
subsequent ledgers a new fee schedule is enacted.

## Consensus

The Ripple consensus algorithm allows distributed participants to arrive at
the same answer for yes/no questions. The canonical case for consensus is
whether or not a particular transaction is included in the ledger. Fees
present a more difficult challenge, since the decision on the new fee is not
a yes or no question.

To convert validators' positions on fees into a yes or no question that can
be converged in the consensus process, the following algorithm is used:

- In the ledger before a voting ledger, validators send proposals which also
  include the values they think the network should use for the new fee schedule.

- In the voting ledger, validators examine the proposals from other validators
  and choose a new fee schedule which moves the fees in a direction closer to
  the validator's ideal fee schedule and is also likely to be accepted. A fee
  amount is likely to be accepted if a majority of validators agree on the
  number.

- Each validator injects a "pseudo transaction" into their proposed ledger
  which sets the fees to the chosen schedule.

- The consensus process is applied to these fee-setting transactions as normal.
  Each transaction is either included in the ledger or not. In most cases, one
  fee setting transaction will make it in while the others are rejected. In
  some rare cases more than one fee setting transaction will make it in. The
  last one to be applied will take effect. This is harmless since a majority
  of validators still agreed on it.

- After the voting ledger has been validated, future pseudo transactions
  before the next voting ledger are rejected as fee setting transactions may
  only appear in voting ledgers.

## Configuration

A validating instance of rippled uses information in the configuration file
to determine how it wants to vote on the fee schedule. It is the responsibility
of the administrator to set these values.

---

# Amendment

An Amendment is a new or proposed change to a ledger rule. Ledger rules affect
transaction processing and consensus; peers must use the same set of rules for
consensus to succeed, otherwise different instances of rippled will get
different results. Amendments can be almost anything but they must be accepted
by a network majority through a consensus process before they are utilized. An
Amendment must receive at least an 80% approval rate from validating nodes for
a period of two weeks before being accepted. The following example outlines the
process of an Amendment from its conception to approval and usage.

*  A community member proposes to change transaction processing in some way.
  The proposal is discussed amongst the community and receives its support
  creating a community or human consensus.

*  Some members contribute their time and work to develop the Amendment.

*  A pull request is created and the new code is folded into a rippled build
  and made available for use.

*  The consensus process begins with the validating nodes.

*  If the Amendment holds an 80% majority for a two week period, nodes will begin
  including the transaction to enable it in their initial sets.

Nodes may veto Amendments they consider undesirable by never announcing their
support for those Amendments. Just a few nodes vetoing an Amendment will normally
keep it from being accepted. Nodes could also vote yes on an Amendments even
before it obtains a super-majority. This might make sense for a critical bug fix.

Validators that support an amendment that is not yet enabled announce their
support in their validations. If 80% support is achieved, they will introduce
a pseudo-transaction to track the amendment's majority status in the ledger.

If an amendment whose majority status is reported in a ledger loses that
majority status, validators will introduce pseudo-transactions to remove
the majority status from the ledger.

If an amendment holds majority status for two weeks, validators will
introduce a pseudo-transaction to enable the amendment.

All amendments are assumed to be critical and irreversible. Thus there
is no mechanism to disable or revoke an amendment, nor is there a way
for a server to operate while an amendment it does not understand is
enabled.

---

# SHAMapStore: Online Delete

Optional online deletion happens through the SHAMapStore. Records are deleted
from disk based on ledger sequence number. These records reside in the
key-value database  as well as in the SQLite ledger and transaction databases.
Without online deletion storage usage grows without bounds. It can only
be pruned by stopping, manually deleting data, and restarting the server.
Online deletion requires less operator intervention to manage the server.

The main mechanism to delete data from the key-value database is to keep two
databases open at all times. One database has all writes directed to it. The
other database has recent archival data from just prior to that from the current
writable database.
Upon rotation, the archival database is deleted. The writable database becomes
archival, and a brand new database becomes writable. To ensure that no
necessary data for transaction processing is lost, a variety of steps occur,
including copying the contents of an entire ledger's account state map,
clearing caches, and copying the contents of (freshening) other caches.

Deleting from SQLite involves more straight-forward SQL DELETE queries from
the respective tables, with a rudimentary back-off algorithm to do portions
of the deletions at a time. This back-off is in place so that the database
lock is not held excessively. The SQLite database is not configured to
delete on-disk storage, so it will grow over time. However, with online delete
enabled, it grows at a very small rate compared with the key-value store.

The online delete routine aborts its current activities if it fails periodic
server health checks. This minimizes impact of I/O and locking of critical
objects. If interrupted, the routine will start again at the next validated
ledger close. Likewise, the routine will continue in a similar fashion if the
server restarts.

Configuration:

* In the [node_db] configuration section, an optional online_delete parameter is
set. If not set or if set to 0, online delete is disabled. Otherwise, the
setting defines number of ledgers between deletion cycles.
* Another optional parameter in [node_db] is that for advisory_delete. It is
disabled by default. If set to non-zero, requires an RPC call to activate the
deletion routine.
* online_delete must not be greater than the [ledger_history] parameter.
* [fetch_depth] will be silently set to equal the online_delete setting if
online_delete is greater than fetch_depth.
* In the [node_db] section, there is a performance tuning option, delete_batch,
which sets the maximum size in ledgers for each SQL DELETE query.
