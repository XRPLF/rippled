# MessageScheduler

At times, our server needs to communicate with its peers:
sending them transactions, proposals, and validations, or requesting data.
At different times we may be sending more than we are receiving, and vice versa.
We have to strike a balance between our ability to flood peers with
notifications and requests and their ability to digest those notifications and
respond to those requests.
If not, then we run the risk of overwhelming our peers in activity
indistinguishable from a denial-of-service attack.

Each message to a peer effectively corresponds to a remote job executed on that
peer.
We already recognize that local threads are a limited resource,
and that local jobs are a demand on that resource.
We throttle the number of simultaneously executing local jobs to avoid
overloading the underlying machine's capacity for simultaneously executing
threads,
and we schedule local jobs according to priority during periods of high
contention.
We should do the same for remote jobs, i.e. messages.


## Comparison to JobQueue

The model for a message scheduler can be informed by our model of a job queue.
With the job queue, callers may submit as many jobs as they want.
Each job is a closure representing a unit of work,
and scheduling a job means requesting an idle thread to perform that work.
Jobs that cannot be immediately executed are queued.
Data for a job can continue to accumulate outside of the closure in
anticipation of the job's execution.
A job is finished when it returns,
and after it finishes, its thread is returned to the pool to be
allocated for another job.

Our message scheduler has some similarities.
Senders should be able to submit as many remote jobs as they want.
Each remote job is a closure representing a unit of work,
and scheduling a remote job means requesting an idle peer to perform that
work.
Remote jobs that cannot be immediately executed should be queued.
Data for a remote job should continue to accumulate outside of the closure in
anticipation of the job's execution.
A remote job is finished when it returns a response,
and after it finishes, its peer is returned to the pool to be allocated for
another remote job.

A peer scheduler has some differences too.
Unlike a job, a sender starting a remote job (by sending a request message)
cannot guarantee that it will terminate (by receiving a response message).
Thus we need timeouts to guarantee bounded termination.

Unlike local jobs where threads are isomorphic,
peers are distinguishable.
When a sender first sends a request,
it may not care which peer is chosen to receive it,
but when it retries a request, it likely will,
perhaps preferring to send it to a different peer in hopes that it will be
more likely to respond with the desired data in a reasonable time.


## Definitions

Before diving into the design discussion,
I want to introduce terminology to frame that discussion.

- **peer**: A connected peer server. It can work on one message at a time.
- **message**: A message sent or received from a peer. A message is either
    a request, a response, or a notification.
- **request**: A message sent to a peer that expects a response.
- **response** A message received from a peer in response to a request.
- **notification**: A message sent to a peer that expects no response.
- **sender**: An object that is waiting for an idle peer to send a message.
- **receiver**: An object that is waiting for a response.


## Interface

Let us walk through the typical workflow of a request from the perspective of
a dependent.

The first step is to schedule a **sender**, adding it to a queue of senders
ordered by priority.[^5]
A sender represents a demand for at least one idle peer to which to send
messages.[^4]
The sender has a callback method, `onOffer`, that accepts a peer **offer**.[^6]

[^4]: Each "peer" in this document represents capacity for one message.
A peer that can actually handle multiple simultaneous messages is represented
in the scheduler by copies of that peer.
[^5]: For now, the priority order is just first come, first served.
We want to consider more sophisticated priority schemes for the future.
[^6]: The `onOffer` callback may be called immediately,
in the same thread that scheduled the sender.
If a caller wants the offer to come in a different thread,
so as not to block the current thread or double lock a mutex,
then it should schedule a job that will schedule the sender.

Eventually an offer will be made by passing it to `sender.onOffer`.
An offer contains N idle peers,
and permits the sender to send messages to M &lt;= N of them.
Each peer represents the capacity to send exactly one message.
M is called the **supply** of the offer.
The sender can iterate through the idle peers in the offer,
sending messages to as many or as few as it wants,
until (a) it has no more messages to send,
(b) the supply is exhausted, or
(c) it has considered every peer in the offer.
Sending a message to a peer is called **consuming** the peer.

If a sender expects a response to its message, then we call that message
a **request**.
If it does not expect a response to its message, then we call that message
a **notification**.
Whenever it sends a request,
the sender must pass to the scheduler a **receiver**
that will wait for and react to the response.

The sender must set a timeout for each message it sends.
For requests, this timeout reflects the time after which a response is
considered missing and the sender may retry.
For notifications, this timeout reflects the time it expects the peer to need
to digest the notification,
effectively limiting the message rate of senders.

If the sender consumed any peers, it will be removed from the sender queue.
If it consumed no peers, perhaps because it is waiting for a specific peer,
then it will be left in the queue to see another, different offer later.
If it consumed a peer, but wants more, perhaps because it has more messages
to send than there was supply in the offer, then it must schedule another
sender.

When the response arrives, the receiver's success callback will be called with
it.
If the timeout expires before, then the receiver's failure callback will be
called with an error code.
Either way, it may choose to schedule another sender, looping back around.


## Design

### Offers

An offer is a negotiation between the scheduler and a sender:
the scheduler is _offering_ some subset of idle peers.
The full set of idle peers is included in the offer to give the sender the
chance to select its favorite subset based on any arbitrary condition.
But if multiple senders are waiting, then we don't want to offer the use of
_every_ idle peer to a sender, or else a single high priority sender can hog
every idle peer indefinitely, starving other senders.
We want a "fair" distribution of idle peers among senders that respects
priority but avoids starvation.
The ability to offer M of N peers gives us flexibility to implement different
fairness strategies.
For now, the strategy is for M to be 1 as long as multiple senders are waiting,
and N otherwise.

If a sender consumes peers from an offer but is not wholly satisfied,
then it may schedule another "continuing" sender to wait for
another offer.
That continuing sender will be placed in the queue by priority (currently
ordered by arrival), and if there are any remaining idle peers after offering
them to any higher priority senders, then they will be offered to the
continuing sender.

When a sender is offered idle peers, it may filter through them and consume the
ones it prefers, up to the offer's supply.
If it does not consume any peers, then it will remain in its place in the queue
and wait for another offer containing a different set of idle peers.
That set is guaranteed to be different, but it may overlap, even significantly.
In fact, it is likely that the next offer will be simply one new idle peer plus
the set in the previous offer.


### Senders and Receivers

When scheduling a sender, we pass a sender.
When sending a message, we pass a receiver.
In the implementation, these objects are represented by
raw pointers to abstract base classes.

The sender has two callback methods: one for sending messages to idle peers,
and one for discarding the sender (called only when the scheduler is stopping).
The receiver has two callback methods: one for success (which means receiving
a response) and one for failure (which can mean a number of reasons).

The scheduler holds senders and receivers by a pointer that the scheduler does
not own and will not delete so that static objects can be used as senders and
receivers if desired.
Receiver callbacks are called with the request ID (returned by the method that
sends a request) so that the same receiver can be used for different requests.


### Lifetimes

Even though the message scheduler takes senders and receivers by non-owning
pointers,
to permit the use of long-living senders and receivers,
some of them may be short-living and require deletion.

Senders and receivers are guaranteed that either
(a) exactly one of their callbacks will be called exactly once,
in which case they can delete themselves, or
(b) they will be returned to a **withdrawer**,
who is then responsible for deleting them.
We choose the name "withdraw" over "cancel" to convey that the sender or
receiver will be returned, not just discarded.[^7]

[^7]: Withdrawal is not yet implemented.

The message scheduler should be constructed as a lower layer,
before anything that depends on it.
That means dependents should be destroyed before the scheduler is destroyed,
but that may leave waiting senders or receivers with dangling references to
those dependents.
There are two ways to handle this situation:

1. Let callers withdraw both senders and receivers.
2. Let a caller "stop" the scheduler from calling any more senders or receivers.

When stopping, the scheduler immediately fails all waiting receivers
(with the reason "shutdown")
and discards all waiting senders,
and never again accepts a sender or receiver.


### Peers

The lifetimes of peer objects are not managed by the peer scheduler.
They are managed by the overlay and the peer objects themselves.[^3]
The scheduler holds only `std::weak_ptr`s to the peer objects in its pool.
A peer offer is a set of `std::weak_ptr`s.
As a sender iterates through an offer, those `std::weak_ptr`s are locked to
`std::shared_ptr`s.
If it cannot be locked, then the peer object has already been destroyed, and it
will be skipped over, silently.
(This means that it is possible that a sender receives an offer that has less
true supply than what is suggested, and even possibly zero supply.)

[^3]: When a `PeerImp` is constructed, it starts a timer.
At first, that timer callback holds the only `std::shared_ptr` owning the `PeerImp`,
and the `OverlayImpl` holds a table of `std::weak_ptr<PeerImp>`s.
Copies of the `std::shared_ptr` are made only when callers are ready to send
a message.
Whenever the `PeerImp` hears a heartbeat from the connected peer, it resets the timer.
If the timer expires before the next heartbeat arrives, then the
`std::shared_ptr` in the callback is destroyed, and as soon as no other scope
is holding a copy of the same `std::shared_ptr`, then the `PeerImp` is destroyed.


### Locks

The message scheduler maintains two locks:
one for "offers", i.e. the queue of senders and the pool of idle peers,
together;
and one for "requests", i.e. a table of waiting receivers.

All callbacks are called while one of the locks is held.
This way, they can benefit from the assumption that they are not executing
concurrently.[^8]
Callbacks should be very short then.
A callback that wants to do significant work should just schedule a job for that
work and return.

[^8]: Maybe we don't need this?

Only `Sender::onOffer` should call `MessageScheduler::send` (via
`PeerOffer::Iterator::send`), which locks the requests.
`Sender::onOffer` is called while the offers are locked,
which means that we must be careful to always lock the offers before the
requests in scenarios where we need to lock both, in order to avoid a deadlock.

`MessageScheduler::schedule` may be called from any sender or receiver callback.
It typically locks the offers, except that it uses a thread-local variable to
detect when it is called in a thread that already owns the offer lock.
