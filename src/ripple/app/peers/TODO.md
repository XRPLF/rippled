# Peer.cpp

- Move magic number constants into Tuning.h / Peer constants enum 

- Wrap lines to 80 columns

- Use Journal

- Pass Journal in the ctor argument list

- Use m_socket->remote_endpoint() instead of m_remoteAddress in all cases.
  For async_connect pass the IPAddress in the bind to onConnect completion
  handler (so we know the address if the connect fails). For PROXY, to recover
  the original remote address (of the ELB) use m_socket->next_layer()->remote_endpoint(),
  and use m_socket->remote_endpoint() to get the "real IP" reported in the PROXY
  handshake.

- Handle operation_aborted correctly, work with Peers.cpp to properly handle
  a stop. Peers need to be gracefully disconnected, the listening socket closed
  on the stop to prevent new connections (and new connections that slip
  through should be refused). The Peers object needs to know when the last
  Peer has finished closing either gracefully or from an expired graceful
  close timer.

- Handle graceful connection closure (With a graceful close timeout). During
  a graceful close, throttle incoming data (by not issuing new socket reads),
  discard any received messages, drain the outbound queue, and tear down
  the socket when the last send completes.

- PeerImp should construct with the socket, using a move-assign or swap.
  PeerDoor should not have to create a Peer object, it should just create a
  socket, accept the connection, and then construct the Peer object.

- No more strings for IP addresses and ports. Always use IPAddress. We can
  have a version of connect() that takes a string but it should either convert
  it to IPAddress if its parseable, else perform a name resolution.

- Stop calling getNativeSocket() this and that, just go through m_socket.

- Properly handle operation_aborted in all the callbacks.

- Move all the function definitions into the class declaration.

- Replace macros with language constants.

- Stop checking for exceptions, just handle errors correctly.

- Move the business logic out of the Peer class. Socket operations and business
  logic should not be mixed together. We can declare a new class PeerLogic to
  abstract the details of message processing.

- Change m_nodePublicKey from RippleAddress to RipplePublicKey, change the
  header, and modify all call sites to use the new type instead of the old. This
  might require adding some compatibility functions to RipplePublicKey.

- Remove public functions that are not used outside of Peer.cpp

# Peers.cpp

- Add Peer::Config instead of using getConfig() to pass in configuration data

# PeerSet.cpp

- Remove to cyclic dependency on InboundLedger (logging only)

- Convert to Journal
