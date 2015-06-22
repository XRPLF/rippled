
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

## SetSignerList ##

In order to enhance the flexibility of Ripple and provide support for enhanced
security of accounts, native support for "multi-signature" or "multi-sign"
accounts is required.

Transactions on an account which is designated as multi-sign can be authorized
either by using the master or regular keys (unless those are disabled) or by
being signed by a certain number (a quorum) of pre-authorized accounts.

Some technical details, including tables indicating some of the Ripple
commands and ledger entries to be used for implementing multi-signature, are
currently listed on the [wiki](https://ripple.com/wiki/Multisign) but will
eventually be migrated into this document as well.

For accounts which are designated as multi-sign, there must be a list which
specifies which accounts are authorized to sign and the quorum threshold.

 - Each authorized account has a weight. The sum of the weights of the signers is used to determine whether a given set of signatures is sufficient for a quorum.

 - The quorum threshold indicates the minimum required weight that the sum of the weights of all signatures must have before a transaction can be authorized.

### Verification of Multiple Signatures During TX Processing
The current approach to adding multi-signature support is to require that a
transaction is to be signed outside the Ripple network and only submitted
after the quorum has been reached.

This reduces the implementation footprint and the load imposed on the network,
and mirrors the way transaction signing is currently handled. It will require
some messaging mechanism outside the Ripple network to disseminate the proposed
transaction to the authorized signers and to allow them to apply signatures.

Supporting in-ledger voting should be considered, but it has both advantages
and disadvantages.

One of the advantages is increased transparency - transactions are visible as
are the "votes" (the authorized accounts signing the transaction) on the
ledger. However, transactions may also languish for a long time in the ledger,
never reaching a quorum and consuming network resources.

### Signature Format
We should not develop a new format for multi-sign signatures. Instead every
signer should extract and sign the transaction as they normally would if this
were a regular transaction. The resulting signature will be stored as a triple
of { signing-account, signer-public-key, signature } in an array of signatures
associated with this transaction.

The advantage of this is that we can reuse the existing signing and
verification code, and leverage the work that will go towards implementing
support for the Ed25519 elliptic curve.

### Fees ###
Multi-signature transactions impose a heavier load on the network and should
claim higher fees.

The fee structure is not yet decided, but an escalating fee structure is laid
out and discussed on the [wiki](https://ripple.com/wiki/Multisign). This
might need to be revisited and designed in light of discussions about changing
how fees are handled.

### Proposed Transaction Cancellation ###
A transaction that has been proposed against a multi-sign account using a
ticket can be positively canceled if a quorum of authorized signers sign and
issue a transaction that consumes that ticket.

### Implementation ###

Any account can have one SignerList attached to it.  A SignerList contains the
following elements:

 - A list of from 1 to a protocol-defined maximum of 8 signers.  Each signer in the array consists of:
   - The signer's 160-bit account ID and
   - The signer's 16-bit weight (used to calculate whether a quorum is met).
 - And, for the entire list, a single 32-bit quorum value.

Giving the signers different weights allows an account to organize signers so
some are more important than others.  A signer with a larger weight has more
significance in achieving the quorum.

A multi-signed transaction is validated like this:

 - Each signer of the transaction has their signature validated.
 - The weights of all valid signers are summed.
 - If the sum of the weights equals or exceeds the quorum value then the entire transaction is considered signed.  If the sum is below the quorum, then the signature fails with a tefBAD_QUORUM.


By making the signer weights 16 bits and the quorum value 32 bits we avoid
concerns about overflows and still have plenty of resolution.

This transactor allows two operations:

 - Create (or replace) a signer list for the target account.
 - Remove any signer list from the target account.

The data for a transaction creating or replacing a signer list has this
general form:

    {
        "TransactionType": "SignerListSet",
        "Account": "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
        "SignerQuorum": 7,
        "SignerEntries": [
            {
                "SignerEntry": {
                    "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    "SignerWeight": 4
                }
            },
            {
                "SignerEntry": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "SignerWeight": 3
                }
            }
        ]
    }

The data for a transaction that removes any signer list has this form:

    {
        "TransactionType": "SignerListSet",
        "Account": "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
        "SignerQuorum": 0
    }

## SetTrust ##

## Tickets ##

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
