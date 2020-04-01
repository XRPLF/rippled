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
For example, `uint32` is initialized to `0` if not explicitly set;
there is no way to tell if the client or server set the field to `0` (which may be
a valid value for the field) or the field was default initialized.

#### Name Collisions

Each message type must have a unique name. To resolve collisions, add a suffix
to one or more message types. For instance, ledger objects and transaction types
often have the same name (`DepositPreauth` for example). To resolve this, the
`DepositPreauth` ledger object is named `DepositPreauthObject`.

#### To add a field or message type

To add a field to a message, define the fields type, name and unique index.
To add a new message type, give the message type a unique name.
Then, add the appropriate C++ code in GRPCHelpers.cpp, or in the handler itself,
to serialize/deserialize the new field or message type.

#### To add a new gRPC method

To add a new gRPC method, add the gRPC method in xrp_ledger.proto. The method name
should begin with a verb. Define the request and response types in their own
file. The name of the request type should be the method name suffixed with `Request`, and
the response type name should be the method name suffixed with `Response`. For
example, the `GetAccountInfo` method has request type `GetAccountInfoRequest` and
response type `GetAccountInfoResponse`.

After defining the protobuf messages for the new method, add an instantiation of the
templated `CallData` class in GRPCServerImpl::setupListeners(). The template
parameters should be the request type and the response type.

Finally, define the handler itself in the appropriate file under the
src/ripple/rpc/handlers folder. If the method already has a JSON/Websocket
equivalent, write the gRPC handler in the same file, and abstract common logic
into helper functions (see Tx.cpp or AccountTx.cpp for an example).

#### Testing

When modifying an existing gRPC method, be sure to test that modification in the
corresponding, existing unit test. When creating a new gRPC method, implement a class that
derives from GRPCTestClientBase, and use the newly created class to call the new
method. See the class `GrpcTxClient` in the file Tx_test.cpp for an example.
The gRPC tests are paired with their JSON counterpart, and the tests should
mirror the JSON test as much as possible.

Refer to the Protocol Buffers [language
guide](https://developers.google.com/protocol-buffers/docs/proto3)
for more detailed information about Protocol Buffers.

