This folder contains the protocol buffer definitions used by the rippled gRPC API.
The gRPC API attempts to mimic the JSON/Websocket API as much as possible.
As of April 2020, the gRPC API supports a subset of the full rippled API:
tx, account_tx, account_info, fee and submit.

### Making Changes
#### Wire Format and Backwards Compatibility
When making changes to the protocol buffer definitions in this folder, care must
be taken to ensure the changes do not break the wire format, which would break
backwards compatibility. At a high level, do not change any existing fields.
This includes the field's name, type and field number. Do not remove any
existing fields. It is always safe to add fields; just remember to give each of
the new fields a unique field number. The field numbers don't have to be in any
particular order and there can be gaps. More info about what changes break the
wire format can be found
[here](https://developers.google.com/protocol-buffers/docs/proto3#updating).
#### Conventions
For fields that are reused across different message types, we define the field as a unique
message type in common.proto. The name of the message type is the same as the
field name, with the exception that the field name itself is snake case, whereas
the message type is in Pascal case. The message type has one field, called
`value`. This pattern does not need to be strictly followed across the entire API,
but should be followed for transactions and ledger objects, since there is a high rate
of field reuse across different transactions and ledger objects.
The motivation for this pattern is two-fold. First, we ensure the field has the
same type everywhere that the field is used. Second, wrapping primitive types in
their own message type prevents default initialization of those primitive types.
For example, `uint32` is always initialized to `0`, even if never explicitly set;
there is no way to tell if the client or server set the field to `0` (which may be
a valid value for the field) or the field was default initialized.

Refer to the Protocol Buffers [language
guide](https://developers.google.com/protocol-buffers/docs/proto3)
for more detailed information about Protocol Buffers.

