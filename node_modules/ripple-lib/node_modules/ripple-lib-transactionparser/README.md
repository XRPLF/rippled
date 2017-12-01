
ripple-lib-transactionparser
----------------------------

[![NPM](https://nodei.co/npm/ripple-lib-transactionparser.png)](https://www.npmjs.org/package/ripple-lib-transactionparser)

Parses transaction objects to a higher-level view.

### parseBalanceChanges(metadata)

Takes a transaction metadata object (as returned by a ripple-lib response) and computes the balance changes that were caused by that transaction.

The return value is a javascript object in the following format:

```javascript
{ RIPPLEADDRESS: [BALANCECHANGE, ...], ... }
```

where `BALANCECHANGE` is a javascript object in the following format:

```javascript
{
    counterparty: RIPPLEADDRESS,
    currency: CURRENCYSTRING,
    value: DECIMALSTRING
}
```

The keys in this object are the Ripple [addresses](https://wiki.ripple.com/Accounts) whose balances have changed and the values are arrays of objects that represent the balance changes. Each balance change has a counterparty, which is the opposite party on the trustline, except for XRP, where the counterparty is set to the empty string.

The `CURRENCYSTRING` is 'XRP' for XRP, a 3-letter ISO currency code, or a 160-bit hex string in the [Currency format](https://wiki.ripple.com/Currency_format).


### parseOrderbookChanges(metadata)

Takes a transaction metadata object and computes the changes in the order book caused by the transaction. Changes in the orderbook are analogous to changes in [`Offer` entries](https://wiki.ripple.com/Ledger_Format#Offer) in the ledger.


The return value is a javascript object in the following format:

```javascript
{ RIPPLEADDRESS: [ORDERCHANGE, ...], ... }
```

where `ORDERCHANGE` is a javascript object with the following format:

```javascript
{
    direction: 'buy' | 'sell',
    quantity: {
        currency: CURRENCYSTRING,
        counterparty: RIPPLEADDRESS,  (omitted if currency is 'XRP')
        value: DECIMALSTRING
    },
    totalPrice: {
        currency: CURRENCYSTRING,
        counterparty: RIPPLEADDRESS,  (omitted if currency is 'XRP')
        value: DECIMALSTRING
    },
    makerExchangeRate: DECIMALSTRING,
    sequence: SEQUENCE,
    status: ORDER_STATUS
}
```


The keys in this object are the Ripple [addresses](https://wiki.ripple.com/Accounts) whose orders have changed and the values are arrays of objects that represent the order changes.

The `SEQUENCE` is the sequence number of the transaction that created that create the orderbook change. (See: https://wiki.ripple.com/Ledger_Format#Offer)
The `CURRENCYSTRING` is 'XRP' for XRP, a 3-letter ISO currency code, or a 160-bit hex string in the [Currency format](https://wiki.ripple.com/Currency_format).

The `makerExchangeRate` field provides the original value of the ratio of what the taker pays over what the taker gets (also known as the "quality").

The `ORDER_STATUS` is a string that represents the status of the order in the ledger:

*   `"created"`: The transaction created the order. The values of `quantity` and `totalPrice` represent the values of the order.
*   `"open"`: The transaction modified the order (i.e., the order was partially consumed). The values of `quantity` and `totalPrice` represent the change in value of the order.
*   `"closed"`: The transaction consumed the order. The values of `quantity` and `totalPrice` represent the change in value of the order.
*   `"canceled"`: The transaction canceled the order. The values of `quantity` and `totalPrice` is zero.
