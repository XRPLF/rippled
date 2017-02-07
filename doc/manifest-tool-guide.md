# Manifest Tool Guide

This guide explains how to setup a validator so the key pairs used to sign and
verify validations may safely change. This procedure does not require manual
reconfiguration of servers that trust this validator.

Validators use two types of key pairs: *master keys* and *ephemeral
keys*. Ephemeral keys are used to sign and verify validations. Master keys are
used to sign and verify manifests that change ephemeral keys. The master secret
key should be tightly controlled. The ephemeral secret key needs to be present
in the config file in the form of a token.

## Validator Keys

When first setting up a validator, use the `manifest` script to generate a
master key pair:

```
  $ bin/manifest create
```

Sample output:
```
  [validator_keys]
  nHDwNP6jbJgVKj5zSEjMn8SJhTnPV54Fx5sSiNpwqLbb42nmh3Ln

  [master_secret]
  paamfhAn5m1NM4UUu5mmvVaHQy8Fb65bkpbaNrvKwX3YMKdjzi2
```

The first value is the master public key. Any other rippled trusting the
validator needs to add the master public key to its config. Only add keys
received from trusted sources.

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
       "validation_key" : "MIN HEAT NUN FAWN HIP KAHN BORG PHI BALK ANN TWIG RACY",
       "validation_private_key" : "pn6kTqE4WyeRQzZrRp5FnJ5J2vLdSCB5KD3DEyfVw6C9woDBfED",
       "validation_public_key" : "n9LtZ9haqYMbzJ92cDd3pu3Lko6uEznrXuYea3ehuhVcwDHF5coX",
       "validation_seed" : "sh8bLqqkGBknGcsgRTrFMxcciwytm"
     }
  }
```

A manifest is a signed message used to inform other servers of this validator's
ephemeral public key. A manifest contains a sequence number, the new ephemeral
public key, and it is signed with both the ephemeral and master secret keys.
The sequence number should be higher than the previous sequence number (if it
is not, the manifest will be ignored). Usually the previous sequence number
will be incremented by one. Use the `manifest` script to create a manifest.
It has the form:

```
 $ bin/manifest sign sequence validation_public_key validation_private_key master_secret
```

For example:

```
  $ bin/manifest sign 1 n9LtZ9ha...wDHF5coX pn6kTqE4...9woDBfED paamfhAn...YMKdjzi2
```

Sample output:

```
  [validator_token]
  eyJ2YWxpZGF0aW9uX3ByaXZhdGVfa2V5IjoiN2VkMWM2ZDVmMTBhOGUyYWNmZWE0OW
  NkNzE5ZjhiZjZhMWI1Yjg2M2Q3N2QxMDE1MjVkNGQxOWU4ZWYwNjU1OSIsIm1hbmlm
  ZXN0IjoiSkFBQUFBRnhJZTNVZ3lXb3QwdFpSTFBNbDQ1dit5YkdPanJYNkE5c1JIcU
  lJWUlZSTZjREhuTWhBNlNqWWZnWnJsTHFXU2UrNzlWUkZnOWJEeVZKNDMvYmV6Zkxu
  dWkxVzhQdWRrWXdSQUlnY09KWmVpdmFaOW1abmM4Y1N4bUlOVGVYSUlKOE5oTXprRD
  lrWThseGRkc0NJRk1OU3k5Qmo4VHNQWlV4NzNwYUJ0RmxpaDZWOTlSRXNnU3V6Wlhr
  BXMHNWTVZXR09QUnY0Ylg2dDVmYi9haUtoNkZyRVdtYk5YeXpRNmI5TGhDQT09In0=
```

Copy this to the config for this validator. It is recommended to add the
sequence number as a comment as well.

## Revoking a key

If a master key is compromised, the key may be revoked permanently. To revoke a
master key, sign a manifest with the highest possible sequence number:
`4294967295`
