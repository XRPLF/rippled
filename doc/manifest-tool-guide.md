# Manifest Tool Guide

This guide explains how to setup a validator so the key pairs used to sign and
verify validations may safely change. This procedure does not require manual
reconfiguration of servers that trust this validator.

Validators use two types of key pairs: *master keys* and *ephemeral
keys*. Ephemeral keys are used to sign and verify validations. Master keys are
used to sign and verify manifests that change ephemeral keys. The master secret
key should be tightly controlled. The ephemeral secret key needs to be present
in the config file.

## Validator Keys

When first setting up a validator, use the `manifest` script to generate a
master key pair:

```
  $ bin/manifest create
```

Sample output:
```
  [validator_keys]
  nHUSSzGw4A9zEmFtK2Q2NcWDH9xmGdXMHc1MsVej3QkLTgvDNeBr

  [master_secret]
  pnxayCakmZRQE2qhEVRbFjiWCunReSbN1z64vPL36qwyLgogyYc
```

The first value is the master public key. Add the public key to the config
for this validator. A one-word comment must be added after the key (for example
*ThisServersName*). Any other rippled trusting the validator needs to add the
master public key to its config. Only add keys received from trusted sources.

The second value is the corresponding master secret key. **DO NOT INSTALL THIS
IN THE CONFIG**. The master secret key will be used to sign manifests that
change validation keys. Put the master secret key in a secure but recoverable
location.

## Validation Keys

When first setting up a validator, or when changing the ephemeral keys, use the
`rippled` program to create a new ephemeral key pair:

```
  $ rippled validation_create
```

Sample output:

```
  Loading: "/Users/alice/.config/ripple/rippled.cfg"
  Securely connecting to 127.0.0.1:5005
  {
     "result" : {
      "status" : "success",
      "validation_key" : "TOO EDNA SHUN FEUD STAB JOAN BIAS FLEA WISE BOHR LOSS WEEK",
      "validation_public_key" : "n9JzKV3ZrcZ3DW5pwjakj4hpijJ9oMiyrPDGJc3mpsndL6Gf3zwd",
      "validation_seed" : "sahzkAajS2dyhjXg2yovjdZhXmjsx"
     }
  }
```

Add the `validation_seed` value (the ephemeral secret key) to this validator's
config. It is recommended to add the ephemeral public key and the sequence
number as a comment as well (sequence numbers are be explained below):

```
  [validation_seed]
  sahzkAajS2dyhjXg2yovjdZhXmjsx
  # validation_public_key: n9JzKV3ZrcZ3DW5pwjakj4hpijJ9oMiyrPDGJc3mpsndL6Gf3zwd
  # sequence number: 1
```

A manifest is a signed message used to inform other servers of this validator's
ephemeral public key. A manifest contains a sequence number, the new ephemeral
public key, and it is signed with the master secret key. The sequence number
should be higher than the previous sequence number (if it is not, the manifest
will be ignored). Usually the previous sequence number will be incremented by
one. Use the `manifest` script to create a manifest. It has the form:

```
 $ bin/manifest sign sequence_number validation_public_key master_secret
```

For example:

```
  $ bin/manifest sign 1 n9JzKV3Z...L6Gf3zwd pnxayCak...yLgogyYc
```

Sample output:

```
  [validation_manifest]
  JAAAAAFxIe2PEzNhe996gykB1PJQNoDxvr/Y0XhDELw8d/i
  Fcgz3A3MhAjqhKsgZTmK/3BPEI+kzjV1p9ip7pl/AtF7CKd
  NSfAH9dkCxezV6apS4FLYzAcQilONx315HvebwAB/pLPaM4
  2sWCEppSuLNKN/JJjTABOo9tmAiNnnstF83yvecKMJzniwN
```  

Copy this to the config for this validator. Don't forget to update the comment
noting the sequence number.

## Revoking a key

If a master key is compromised, the key may be revoked permanently. To revoke a
master key, sign a manifest with the highest possible sequence number:
`4,294,967,295`
