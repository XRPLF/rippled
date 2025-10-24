# Fees

Rippled's fee mechanism consists of several interrelated processes:

1. [Rapid Fee escalation](#fee-escalation)
2. [The Transaction Queue](#transaction-queue)

## Fee Escalation

The guiding principal of fee escalation is that when things are going
smoothly, fees stay low, but as soon as high levels of traffic appear
on the network, fees will grow quickly to extreme levels. This should
dissuade malicious users from abusing the system, while giving
legitimate users the ability to pay a higher fee to get high-priority
transactions into the open ledger, even during unfavorable conditions.

How fees escalate:

1. There is a base [fee level](#fee-level) of 256,
   which is the minimum that a typical transaction
   is required to pay. For a [reference
   transaction](#reference-transaction), that corresponds to the
   network base fee, which is currently 10 drops.
2. However, there is a limit on the number of transactions that
   can get into an open ledger for that base fee level. The limit
   will vary based on the [health](#consensus-health) of the
   consensus process, but will be at least [5](#other-constants).

- If consensus stays [healthy](#consensus-health), the limit will
  be the max of the number of transactions in the validated ledger
  plus [20%](#other-constants) or the current limit until it gets
  to [50](#other-constants), at which point, the limit will be the
  largest number of transactions plus [20%](#other-constants)
  in the last [20](#other-constants) validated ledgers which had
  more than [50](#other-constants) transactions. Any time the limit
  decreases (i.e. a large ledger is no longer recent), the limit will
  decrease to the new largest value by 10% each time the ledger has
  more than 50 transactions.
- If consensus does not stay [healthy](#consensus-health),
  the limit will clamp down to the smaller of the number of
  transactions in the validated ledger minus [50%](#other-constants)
  or the previous limit minus [50%](#other-constants).
- The intended effect of these mechanisms is to allow as many base fee
  level transactions to get into the ledger as possible while the
  network is [healthy](#consensus-health), but to respond quickly to
  any condition that makes it [unhealthy](#consensus-health), including,
  but not limited to, malicious attacks.

3. Once there are more transactions in the open ledger than indicated
   by the limit, the required fee level jumps drastically.

- The formula is `( lastLedgerMedianFeeLevel *
TransactionsInOpenLedger^2 / limit^2 )`,
  and returns a [fee level](#fee-level).

4. That may still be pretty small, but as more transactions get
   into the ledger, the fee level increases exponentially.

- For example, if the limit is 6, and the median fee is minimal,
  and assuming all [reference transactions](#reference-transaction),
  the 8th transaction only requires a [level](#fee-level) of about 174,000
  or about 6800 drops,
  but the 20th transaction requires a [level](#fee-level) of about
  1,283,000 or about 50,000 drops.

5. Finally, as each ledger closes, the median fee level of that ledger is
   computed and used as `lastLedgerMedianFeeLevel` (with a
   [minimum value of 128,000](#other-constants))
   in the fee escalation formula for the next open ledger.

- Continuing the example above, if ledger consensus completes with
  only those 20 transactions, and all of those transactions paid the
  minimum required fee at each step, the limit will be adjusted from
  6 to 24, and the `lastLedgerMedianFeeLevel` will be about 322,000,
  which is 12,600 drops for a
  [reference transaction](#reference-transaction).
- This will only require 10 drops for the first 25 transactions,
  but the 26th transaction will require a level of about 349,150
  or about 13,649 drops.

- This example assumes a cold-start scenario, with a single, possibly
  malicious, user willing to pay arbitrary amounts to get transactions
  into the open ledger. It ignores the effects of the [Transaction
  Queue](#transaction-queue). Any lower fee level transactions submitted
  by other users at the same time as this user's transactions will go into
  the transaction queue, and will have the first opportunity to be applied
  to the _next_ open ledger. The next section describes how that works in
  more detail.

## Transaction Queue

An integral part of making fee escalation work for users of the network
is the transaction queue. The queue allows legitimate transactions to be
considered by the network for future ledgers if the escalated open
ledger fee gets too high. This allows users to submit low priority
transactions with a low fee, and wait for high fees to drop. It also
allows legitimate users to continue submitting transactions during high
traffic periods, and give those transactions a much better chance to
succeed.

1. If an incoming transaction meets both the base [fee
   level](#fee-level) and the [load fee](#load-fee) minimum, but does not have a high
   enough [fee level](#fee-level) to immediately go into the open ledger,
   it is instead put into the queue and broadcast to peers. Each peer will
   then make an independent decision about whether to put the transaction
   into its open ledger or the queue. In principle, peers with identical
   open ledgers will come to identical decisions. Any discrepancies will be
   resolved as usual during consensus.
2. When consensus completes, the open ledger limit is adjusted, and
   the required [fee level](#fee-level) drops back to the base
   [fee level](#fee-level). Before the ledger is made available to
   external transactions, transactions are applied from the queue to the
   ledger from highest [fee level](#fee-level) to lowest. These transactions
   count against the open ledger limit, so the required [fee level](#fee-level)
   may start rising during this process.
3. Once the queue is empty, or the required [fee level](#fee-level)
   rises too high for the remaining transactions in the queue, the ledger
   is opened up for normal transaction processing.
4. A transaction in the queue can stay there indefinitely in principle,
   but in practice, either

- it will eventually get applied to the ledger,
- it will attempt to apply to the ledger and fail,
- it will attempt to apply to the ledger and retry [10
  times](#other-constants),
- its last ledger sequence number will expire,
- the user will replace it by submitting another transaction with the same
  sequence number and at least a [25% higher fee](#other-constants), or
- it will get dropped when the queue fills up with more valuable transactions.
  The size limit is computed dynamically, and can hold transactions for
  the next [20 ledgers](#other-constants) (restricted to a minimum of
  [2000 transactions](#other-constants)). The lower the transaction's
  fee, the more likely that it will get dropped if the network is busy.

If a transaction is submitted for an account with one or more transactions
already in the queue, and a sequence number that is sequential with the other
transactions in the queue for that account, it will be considered
for the queue if it meets these additional criteria:

- the account has fewer than [10](#other-constants) transactions
  already in the queue.
- all other queued transactions for that account, in the case where
  they spend the maximum possible XRP, leave enough XRP balance to pay
  the fee,
- the total fees for the other queued transactions are less than both
  the network's minimum reserve and the account's XRP balance, and
- none of the prior queued transactions affect the ability of subsequent
  transactions to claim a fee.

Currently, there is an additional restriction that the queue cannot work with
transactions using the `sfPreviousTxnID` or `sfAccountTxnID` fields.
`sfPreviousTxnID` is deprecated and shouldn't be used anyway. Future
development will make the queue aware of `sfAccountTxnID` mechanisms.

## Technical Details

### Fee Level

"Fee level" is used to allow the cost of different types of transactions
to be compared directly. For a [reference
transaction](#reference-transaction), the base fee
level is 256. If a transaction is submitted with a higher `Fee` field,
the fee level is scaled appropriately.

Examples, assuming a [reference transaction](#reference-transaction)
base fee of 10 drops:

1. A single-signed [reference transaction](#reference-transaction)
   with `Fee=20` will have a fee level of
   `20 drop fee * 256 fee level / 10 drop base fee = 512 fee level`.
2. A multi-signed [reference transaction](#reference-transaction) with
   3 signatures (base fee = 40 drops) and `Fee=60` will have a fee level of
   `60 drop fee * 256 fee level / ((1tx + 3sigs) * 10 drop base fee) = 384
fee level`.
3. A hypothetical future non-reference transaction with a base
   fee of 15 drops multi-signed with 5 signatures and `Fee=90` will
   have a fee level of
   `90 drop fee * 256 fee level / ((1tx + 5sigs) * 15 drop base fee) = 256
fee level`.

This demonstrates that a simpler transaction paying less XRP can be more
likely to get into the open ledger, or be sorted earlier in the queue
than a more complex transaction paying more XRP.

### Load Fee

Each rippled server maintains a minimum cost threshold based on its current load. If you submit a transaction with a fee that is lower than the current load-based transaction cost of the rippled server, the server neither applies nor relays the transaction to its peers. A transaction is very unlikely to survive the consensus process unless its transaction fee value meets the requirements of a majority of servers.

### Reference Transaction

In this document, a "Reference Transaction" is any currently implemented
single-signed transaction (eg. Payment, Account Set, Offer Create, etc)
that requires a fee.

In the future, there may be other transaction types that require
more (or less) work for rippled to process. Those transactions may have
a higher (or lower) base fee, requiring a correspondingly higher (or
lower) fee to get into the same position as a reference transaction.

### Consensus Health

For consensus to be considered healthy, the peers on the network
should largely remain in sync with one another. It is particularly
important for the validators to remain in sync, because that is required
for participation in consensus. However, the network tolerates some
validators being out of sync. Fundamentally, network health is a
function of validators reaching consensus on sets of recently submitted
transactions.

Another factor to consider is
the duration of the consensus process itself. This generally takes
under 5 seconds on the main network under low volume. This is based on
historical observations. However factors such as transaction volume
can increase consensus duration. This is because rippled performs
more work as transaction volume increases. Under sufficient load this
tends to increase consensus duration. It's possible that relatively high
consensus duration indicates a problem, but it is not appropriate to
conclude so without investigation. The upper limit for consensus
duration should be roughly 20 seconds. That is far above the normal.
If the network takes this long to close ledgers, then it is almost
certain that there is a problem with the network. This circumstance
often coincides with new ledgers with zero transactions.

### Other Constants

- _Base fee transaction limit per ledger_. The minimum value of 5 was
  chosen to ensure the limit never gets so small that the ledger becomes
  unusable. The "target" value of 50 was chosen so the limit never gets large
  enough to invite abuse, but keeps up if the network stays healthy and
  active. These exact values were chosen experimentally, and can easily
  change in the future.
- _Expected ledger size growth and reduction percentages_. The growth
  value of 20% was chosen to allow the limit to grow quickly as load
  increases, but not so quickly as to allow bad actors to run unrestricted.
  The reduction value of 50% was chosen to cause the limit to drop
  significantly, but not so drastically that the limit cannot quickly
  recover if the problem is temporary. These exact values were chosen
  experimentally, and can easily change in the future.
- _Minimum `lastLedgerMedianFeeLevel`_. The value of 500 was chosen to
  ensure that the first escalated fee was more significant and noticable
  than what the default would allow. This exact value was chosen
  experimentally, and can easily change in the future.
- _Transaction queue size limit_. The limit is computed based on the
  base fee transaction limit per ledger, so that the queue can grow
  automatically as the network's performance improves, allowing
  more transactions per second, and thus more transactions per ledger
  to process successfully. The limit of 20 ledgers was used to provide
  a balance between resource (specifically memory) usage, and giving
  transactions a realistic chance to be processed. The minimum size of
  2000 transactions was chosen to allow a decent functional backlog during
  network congestion conditions. These exact values were
  chosen experimentally, and can easily change in the future.
- _Maximum retries_. A transaction in the queue can attempt to apply
  to the open ledger, but get a retry (`ter`) code up to 10 times, at
  which point, it will be removed from the queue and dropped. The
  value was chosen to be large enough to allow temporary failures to clear
  up, but small enough that the queue doesn't fill up with stale
  transactions which prevent lower fee level, but more likely to succeed,
  transactions from queuing.
- _Maximum transactions per account_. A single account can have up to 10
  transactions in the queue at any given time. This is primarily to
  mitigate the lost cost of broadcasting multiple transactions if one of
  the earlier ones fails or is otherwise removed from the queue without
  being applied to the open ledger. The value was chosen arbitrarily, and
  can easily change in the future.
- _Minimum last ledger sequence buffer_. If a transaction has a
  `LastLedgerSequence` value, and cannot be processed into the open
  ledger, that `LastLedgerSequence` must be at least 2 more than the
  sequence number of the open ledger to be considered for the queue. The
  value was chosen to provide a balance between letting the user control
  the lifespan of the transaction, and giving a queued transaction a
  chance to get processed out of the queue before getting discarded,
  particularly since it may have dependent transactions also in the queue,
  which will never succeed if this one is discarded.
- _Replaced transaction fee increase_. Any transaction in the queue can be
  replaced by another transaction with the same sequence number and at
  least a 25% higher fee level. The 25% increase is intended to cover the
  resource cost incurred by broadcasting the original transaction to the
  network. This value was chosen experimentally, and can easily change in
  the future.

### `fee` command

**The `fee` RPC and WebSocket command is still experimental, and may
change without warning.**

`fee` takes no parameters, and returns information about the current local
[fee escalation](#fee-escalation) and [transaction queue](#transaction-queue)
state as both fee levels and drops. The drop values assume a
single-singed reference transaction. It is up to the user to compute the
necessary fees for other types of transactions. (E.g. multiply all drop
values by 5 for a multi-signed transaction with 4 signatures.)

The `fee` result is always instantanteous, and relates to the open
ledger. It includes the sequence number of the current open ledger,
but may not make sense if rippled is not synced to the network.

Result format:

```
{
   "result" : {
      "current_ledger_size" : "16", // number of transactions in the open ledger
      "current_queue_size" : "2", // number of transactions waiting in the queue
      "expected_ledger_size" : "15", // one less than the number of transactions that can get into the open ledger for the base fee.
      "max_queue_size" : "300", // number of transactions allowed into the queue
      "ledger_current_index" : 123456789, // sequence number of the current open ledger
      "levels" : {
         "reference_level" : "256", // level of a reference transaction. Always 256.
         "minimum_level" : "256", // minimum fee level to get into the queue. If >256, indicates the queue is full.
         "median_level" : "281600", // lastLedgerMedianFeeLevel used in escalation calculations.
         "open_ledger_level" : "320398" // minimum fee level to get into the open ledger immediately.
      },
      "drops" : {
         "base_fee" : "10", // base fee of a reference transaction in drops.
         "minimum_fee" : "10", // minimum drops to get a reference transaction into the queue. If >base_fee, indicates the queue is full.
         "median_fee" : "11000", // drop equivalent of "median_level" for a reference transaction.
         "open_ledger_fee" : "12516" // minimum drops to get a reference transaction into the open ledger immediately.
      }
   }
}
```

### [`server_info`](https://xrpl.org/server_info.html) command

**The fields listed here are still experimental, and may change
without warning.**

Up to two fields in `server_info` output are related to fee escalation.

1. `load_factor_fee_escalation`: The factor on base transaction cost
   that a transaction must pay to get into the open ledger. This value can
   change quickly as transactions are processed from the network and
   ledgers are closed. If not escalated, the value is 1, so will not be
   returned.
2. `load_factor_fee_queue`: If the queue is full, this is the factor on
   base transaction cost that a transaction must pay to get into the queue.
   If not full, the value is 1, so will not be returned.

In all cases, the transaction fee must be high enough to overcome both
`load_factor_fee_queue` and `load_factor` to be considered. It does not
need to overcome `load_factor_fee_escalation`, though if it does not, it
is more likely to be queued than immediately processed into the open
ledger.

### [`server_state`](https://xrpl.org/server_state.html) command

**The fields listed here are still experimental, and may change
without warning.**

Three fields in `server_state` output are related to fee escalation.

1. `load_factor_fee_escalation`: The factor on base transaction cost
   that a transaction must pay to get into the open ledger. This value can
   change quickly as transactions are processed from the network and
   ledgers are closed. The ratio between this value and
   `load_factor_fee_reference` determines the multiplier for transaction
   fees to get into the current open ledger.
2. `load_factor_fee_queue`: This is the factor on base transaction cost
   that a transaction must pay to get into the queue. The ratio between
   this value and `load_factor_fee_reference` determines the multiplier for
   transaction fees to get into the transaction queue to be considered for
   a later ledger.
3. `load_factor_fee_reference`: Like `load_base`, this is the baseline
   that is used to scale fee escalation computations.

In all cases, the transaction fee must be high enough to overcome both
`load_factor_fee_queue` and `load_factor` to be considered. It does not
need to overcome `load_factor_fee_escalation`, though if it does not, it
is more likely to be queued than immediately processed into the open
ledger.
