# TestOverlay

This provides a set of template classes for simulating a peer to peer
network. These facilities are provided:

- Initial construction of the network.
- Message passing between peers
- Network wide state information.
- Per-peer state information.

## Description

Through the use of suitable template arguments, the logic and state information
for each peer can be customized. Messages are packets of arbitrary size with
template-parameter defined data. The network is modeled discretely; The time
evolution of the network is defined by successive steps where messages are
always delivered reliably on the next step after which they are sent.
