# Compact Fungible Tokens (CFTs) LIGHT

## 1.1. Abstract

This document summarizes a "light" version of CFTs for prototyping Plugin Transactor functionality in Rust.

### 1.1.2. Assumptions

This proposal only exists to guide our prototyping efforts. It is not a real spec.

## 1.2. Creating Compact Fungible Tokens

### 1.2.1. On-Ledger Data Structures

We propose two new objects and one new ledger structure:

1. A **`CFTokenIssuance`** is a new object that describes a fungible token issuance created by an issuer.
1. A **`CFToken`** is a new object that describes a single account's holdings of an issued token.
1. A **`CFTokenPage`** is a ledger structure that contains a set of **`CFToken`** objects owned by the same token
   holder.

#### 1.2.1.1. The **`CFTokenIssuance`** object

The **`CFTokenIssuance`** object represents a single CFT issuance and holds data associated with the issuance itself.
Token issuances are created using the **`CFTokenIssuanceCreate`** transaction and can, optionally, be destroyed by the *
*`CFTokenIssuanceDestroy`** transaction.

##### 1.2.1.1.1. **`CFTokenIssuance`** Ledger Identifier

The ID of an CFTokenIssuance object, a.k.a `CFTokenIssuanceID` is the result of SHA512-Half of the following values,
concatenated in order:

* The CFTokenIssuance space key (0x007E).
* The AccountID of the issuer.
* The AssetCode of the issuance.

##### 1.2.1.1.2. Fields

**`CFTokenIssuance`** objects are stored in the ledger and tracked in
an [Owner Directory](https://xrpl.org/directorynode.html) owned by the issuer. Issuances have the following required and
optional fields:

| Field Name          | Required?          | JSON Type | Internal Type |
|---------------------|--------------------|-----------|---------------|
| `LedgerEntryType`   | :heavy_check_mark: | `number`  | `UINT16`      |
| `Flags`             | :heavy_check_mark: | `number`  | `UINT32`      |
| `Issuer`            | :heavy_check_mark: | `string`  | `ACCOUNTID`   |
| `AssetCode`         | :heavy_check_mark: | `string`  | `UINT160`     |
| `AssetScale`        | (default)          | `number`  | `UINT8`       |
| `MaximumAmount`     | :heavy_check_mark: | `string`  | `UINT64`      |
| `OutstandingAmount` | :heavy_check_mark: | `string`  | `UINT64`      |
| `LockedAmount`      | ️(default)         | `string`  | `UINT64`      |
| `TransferFee`       | ️(default)         | `number`  | `UINT16`      |
| `Metadata`          |                    | `string`  | `BLOB`        |
| `OwnerNode`         | (default)          | `number`  | `UINT64`      |

###### 1.2.1.1.2.1. `LedgerEntryType`

The value 0x007E, mapped to the string `CFTokenIssuance`, indicates that this object describes a Compact Fungible
Token (CFT).

###### 1.2.1.1.2.2. `Flags`

A set of flags indicating properties or other options associated with this **`CFTokenIssuance`** object. The type
specific flags proposed are:

FOR PURPOSES OF THIS PROTOTYPE, NO FLAGS ARE IMPLEMENTED!

###### 1.2.1.1.2.3. `Issuer`

The address of the account that controls both the issuance amounts and characteristics of a particular fungible token.

###### 1.2.1.1.2.4. `AssetCode`

A 160-bit blob of data. We recommend using only upper-case ASCII letters, ASCII digits 0 through 9, the dot (`.`)
character, and the dash (`-`) character. Dots and dashes should never be the first character of an asset code, and
should not be repeated sequentially.

While it's possible to store any arbitrary data in this field, implementations that detect the above recommended
character conformance can and should display them as ASCII for human readability, allowing issuers to support well-known
ISO-4207 currency codes in addition to custom codes. This also helps prevents spoofing attacks where
a [homoglyph](https://en.wikipedia.org/wiki/Homoglyph) might be used to trick a person into using the wrong asset code.

###### 1.2.1.1.2.5. `AssetScale`

An asset scale is the difference, in orders of magnitude, between a standard unit and a corresponding fractional unit.
More formally, the asset scale is a non-negative integer (0, 1, 2, …) such that one standard unit equals 10^(-scale) of
a corresponding fractional unit. If the fractional unit equals the standard unit, then the asset scale is 0.

###### 1.2.1.1.2.6. `MaximumAmount`

This value is an unsigned number that specifies the maximum number of CFTs that can be distributed to non-issuing
accounts (i.e., `minted`). For issuances that do not have a maximum limit, this value should be set to
0xFFFFFFFFFFFFFFFF.

###### 1.2.1.1.2.7. `OutstandingAmount`

Specifies the sum of all token amounts that have been minted to all token holders. This value can be stored on ledger as
a `default` type so that when its value is 0, it takes up less space on ledger. This value is increased whenever an
issuer pays CFTs to a non-issuer account, and decreased whenever a non-issuer pays CFTs into the issuing account.

###### 1.2.1.1.2.8. `TransferFee`

This value specifies the fee, in tenths of a [basis point](https://en.wikipedia.org/wiki/Basis_point), charged by the
issuer for secondary sales of the token, if such sales are allowed at all. Valid values for this field are between 0 and
50,000 inclusive. A value of 1 is equivalent to 1/10 of a basis point or 0.001%, allowing transfer rates between 0% and
50%. A `TransferFee` of 50,000 corresponds to 50%. The default value for this field is 0.

###### 1.2.1.1.2.9. `OwnerNode`

Identifies the page in the owner's directory where this item is referenced.

##### 1.2.1.1.2. Example **`CFTokenIssuance`** JSON

 ```json
 {
  "LedgerEntryType": "CFTokenIssuance",
  "Flags": 131072,
  "Issuer": "rsA2LpzuawewSBQXkiju3YQTMzW13pAAdW",
  "AssetCode": "5553440000000000000000000000000000000000",
  "AssetScale": "2",
  "MaximumAmount",
  "100000000",
  "OutstandingAmount",
  "5",
  "TransferFee": 50000,
  "Metadata": "",
  "OwnerNode": "74"
}
 ```

##### 1.2.1.1.3. How do **`CFTokenIssuance`** objects work?

Any account may issue up to 32 Compact Fungible Tokens, but each issuance must have a different **`AssetCode`**.

###### 1.2.1.1.3.1. Searching for a **`CFTokenIssuance`** object

CFT Issuances are uniquely identified by a combination of a type-specific prefix, the isser address and an asset code.
To locate a specific **`CFTokenIssuance`**, the first step is to locate the owner directory for the issuer. Then, find
the directory that holds `CFTokenIssuance` ledger objects and iterate through each entry to find the instance with the
desired **`AssetCode`**. If that entry does not exist then the **`CFTokenIssuance`** does not exist for the given
account.

###### 1.2.1.1.3.2. Adding a **`CFTokenIssuance`** object

A **`CFTokenIssuance`** object can be added by using the same approach to find the **`CFTokenIssuance`**, and adding it
to that directory. If, after addition, the number of CFTs in the directory would exceed 32, then the operation must
fail.

###### 1.2.1.1.3.3. Removing a **`CFTokenIssuance`** object

A **`CFTokenIssuance`** can be removed using the same approach, but only if the **`CurMintedAmount`** is equal to 0.

###### 1.2.1.1.3.4. Reserve for **`CFTokenIssuance`** object

Each **`CFTokenIssuance`** costs an incremental reserve to the owner account. This specification allows up to 32 *
*`CFTokenIssuance`** entries per account.

#### 1.2.1.2. The **`CFToken`** object

The **`CFToken`** object represents an amount of a token held by an account that is **not** the token issuer. CFTs are
acquired via ordinary Payment or DEX transactions, and can optionally be redeemed or exchanged using these same types of
transactions.

##### 1.2.1.2.1 Fields

A **`CFToken`** object can have the following required and optional fields. Notice that, unlike other objects, no field
is needed to identify the object type or current owner of the object, because CFT holdings are grouped into pages that
implicitly define the object type and identify the holder.

| Field Name      | Required?          | JSON Type | Internal Type |
|-----------------|--------------------|-----------|---------------|
| `CFTIssuanceID` | :heavy_check_mark: | `string`  | `UINT256`     |
| `Amount`        | :heavy_check_mark: | `string`  | `UINT64`      |
| `LockedAmount`  | default            | `string`  | `UINT64`      |
| `Flags`         | default            | `number`  | `UINT32`      |

###### 1.2.1.2.1.1. `CFTIssuanceID`

The `CFTokenIssuance` identifier.

###### 1.2.1.2.1.2. `Amount`

This value specifies a positive amount of tokens currently held by the owner. Valid values for this field are between
0x0 and 0xFFFFFFFFFFFFFFFF.

###### 1.2.1.2.1.3. `LockedAmount`

This value specifies a positive amount of tokens that are currently held in a token holder's account but that are
unavailable to be used by the token holder. Locked tokens might, for example, represent value currently being held in
escrow, or value that is otherwise inaccessible to the token holder for some other reason, such as an account freeze.

This value is stored as a `default` value such that it's initial value is `0`, in order to save space on the ledger for
a an empty CFT holding.

###### 1.2.1.2.1.4. `Flags`

A set of flags indicating properties or other options associated with this **`CFTokenIssuance`** object. The type
specific flags proposed are:

| Flag Name   | Flag Value | Description                                                                                                                                                                                                                               |
|-------------|------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `lsfFrozen` | `0x0001`   | If set, indicates that the CFT owned by this account is currently frozen and cannot be used in any XRP transactions other than sending value back to the issuer. When this flag is set, the `LockedAmount` must equal the `Amount` value. |

##### 1.2.1.2.2. Example CFToken JSON

 ```json
 {
  "TokenID": "00070C4495F14B0E44F78A264E41713C64B5F89242540EE255534400000000000000",
  "Flags": 0,
  "Amount": "100000000",
  "LockedAmount : "0"
}
 ```

#### 1.2.1.3. The **`CFTokenPage`** ledger entry

This object represents a collection of **`CFToken`** objects owned by the same account. It is important to note that the
**`CFToken`** objects themselves reside within this page, instead of in a dedicated object entry in the `SHAMap`. An
account can have multiple **`CFTokenPage`** ledger objects, which form a doubly-linked list (DLL).

In the interest of minimizing the size of a page and optimizing storage, the `Owner` field is not present since it is
encoded as part of the object's ledger identifier (more details in the **`CFTokenPageID`** discussion below).

##### 1.2.1.3.1 Fields

A **`CFTokenPage`** object may have the following required and optional fields:

| Field Name          | Required? | JSON Type | Internal Type |
|---------------------|-----------|-----------|---------------|
| `LedgerEntryType`   | ✔️        | `string`  | `UINT16`      |
| `PreviousPageMin`   | ️         | `string`  | `UINT256`     |
| `NextPageMin`       | ️         | `string`  | `UINT256`     |
| `PreviousTxnID`     | ️         | `string`  | `HASH256`     |
| `PreviousTxnLgrSeq` | ️         | `number`  | `UINT32`      |
| `CFTokens`          | ️ ✔       | `object`  | `TOKEN`       |

###### 1.2.1.3.1.1. `**LedgerEntryType**`

Identifies the type of ledger object. This proposal recommends the value `0x007F` as the reserved ledger entry type.

###### 1.2.1.3.1.2. `**PreviousPageMin**`

The locator of the previous page, if any. Details about this field and how it should be used are outlined below, after
the construction of the **`CFTokenPageID`** is explained.

###### 1.2.1.3.1.3. `**NextPageMin**`

The locator of the next page, if any. Details about this field and how it should be used are outlined below, after the
construction of the **`CFTokenPageID`** is explained.

###### 1.2.1.3.1.4. `**PreviousTxnID**`

Identifies the transaction ID of the transaction that most recently modified this **`CFTokenPage`** object.

###### 1.2.1.3.1.5. `**PreviousTxnLgrSeq**`

The sequence of the ledger that contains the transaction that most recently modified this **`CFTokenPage`** object.

###### 1.2.1.3.1.6. `**CFTokens**`

The collection of **`CFToken`** objects contained in this **`CFTokenPage`** object. This specification places an upper
bound of 32 **`CFToken`** objects per page. Objects should be stored in sorted order, from low to high with the low
order 96 bits of the `TokenID` used as the sorting parameter.

##### 1.2.1.3.2. CFTokenPage ID Format

Unlike other object identifiers on the XRP Ledger, which are derived by hashing a collection of data
using `SHA512-Half`, **`CFTokenPage`** identifiers are constructed so as to specfically allow for the adoption of a more
efficient paging structure, designed to enable user accounts to efficiently hold many CFTs at the same time.

To that end, a unique CFTokenPage ID (a.k.a., `CFTokenPageID`) is derived by concatenating a 196-bit value that uniquely
identifies a particular account's holdings of CFT, followed by a 64-bit value that uniquely identifies a particular CFT
issuance. Using this construction enables efficient lookups of individual `CFTokenPage` objects without requiring
iteration of the doubly-linked list of all CFTokenPages.

More formally, we assume:

- The function `high196(x)` returns the "high" 196 bits of a 256-bit value.
- The function `low64(x)` returns the "low" 64-bits of a 256-bit value.
- A `CFTokenIssuanceID` uniqely identifies a CFT Issuance as defined above in ["CFTokenIssuance Ledger Identifier"].
- A `CFTokenHolderID` uniquely identifies a holder of some amount of CFT (as opposed to other token types such as an
  NFT) and is defined as the result of SHA512-Half of the following values, concatenated in order:
    - The `CFTokenIssuance` ledger identifier key (0x007E).
    - The `AccountID` of the CFT holder.

Therefore:

- Let `CFTokenPageID` equal `high196(CFTokenHolderId)` concatenated with `low64(CFTokenIssuanceId)`.
- Let `CFTokenIssuanceID` `A` only be included in a page with `CFTokenPageId` `B` if and only if `low64(A) >= low64(B)`.

This scheme is similar to the existing scheme for organizing `NFToken` objects into `NFTokenPage`s.

##### 1.2.1.3.3. Example CFTokenPage JSON

 ```json
 {
  "LedgerEntryType": "CFTokenPage",
  "PreviousTokenPage": "598EDFD7CF73460FB8C695d6a9397E907378C8A841F7204C793DCBEF5406",
  "PreviousTokenNext": "598EDFD7CF73460FB8C695d6a9397E9073781BA3B78198904F659AAA252A",
  "PreviousTxnID": "95C8761B22894E328646F7A70035E9DFBECC90EDD83E43B7B973F626D21A0822",
  "PreviousTxnLgrSeq": 42891441,
  "CFTokens": {
{
  "CFTokenID": "00070C4495F14B0E44F78A264E41713C64B5F89242540EE255534400000000000000",
  "Amount": 50000
},
...
}
}
 ```

##### 1.2.1.3.4. How do **`CFTokenPage`** objects work?

The page within which a **`CFToken`** entry is stored will be formed as described above. This is needed to find the
correct starting point in the doubly-linked list of **`CFTokenPage`** objects if that list is large. This is because it
is inefficient to have to traverse the list from the beginning if an account holds thousands of **`CFToken`** objects in
hundreds of **`CFTokenPage`** objects.

###### 1.2.1.3.4.1. Searching a **`CFToken`** object

To search for a specific **`CFToken`**, the first step is to locate the **`CFTokenPage`**, if any, that should contain
that **`CFToken`**. For that do the following:

Compute the **`CFTokenPageID`** using the account of the owner and the **`CFTIssuanceID`** of the token, as described
above. Then search for the ledger entry whose identifier is less than or equal to that value. If that entry does not
exist or is not a **`CFTokenPage`**, the **`CFToken`** is not held by the given account.

###### 1.2.1.3.4.2. Adding a **`CFToken`** object

A **`CFToken`** object can be added by using the same approach to find the **`CFTokenPage`** it should be in and adding
it to that page. If after addition the page overflows, find the `next` and `previous` pages (if any) and balance those
three pages, inserting a new page as/if needed.

###### 1.2.1.3.4.2. Removing a **`CFToken`** object

A **`CFToken`** can be removed by using the same approach. If the number of **`CFToken`** in the page goes below a
certain threshhold, an attempt will be made to consolidate the page with a `previous` or subsequent page and recover the
reserve.

###### 1.2.1.3.4.3. Reserve for **`CFTokenPage`** object

Each **`CFTokenPage`** costs an incremental reserve to the owner account. This specification allows up to 32 **`CFToken`
** entries per page, which means that for accounts that hold multiple CFTs the _effective_ reserve cost per Fungible
Token can be as low as _R_/32 where _R_ is the incremental reserve.

## 1.3 Transactions

This proposal introduces several new transactions to allow for the creation and deletion of CFT issuances. Likewise,
this proposal introduce several new transactions for minting and redeeming discrete instances of CFTs. All transactions
introduced by this proposal incorporate the [common transaction fields](https://xrpl.org/transaction-common-fields.html)
that are shared by all transactions. Common fields are not documented in this proposal unless needed because this
proposal introduces new possible values for such fields.

### 1.3.1 Transactions for Creating and Destroying Compact Fungible Token Issuances on XRPL

We define three transactions related to CFT Issuances: **`CFTokenIssuanceCreate`** for minting CFT _Issuances_ on XRPL.

#### 1.3.1.1 The **`CFTokenIssuanceCreate`** transaction

The **`CFTokenIssuanceCreate`** transaction creates an **`CFTokenIssuance`** object and adds it to the relevant
directory node of the `creator`. This transaction is the only opportunity an `issuer` has to specify any token fields
that are defined as immutable (e.g., CFT Flags).

If the transaction is successful, the newly created token will be owned by the account (the `creator` account) which
executed the transaction.

##### 1.3.1.1.1 Transaction-specific Fields

| Field Name        | Required? | JSON Type | Internal Type |
|-------------------|-----------|-----------|---------------|
| `TransactionType` | ️ ✔       | `object`  | `UINT16`      |

Indicates the new transaction type **`CFTokenIssuanceCreate`**. The integer value is `25 (TODO)`.

| Field Name  | Required? | JSON Type | Internal Type |
|-------------|-----------|-----------|---------------|
| `AssetCode` | ️ ✔       | `string`  | `BLOB`        |

A 160-bit blob of data. It is reccommended to use only upper-case ASCII letters in addition to the ASCII digits 0
through 9. While it's possible to store any arbitrary data in this field, implementations that detect the above
reccommended characters should display them as ASCII for human readability. This also helps prevents spoofing attacks
where a [homoglyph](https://en.wikipedia.org/wiki/Homoglyph) might be used to trick a person into using the wrong asset
code.

| Field Name   | Required? | JSON Type | Internal Type |
|--------------|-----------|-----------|---------------|
| `AssetScale` | ️ ✔       | `number`  | `UINT8`       |

An asset scale is the difference, in orders of magnitude, between a standard unit and a corresponding fractional unit.
More formally, the asset scale is a non-negative integer (0, 1, 2, …) such that one standard unit equals 10^(-scale) of
a corresponding fractional unit. If the fractional unit equals the standard unit, then the asset scale is 0.

| Field Name | Required? | JSON Type | Internal Type |
|------------|-----------|-----------|---------------|
| `Flags`    | ️         | `number`  | `UINT16`      |

Specifies the flags for this transaction. In addition to the universal transaction flags that are applicable to all
transactions (e.g., `tfFullyCanonicalSig`), the following transaction-specific flags are defined and used to set the
appropriate fields in the Fungible Token:

FOR PURPOSES OF THIS PROTOTYPE, NO FLAGS ARE IMPLEMENTED!

| Field Name    | Required? | JSON Type | Internal Type |
|---------------|-----------|-----------|---------------|
| `TransferFee` |           | `number`  | `UINT16`      | 

The value specifies the fee to charged by the issuer for secondary sales of the Token, if such sales are allowed. Valid
values for this field are between 0 and 50,000 inclusive, allowing transfer rates of between 0.000% and 50.000% in
increments of 0.001.

The field MUST NOT be present if the `tfTransferable` flag is not set. If it is, the transaction should fail and a fee
should be claimed.

| Field Name      | Required?          | JSON Type | Internal Type |
|-----------------|--------------------|-----------|---------------|
| `MaximumAmount` | :heavy_check_mark: | `string`  | `UINT64`      | 

The maximum asset amount of this token that should ever be issued.

| Field Name | Required?          | JSON Type | Internal Type |
|------------|--------------------|-----------|---------------|
| `Metadata` | :heavy_check_mark: | `string`  | `BLOB`        | 

Arbitrary metadata about this issuance, in hex format.

##### 1.3.1.1.2 Example **`CFTokenIssuanceCreate`** transaction

```
{
  "TransactionType": "CFTokenIssuanceCreate",
  "AssetCode": "5553440000000000000000000000000000000000",
  "AssetScale": "2",
  "TransferFee": 314,
  "MaxAmount": "50000000",
  "Flags": 83659,
  "Metadata": "FOO",
  "Fee": 10,
}
```

This transaction assumes that the issuer of the token is the signer of the transaction.


### 1.3.4 The **`Payment`** Transaction

The existing `Payment` transaction will not have any new top-level fields or flags added. However, we will extend the
existing `amount` field to accommodate CFT amounts.

#### 1.3.4.1 The `amount` field

Currently, the amount field takes one of two forms. The below indicates an amount of 1 drop of XRP::

```json
"amount": "1"
```

The below indicates an amount of USD $1 issued by the indicated amount::

```json
"amount": {
"issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
"currency": "USD",
"value": "1"
}
```

We propose using the following format for CFT amounts::

```json
"amount": {
"issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
"cft_asset": "USD",
"value": "1"
}
```

The idea behind this format is that it adds only one new subfield to the `amount` field, but still distinguishes between
itself and Issued Currency amounts. Using the CFT ID directly would not allow us to easily find the
underlying `CFTokenIssuance` object because we would still need the `issuer` to know where to look for it.


## Not Implemented

* No CFTokenSet API (Freeze or UnFreeze).
* No CFTokenIssuanceSet (No Global Freeze)
* No AccountDelete.
* No CFTokenIssuanceDestroy
* No CFT Issuance limit per account.
* 