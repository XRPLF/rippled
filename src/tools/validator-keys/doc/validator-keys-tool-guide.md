# Validator Keys Tool Guide

This guide explains how to set up a validator so its public key does not have to
change if the xrpld config and/or server are compromised.

A validator uses a public/private key pair. The validator is identified by the
public key. The private key should be tightly controlled. It is used to:

- sign tokens authorizing an xrpld server to run as the validator identified
  by this public key.
- sign revocations indicating that the private key has been compromised and
  the validator public key should no longer be trusted.

Each new token invalidates all previous tokens for the validator public key.
The current token needs to be present in the xrpld config file.

Servers that trust the validator will adapt automatically when the token
changes.

## Validator Keys

When first setting up a validator, use the `validator-keys` tool to generate
its key pair:

```
  $ validator-keys create_keys
```

Sample output:

```
  Validator keys stored in /home/ubuntu/.ripple/validator-keys.json
```

Keep the key file in a secure but recoverable location, such as an encrypted
USB flash drive. Do not modify its contents.

## Validator Token

After first creating the [validator keys](#validator-keys) or if the previous
token has been compromised, use the `validator-keys` tool to create a new
validator token:

```
  $ validator-keys create_token
```

Sample output:

```
  Update xrpld.cfg file with these values:

  # validator public key: nHUtNnLVx7odrz5dnfb2xpIgbEeJPbzJWfdicSkGyVw1eE5GpjQr

  [validator_token]
  <base64 token, in 72-character lines>
```

For a new validator, add the [validator_token] value to the xrpld config file.
For a pre-existing validator, replace the old [validator_token] value with the
newly generated one. A valid config file may only contain one [validator_token]
value. After the config is updated, restart xrpld.

There is a hard limit of 4,294,967,294 tokens that can be generated for a given
validator key pair.

## Key Revocation

If a validator private key is compromised, the key must be revoked permanently.
To revoke the validator key, use the `validator-keys` tool to generate a
revocation, which indicates to other servers that the key is no longer valid:

```
  $ validator-keys revoke_keys
```

Sample output:

```
  WARNING: This will revoke your validator keys!

  Update xrpld.cfg file with these values and restart xrpld:

  # validator public key: nHUtNnLVx7odrz5dnfb2xpIgbEeJPbzJWfdicSkGyVw1eE5GpjQr

  [validator_key_revocation]
  <base64 revocation, in 72-character lines>
```

Add the `[validator_key_revocation]` value to this validator's config and
restart xrpld. Rename the old key file and generate new [validator keys](#validator-keys) and
a corresponding [validator token](#validator-token).

## Signing

The `validator-keys` tool can be used to sign arbitrary data with the validator
key.

```
  $ validator-keys sign "your data to sign"
```

Sample output:

```
  B91B73536235BBA028D344B81DBCBECF19C1E0034AC21FB51C2351A138C9871162F3193D7C41A49FB7AABBC32BC2B116B1D5701807BE462D8800B5AEA4F0550D
```

## External Signing

The master key can live in a hardware signer instead of the key file. The tool
then produces the bytes to sign, the hardware signs them, and the tool
assembles the result.

One-time setup, with the hardware key's public key in base58 (`nHB...`), hex
(`ED...`) or base64:

```
  $ validator-keys create_external <public key>
```

The key file is written with `"secret_key": "external"`.

To create a token, print the bytes to sign, sign them externally, and pass the
hex or base64 signature back:

```
  $ validator-keys start_token
  $ validator-keys finish_token <signature>
```

The output is the same `[validator_token]` block `create_token` prints. A
revocation works the same way with `start_revoke_keys` and
`finish_revoke_keys <signature>`.

When the signing key is also held by the hardware, name it when starting the
token. The bytes are then signed twice, once by each key, and the result is a
manifest without a secret:

```
  $ validator-keys start_token --signing-key <signing public key>
  $ validator-keys finish_token <master signature> <signing signature>
```

A secp256k1 signature from an external signer must be in the fully canonical
form a server accepts (a low `S` value in the DER encoding); the tool rejects
any other form. An ed25519 signature has one form.

`set_domain` on an external master key stores the domain for the next token
made with `start_token` and `finish_token`, and `attest_domain` then prints the
bytes of the attestation for the external signer; the hex signature is the
attestation.

For testing without a hardware signer, a second key file can stand in for it:

```
  $ validator-keys --keyfile other-keys.json sign_hex <bytes>
```

## Validator List Publishers

A publisher's keys are a master key and a signing key bound to it by a
manifest, exactly like a validator's. The token `create_token` prints holds
that manifest and the signing key, so a publisher's setup is:

```
  $ validator-keys create_keys
  $ validator-keys create_token --token-key-type ed25519 --out publisher-token.txt
```

`--token-key-type ed25519` matches what hardware signers support; it is for a
publisher's signing key only, since xrpld loads secp256k1 tokens from
`[validator_token]` and no other. `--out` writes the token to a file readable
only by its owner instead of printing it.

The manifest's sequence is `token_sequence` in the key file. A server keeps
the highest sequence it has seen for a master key, so a key migrated from
another tool must start `token_sequence` above the sequence of the manifest
currently published.

### Signing a list

The unsigned list is a JSON file with the fields a server reads:

```json
{
  "sequence": 2026091301,
  "expiration": 843955200,
  "validators": [
    { "validation_public_key": "ED...", "manifest": "<base64 manifest>" }
  ]
}
```

`sequence` must rise with every published list, `expiration` and the optional
`effective` are seconds since the XRP Ledger epoch, and each validator's
`manifest` must belong to its `validation_public_key`. Whitespace is removed
and one space is placed after each `,` and `:` before signing.

```
  $ validator-keys sign_list unsigned.json --token-file publisher-token.txt --out vl.json
```

`vl.json` is the version 1 document a server fetches: `blob`, `manifest`,
`public_key`, `signature`, `version`. `--list-version 2` writes the blob into
`blobs_v2` instead, and `--append <existing.json>` adds it to a version 2
document that already holds up to four blobs, so a list can be published
alongside the one it will replace.

When the signing key is held by a hardware signer, the list is signed in two
steps against the manifest from `finish_token`:

```
  $ validator-keys start_sign_list unsigned.json --manifest-file manifest.txt
  $ validator-keys finish_sign_list <signature> unsigned.json --manifest-file manifest.txt --out vl.json
```

### Checking a list

```
  $ validator-keys verify_list vl.json --validators unsigned.json --expected-key <master public key>
```

The checks are the ones a server makes before trusting the list: the manifest
verifies and names `public_key`, every blob's signature verifies under the
manifest's signing key, every blob parses, and none has expired. `--validators`
requires every blob to list exactly the keys in the unsigned list, and
`--expected-key` requires the master key to be the one given. The result is
printed as JSON and the exit code is 0 only if every check passed.
