
# M-of-N / Multi-Signature Support on Ripple

In order to enhance the flexibility of Ripple and provide support for enhanced security of accounts, native support for "multi-signature" or "multisign" accounts is required.

Transactions on an account which is designated as multisign can be authorized either by using the master or regular keys (unless those are disabled) or by being signed by a certain number (a quorum) of preauthorized accounts.

Some technical details, including tables indicating some of the Ripple commands and ledger entries to be used for implementing multi-signature, are currently listed on the [wiki](https://ripple.com/wiki/Multisign) but will eventually be migrated into this document as well.

## Steps to MultiSign

The implementation of multisign is a protocol breaking change which will require the coordinated rollout and deployment of the feature on the Ripple network.

Critical components for MultiSign are:

* Ticket Support
* Authorized Signer List Management
* Verification of Multiple Signatures during TX processing.

### Ticket Support

** This is the task that NikB is currently working on - it is documented on the JIRA as [RIPD-368](https://ripplelabs.atlassian.net/browse/RIPD-368)**

Currently transactions on the Ripple network require the use of sequence numbers and sequence numbers must monotonically increase. Since the sequence number is part of the transaction, it is "covered" by the signature that authorizes the transaction, which means that the sequence number would have to be decided at the time of transaction issuance. This would mean that multi-signature transactions could only be processed in strict "first-in, first-out" order which is not practical.

Tickets can be used in lieu of sequence number. A ticket is a special token which, through a transaction, can be issued by any account and can be configured with an optional expiry date and an optional associated account.

#### Specifics

The expiry date can be used to constrain the validity of the ticket. If specified, the ticket will be considered invalid and unusable if the closing  time of the last *validated* ledger is greater than or equal to the expiration time of the ticket.

The associated account can be used to specify an account, other than the issuing account, that is allowed to "consume" the ticket. Consuming a ticket means to use the ticket in a transaction that is accepted by the network and makes its way into a validated ledger. If not present, the ticket can only be consumed by the issuing account.

*Corner Case:* It is possible that two or more transactions reference the same ticket and that both go into the same consensus set. During final application of transactions from the consensus set at most one of these transactions may succeed; others must fail with the indication that the ticket has been consumed.

*Reserve:* While a ticket is outstanding, it should count against the reserve of the *issuer*.

##### Issuance
We should decide whether, in the case of multi-signature accounts, any single authorized signer can issue a ticket on the multisignature accounts' behalf. This approach has both advantages and disadvantages.

Advantages include:

+ Simpler logic for tickets and reduced data - no need to store or consider an associated account.
+ Owner reserves for issued tickets count against the multi-signature account instead of the account of the signer proposing a transaction.
+ Cleaner meta-data: easier to follow who issued a ticket and how many tickets are outstanding and associated with a particular account.

Disadvantages are:

+ Any single authorized signer can issue multiple tickets, each counting against the account's reserve.
+ Special-case logic for authorizing tickets on multi-sign accounts.

I believe that the disadvantages outweigh the advantages, but debate is welcome.

##### Proposed Transaction Cancelation
A transaction that has been proposed against a multi-sign account using a ticket can be positively cancelled if a quorum of authorized signers sign and issue a transaction that consumes that ticket.

### Authorized Signer List Management

For accounts which are designated as multi-sign, there must be a list which specifies which accounts are authorized to sign and the quorum threshold.

The quorum threshold indicates the minimum required weight that the sum of the weights of all signatures must have before a transaction can be authorized. It is an unsigned integer that is at least 2.

Each authorized account can be given a weight, from 1 to 32 inclusive. The weight is used when to determine whether a given number of signatures are sufficient for a quorum.

### Verification of Multiple Signatures during TX processing
The current approach to adding multi-signature support is to require that a transaction is to be signed outside the Ripple network and only submitted after the quorum has been reached.

This reduces the implementation footprint and the load imposed on the network, and mirrors the way transaction signing is currently handled. It will require some messaging mechanism outside the Ripple network to disseminate the proposed transaction to the authorized signers and to allow them apply signatures.

Supporting in-ledger voting should be considered, but it has both advantages and disadvantages.

One of the advantages is increased transparency - transactions are visible as are the "votes" (the authorized accounts signing the transaction) on the ledger. However, transactions may also languish for a long time in the ledger, never reaching a quorum and consuming network resources.

### Signature Format
We should not develop a new format for multi-sign signatures. Instead each signer should extract and sign the transaction as he normally would if this were a regular transaction. The resulting signature will be stored as a pair of { signing-account, signature } in an array of signatures associated with this transaction.

The advantage of this is that we can reuse the existing signing and verification code, and leverage the work that will go towards implementing support for the Ed25519 elliptic curve.


### Fees
Multi-signature transactions impose a heavier load on the network and should claim higher fees.

The fee structure is not yet decided, but an escalating fee structure is laid out and discussed on the [wiki](https://ripple.com/wiki/Multisign). This might need to be revisited and designed in light of discussions about changing how fees are handled.
