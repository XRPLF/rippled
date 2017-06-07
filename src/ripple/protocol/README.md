# protocol

Classes and functions for handling data and
values associated with the Ripple protocol.

## Serialized Objects

In ripple objects transmitted over the network must be
serialized into a canonical format. The prefix "ST" refers
to classes that deal with the serialized format of ripple
objects.

The term "Tx" or "tx" is an abbreviation for "Transaction",
a commonly occurring object type.

### Optional Fields

Our serialized fields have some "type magic" to make
optional fields easier to read:

- The operation `x[sfFoo]` means "return the value of 'Foo'
  if it exists, or the default value if it doesn't."
- The operation `x[~sfFoo]` means "return the value of 'Foo'
  if it exists, or nothing if it doesn't." This usage of the
  tilde/bitwise NOT operator is not standard outside of the
  `rippled` codebase.
    - As a consequence of this, `x[~sfFoo] = y[~sfFoo]`
      assigns the value of Foo from y to x, including omitting
      Foo from x if it doesn't exist in y.

Typically, for things that are guaranteed to exist, you use
`x[sfFoo]` and avoid having to deal with a container that may
or may not hold a value. For things not guaranteed to exist,
you use `x[~sfFoo]` because you want such a container. It
avoids having to look something up twice, once just to see if
it exists and a second time to get/set its value.
([Real example](https://github.com/ripple/rippled/blob/35f4698aed5dce02f771b34cfbb690495cb5efcc/src/ripple/app/tx/impl/PayChan.cpp#L229-L236))

The source of this "type magic" is in
[SField.h](./SField.h#L296-L302).
