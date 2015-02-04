
# Transactors #

## Introduction ##

Each separate kind of transaction is handled by it's own Transactor.
The Transactor base class provides functionality that is common to
all derived Transactors.  The Transactor base class also gives derived
classes the ability to replace portions of the default implementation.


# Details on Specific Transactors #

## AddWallet ##

## Change ##

## Offers ##

### CreateOffer ###

### CancelOffer ###

## Payment ##

## SetAccount ##

## SetRegularKey ##

## SetTrust ##

## Tickets ##

**Associated JIRA task is [RIPD-368](https://ripplelabs.atlassian.net/browse/RIPD-368)**

Currently transactions on the Ripple network require the use of sequence
numbers and sequence numbers must monotonically increase. Since the sequence
number is part of the transaction, it is "covered" by the signature that
authorizes the transaction, which means that the sequence number would have
to be decided at the time of transaction issuance. This would mean that
multi-signature transactions could only be processed in strict "first-in,
first-out" order which is not practical.

Tickets can be used in lieu of sequence number. A ticket is a special token
which, through a transaction, can be issued by any account and can be
configured with an optional expiry date and an optional associated account.

### Specifics ###

The expiry date can be used to constrain the validity of the ticket. If
specified, the ticket will be considered invalid and unusable if the closing
time of the last *validated* ledger is greater than or equal to the expiration
time of the ticket.

The associated account can be used to specify an account, other than the
issuing account, that is allowed to "consume" the ticket. Consuming a ticket
means to use the ticket in a transaction that is accepted by the network and
makes its way into a validated ledger. If not present, the ticket can only be
consumed by the issuing account.

*Corner Case:* It is possible that two or more transactions reference the same
ticket and that both go into the same consensus set. During final application
of transactions from the consensus set at most one of these transactions may
succeed; others must fail with the indication that the ticket has been consumed.

*Reserve:* While a ticket is outstanding, it should count against the reserve
of the *issuer*.

##### Issuance
We should decide whether, in the case of multi-signature accounts, any single
authorized signer can issue a ticket on the multi-signature accounts' behalf.
This approach has both advantages and disadvantages.

Advantages include:

+ Simpler logic for tickets and reduced data - no need to store or consider an associated account.
+ Owner reserves for issued tickets count against the multi-signature account instead of the account of the signer proposing a transaction.
+ Cleaner meta-data: easier to follow who issued a ticket and how many tickets are outstanding and associated with a particular account.

Disadvantages are:

+ Any single authorized signer can issue multiple tickets, each counting against the account's reserve.
+ Special-case logic for authorizing tickets on multi-sign accounts.

I believe that the disadvantages outweigh the advantages, but debate is welcome.


### CreateTicket ###

### CancelTicket ###
