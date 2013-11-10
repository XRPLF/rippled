# PeerFinder

The PeerFinder module has these responsibilities:

- Maintain a set of addresses suitable for bootstrapping into the overlay.
- Send and receive protocol messages for peer address discovery.
- Provide network addresses to other peers that need them.
- Maintain connections to the configured set of fixed peers.
- Track and manage peer connection slots.

## Description

## Terms

<table>
<tr>
  <td>Bootstrap</td>
  <td>The process by which a Ripple peer obtains the initial set of
      connections into the Ripple payment network overlay.
  </td></tr>
</tr>
<tr>
  <td>Overlay</td>
  <td>The connected graph of Ripple peers, overlaid on the public Internet.
  </td>
</tr>
<tr>
  <td>Peer</td>
  <td>A network server running the **rippled** daemon.
  </td>
</tr>
</table>
