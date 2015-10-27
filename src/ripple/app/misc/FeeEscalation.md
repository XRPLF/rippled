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
  * If consensus stays [healthy](#consensus-health), the limit will
  be the max of the current limit or the number of transactions in
  the validated ledger until it gets to [50](#other-constants), at
  which point, the limit will only be updated to the number of
  transactions in the validated ledger if it is larger than 50.
  * If consensus does not stay [healthy](#consensus-health),
  the limit will clamp down to the smaller of [50](#other-constants)
  or the number of transactions in the validated ledger.
3. Once there are more transactions in the open ledger than indicated
by the limit, the required fee level jumps drastically.
  * The formula is `( baseFeeLevel * lastLedgerMedianFeeLevel *
  TransactionsInOpenLedger^2 / limit^2 )`,
  and returns a [fee level](#fee-level).
4. That may still be pretty small, but as more transactions get
into the ledger, the fee level increases exponentially.
  * For example, if the limit is 6, and the median fee is minimal,
  and assuming all [reference transactions](#reference-transaction),
  the 7th transaction only requires a [level](#fee-level) of about 174,000
  or about 6800 drops,
  but the 20th transaction requires a [level](#fee-level) of about
  1,422,000 or about 56,000 drops.
5. Finally, as each ledger closes, the median fee level of that ledger is
computed and used as `lastLedgerMedianFeeLevel` (with a
[minimum value of 500](#other-constants))
in the fee escalation formula for the next open ledger.
  * Continuing the example above, if ledger consensus completes with
  only those 20 transactions, and all of those transactions paid the
  minimum required fee at each step, the limit will be adjusted from
  6 to 20, and the `lastLedgerMedianFeeLevel` will be about 393,000,
  which is 15,000 drops for a
  [reference transaction](#reference-transaction).
  * This will cause the first 21 transactions only require 10
  drops, but the 22st transaction will require
  a level of about 110,000,000 or about 4.3 million drops (4.3XRP).

## Transaction Queue

An integral part of making fee escalation work for users of the network
is the transaction queue. The queue allows legitimate transactions to be
considered by the network for future ledgers if the escalated open
ledger fee gets too high. This allows users to submit low priority
transactions with a low fee, and wait for high fees to drop. It also
allows legitimate users to continue submitting transactions during high
traffic periods, and give those transactions a much better chance to
succeed.

1. If an incoming transaction meets the base [fee level](#fee-level),
but does not have a high enough [fee level](#fee-level) to immediately
go into the open ledger, it is instead put into the queue and broadcast
to peers. Each peer will then make an independent decision about whether
to put the transaction into its open ledger or the queue. In principle,
peers with identical open ledgers will come to identical decisions. Any
discrepancies will be resolved as usual during consensus.
2. When consensus completes, the open ledger limit is adjusted, and
the required [fee level](#fee-level) drops back to the base
[fee level](#fee-level). Before the ledger is made available to
external transactions, transactions are applied from the queue to the
ledger from highest [fee level](#fee-level) to lowest. These transactions
count against the open ledger limit, so the required [fee level](#fee-level)
may start rising during this process.
3. Once the queue is empty, or required the [fee level](#fee-level)
jumps too high for the remaining transactions in the queue, the ledger
is opened up for normal transaction processing.
4. A transaction in the queue can stay there indefinitely in principle,
but in practice, either
  * it will eventually get applied to the ledger,
  * its last ledger sequence number will expire,
  * the user will replace it by submitting another transaction with the same
  sequence number and a higher fee, or
  * it will get dropped when the queue fills up with more valuable transactions.
  The size limit is computed dynamically, and can hold transactions for
  the next [20 ledgers](#other-constants).
  The lower the transaction's fee, the more likely that it will get dropped if the
  network is busy.

Currently, there is an additional restriction that the queue can only hold one
transaction per account at a time. Future development will make the queue
aware of transaction dependencies so that more than one can be
queued up at a time per account.

## Technical Details

### Fee Level

"Fee level" is used to allow the cost of different types of transactions
to be compared directly.  For a [reference
transaction](#reference-transaction), the base fee
level is 256. If a transaction is submitted with a higher `Fee` field,
the fee level is scaled appropriately.

Examples, assuming a base fee of 10 drops:

1. A single-signed [reference transaction](#reference-transaction)
with `Fee=20` will have a fee level of 512.
2. A multi-signed transaction with 3 signatures (base fee = 40 drops)
and `Fee=60` will have a fee level of 384.
3. A hypothetical future non-reference transaction with a base
fee of 15 drops multi-signed with 5 signatures and `Fee=90` will
have a fee level of 256.

This demonstrates that a simpler transaction paying less XRP can be more
likely to get into the open ledger, or be sorted earlier in the queue
than a more complex transaction paying more XRP.

### Reference Transaction

In this document, a "Reference Transaction" is any currently implemented
single-signed transaction (eg. Payment, Account Set, Offer Create, etc)
that requires a fee.

In the future, there may be other transaction types that require
more (or less) work for rippled to process. Those transactions may have
a higher (or lower) base fee, requiring a correspondingly higher (or
lower) fee to get into the same position as a reference transaction.

### Consensus Health

For consensus to be considered healthy, the consensus process must take
less than 5 seconds. This time limit was chosen based on observed past
behavior of the ripple network. Note that this is not necessarily the
time between ledger closings, as consensus usually starts some amount
of time after a ledger opens.

### Other Constants

* *Base fee transaction limit per ledger*. The minimum value of 5 was
chosen to ensure the limit never gets so small that the ledger becomes
unusable. The "target" value of 50 was chosen so the limit never gets large
enough to invite abuse, but keeps up if the network stays healthy and
active. These exact values were chosen experimentally, and can easily
change in the future.
* *Minimum `lastLedgerMedianFeeLevel`*. The value of 500 was chosen to
ensure that the first escalated fee was more significant and noticable
than what the default would allow. This exact value was chosen
experimentally, and can easily change in the future.
* *Transaction queue size limit*. The limit is computed based on the
base fee transaction limit per ledger, so that the queue can grow
automatically as the ripple network's performance improves, allowing
more transactions per second, and thus more transactions per ledger
to process successfully.  The limit of 20 ledgers was used to provide
a balance between resource (specifically memory) usage, and giving
transactions a realistic chance to be processed. This exact value was
chosen experimentally, and can easily change in the future.

### `fee` command

**The `fee` RPC and WebSocket command is still experimental, and may
change without warning.**

`fee` takes no parameters, and returns information about the current local
[fee escalation](#fee-escalation) and [transaction queue](#transaction-queue)
state as both fee levels and drops. The drop values assume a
single-singed reference transaction. It is up to the user to compute the
neccessary fees for other types of transactions. (E.g. multiply all drop
values by 5 for a multi-signed transaction with 4 signatures.)

The `fee` result is always instantanteous, and relates to the open
ledger. Thus, it does not include any sequence number or IDs, and may
not make sense if rippled is not synced to the network.

Result format:
```
{
   "result" : {
      "current_ledger_size" : "16", // number of transactions in the open ledger
      "current_queue_size" : "2", // number of transactions waiting in the queue
      "expected_ledger_size" : "15", // one less than the number of transactions that can get into the open ledger for the base fee.
      "max_queue_size" : "300", // number of transactions allowed into the queue
      "levels" : {
         "reference_level" : "256", // level of a reference transaction. Always 256.
         "minimum_level" : "256", // minimum fee level to get into the queue. If >256, indicates the queue is full.
         "median_level" : "281600", // lastLedgerMedianFeeLevel used in escalation calculations.
         "open_ledger_level" : "82021944" // minimum fee level to get into the open ledger immediately.
      },
      "drops" : {
         "base_fee" : "10", // base fee of a reference transaction in drops.
         "minimum_fee" : "10", // minimum drops to get a reference transaction into the queue. If >base_fee, indicates the queue is full.
         "median_fee" : "11000", // drop equivalent of "median_level" for a reference transaction.
         "open_ledger_fee" : "3203982" // minimum drops to get a reference transaction into the open ledger immediately.
      }
   }
}
```

### Enabling Fee Escalation

These features are disabled by default and need to be activated by a
feature in your rippled.cfg. Add a `[features]` section if one is not
already present, and add `FeeEscalation` (case-sensitive) to that
list, then restart rippled.

```
[features]
FeeEscalation
```
