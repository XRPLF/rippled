# Ledger Replay

`LedgerReplayer` is a new `Stoppable` for replaying ledgers.
Patterned after two other `Stoppable`s under `JobQueue`---`InboundLedgers`
and `InboundTransactions`---it acts like a factory for creating
state-machine workers, and a network message demultiplexer for those workers.
Think of these workers like asynchronous functions.
Like functions, they each take a set of parameters.
The `Stoppable` memoizes these functions. It maintains a table for each
worker type, mapping sets of arguments to the worker currently working
on that argument set.
Whenever the `Stoppable` is asked to construct a worker, it first searches its
table to see if there is an existing worker with the same or overlapping
argument set.
If one exists, then it is used. If not, then a new one is created,
initialized, and added to the table.

For `LedgerReplayer`, there are three worker types: `LedgerReplayTask`,
`SkipListAcquire`, and `LedgerDeltaAcquire`.
Each is derived from `TimeoutCounter` to give it a timeout.
For `LedgerReplayTask`, the parameter set
is {reason, finish ledger ID, number of ledgers}. For `SkipListAcquire` and
`LedgerDeltaAcquire`, there is just one parameter: a ledger ID.

Each `Stoppable` has an entry point. For `LedgerReplayer`, it is `replay`.
`replay` creates two workers: a `LedgerReplayTask` and a `SkipListAcquire`.
`LedgerDeltaAcquire`s are created in the callback for when the skip list
returns.

For `SkipListAcquire` and `LedgerDeltaAcquire`, initialization fires off the
underlying asynchronous network request and starts the timeout. The argument
set identifying the worker is included in the network request, and copied to
the network response. `SkipListAcquire` sends a request for a proof path for
the skip list of the desired ledger. `LedgerDeltaAcquire` sends a request for
the transaction set of the desired ledger.

`LedgerReplayer` is also a network message demultiplexer.
When a response arrives for a request that was sent by a `SkipListAcquire` or
`LedgerDeltaAcquire` worker, the `Peer` object knows to send it to the
`LedgerReplayer`, which looks up the worker waiting for that response based on
the identifying argument set included in the response.

`LedgerReplayTask` may ask `InboundLedgers` to send requests to acquire
the start ledger, but there is no way to attach a callback or be notified when
the `InboundLedger` worker completes. All the responses for its messages will
be directed to `InboundLedgers`, not `LedgerReplayer`. Instead,
`LedgerReplayTask` checks whether the start ledger has arrived every time its
timeout expires.

Like a promise, each worker keeps track of whether it is pending (`!isDone()`)
or whether it has resolved successfully (`complete_ == true`) or unsuccessfully
(`failed_ == true`). It will never exist in both resolved states at once, nor
will it return to a pending state after reaching a resolved state.

Like promises, some workers can accept continuations to be called when they
reach a resolved state, or immediately if they are already resolved.
`SkipListAcquire` and `LedgerDeltaAcquire` both accept continuations of a type
specific to their payload, both via a method named `addDataCallback()`. Continuations
cannot be removed explicitly, but they are held by `std::weak_ptr` so they can
be removed implicitly.

`LedgerReplayTask` is simultaneously:

1. an asynchronous function,
1. a continuation to one `SkipListAcquire` asynchronous function,
1. a continuation to zero or more `LedgerDeltaAcquire` asynchronous functions, and
1. a continuation to its own timeout.

Each of these roles corresponds to different entry points:

1. `init()`
1. the callback added to `SkipListAcquire`, which calls `updateSkipList(...)` or `cancel()`
1. the callback added to `LedgerDeltaAcquire`, which calls `deltaReady(...)` or `cancel()`
1. `onTimer()`

Each of these entry points does something unique to that entry point. They
either (a) transition `LedgerReplayTask` to a terminal failed resolved state
(`cancel()` and `onTimer()`) or (b) try to make progress toward the successful
resolved state. `init()` and `updateSkipList(...)` call `trigger()` while
`deltaReady(...)` calls `tryAdvance()`. There's a similarity between this
pattern and the way coroutines are implemented, where every yield saves the spot
in the code where it left off and every resume jumps back to that spot.

### Sequence Diagram
![Sequence diagram](./ledger_replay_sequence.png?raw=true "A successful ledger replay")

### Class Diagram
![Class diagram](./ledger_replay_classes.png?raw=true "Ledger replay classes")
