## Introduction

The config file for side chain servers that run as federators require three
addition configuration stanzas. One additional stanza is required if the
federator will run in standalone mode, and one existing stanza (`ips_fixed`) can
be useful if running a side chain network on the local machine.

## The `[sidechain]` stanza

This stanza defines the side chain top level parameters. This includes:
* The federator's signing key. This is needed to add a signature to a
  mutli-signed transaction before submitting it on the main chain or the side
  chain.
* The main chain account. This is the account controlled by the federators and
  the account users will send their assets to initiate cross chain transactions.
  Some documentation calls this the main chain "door" account.
* The ip address and port of the main chain. This is needed to communicate with
  the main chain server.

An example stanza may look like this (where the "X" are part of a secret key):
```
[sidechain]
signing_key=sXXXXXXXXXXXXXXXXXXXXXXXXXXXX
mainchain_account=rDj4pMuPv8gAD5ZvUrpHza3bn6QMAK6Zoo
mainchain_ip=127.0.0.1
mainchain_port_ws=6007
```

## The `[sidechain_federators]` stanza

This stanza defines the signing public keys of the sidechain federators. This is
needed to know which servers to collect transaction signatures from. An example
stanza may look like this:

```
[sidechain_federators]
aKNmFC2QWXbCUFq9XxaLgz1Av6SY5ccE457zFjSoNwaFPGEwz6ab
aKE9m7iDjhy5QAtnrmE8RVbY4RRvFY1Fn3AZ5NN2sB4N9EzQe82Z
aKNFZ3L7Y7z8SdGVewkVuqMKmDr6bqmaErXBdWAVqv1cjgkt1X36
aKEhTF5hRYDenn2Rb1NMza1vF9RswX8gxyJuuYmz6kpU5W6hc7zi
aKEydZ5rmPm7oYQZi9uagk8fnbXz4gmx82WBTJcTVdgYWfRBo1Mf
```

## The `[sidechain_assets]` and associated stanzas.

These stanza define what asset is used as the cross chain asset between the main
chain and the side chain. The `mainchain_asset` is the asset that accounts on
the main chain send to the account controlled by the federators to initiate an
cross chain transaction. The `sidechain_asset` is the asset that will be sent to
the destination address on the side chain. When returning an asset from the side
chain to the main chain, the `sidechain_asset` is sent to the side chain account
controlled by the federators and the `mainchain_asset` will be sent to the
destination address on the main chain. There are amounts associated with these
two assets. These amount define an exchange rate. If the value of the main chain
asset is 1, and the amount of the side chain asset is 2, then for every asset
locked on the main chain, twice of many assets are sent on the side chain.
Similarly, for every asset returned from the main chain, half as many assets are
sent on the main chain. The format used to specify these amounts is the same as
used in json RPC commands.

There are also fields for "refund_penalty" on the main chain and side chain.
This is the amount to deduct from refunds if a transaction fails. For example,
if a cross chain transaction sends 1 XRP to an address on the side chain that
doesn't exist (and the reserve is greater than 1 XRP), then a refund is issued
on the main chain. If the `mainchain_refund_penalty` is 400 drops, then the
amount returned is 1 XRP - 400 drops.

An example of stanzas where the main chain asset is XRP, and the sidechain asset
is also XRP, and the exchange rate is 1 to 1 may look like this:

```
[sidechain_assets]
xrp_xrp_sidechain_asset

[xrp_xrp_sidechain_asset]
mainchain_asset="1"
sidechain_asset="1"
mainchain_refund_penalty="400"
sidechain_refund_penalty="400"
```


An example of stanzas where the main chain asset is USD/rD... and the side chain
asset is USD/rHb... and the exchange rate is 1 to 2 may look like this:

```
[sidechain_assets]
iou_iou_sidechain_asset

[iou_iou_sidechain_asset]
mainchain_asset={"currency": "USD", "issuer": "rDj4pMuPv8gAD5ZvUrpHza3bn6QMAK6Zoo", "value": "1"}
sidechain_asset={"currency": "USD", "issuer": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", "value": "2"}
mainchain_refund_penalty={"currency": "USD", "issuer": "rDj4pMuPv8gAD5ZvUrpHza3bn6QMAK6Zoo", "value": "0.02"}
sidechain_refund_penalty={"currency": "USD", "issuer": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", "value": "0.04"}
```



## The `[sidechain_federators_secrets]` stanza

When running a side chain with a single federator in stand alone mode (useful
for debugging), that single server needs to know the signing keys of all the
federators in order to submit transactions. This stanza will not normally only
be part of configuration files that are used for testing and debugging.

An example of a stanza with federator secrets may look like this (where the "X"
are part of a secret key).

```
[sidechain_federators_secrets] 
sXXXXXXXXXXXXXXXXXXXXXXXXXXXX
sXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
sXXXXXXXXXXXXXXXXXXXXXXXXXXXX
sXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
sXXXXXXXXXXXXXXXXXXXXXXXXXXXX
```

## The `[ips_fixed]` stanza

When running a test net it can be useful to hard code the ip addresses of the
side chain servers. An example of such a stanza used to run a test net locally
may look like this:

```
[ips_fixed]
127.0.0.2 51238
127.0.0.3 51239
127.0.0.4 51240
127.0.0.5 51241
```
