### Operating an XRP Ledger server securely

For more details on operating an XRP Ledger server securely, please visit https://xrpl.org/manage-the-rippled-server.html.

# Security Policy

## Supported Versions

Software constantly evolves. In order to focus resources, we generally only accept vulnerability reports that affect recent and current versions of the software. We always accept reports for issues present in the **master**, **release** or **develop** branches, and with proposed, [open pull requests](https://github.com/XRPLF/rippled/pulls).

## Identifying and Reporting Vulnerabilities

We take security seriously and we do our best to ensure that all our releases are bug free. But we aren't perfect and sometimes things will slip through.

### Responsible Investigation

We urge you to examine our code carefully and responsibly, and to disclose any issues that you identify in a responsible fashion.

Responsible investigation includes, but isn't limited to, the following:

- Not performing tests on the main network. If testing is necessary, use the [Testnet or Devnet](https://xrpl.org/xrp-testnet-faucet.html).
- Not targeting physical security measures, or attempting to use social engineering, spam, distributed denial of service (DDOS) attacks, etc.
- Investigating bugs in a way that makes a reasonable, good faith effort not to be disruptive or harmful to the XRP Ledger and the broader ecosystem.

## Bug Bounty Program

[Ripple](https://ripple.com) is generously sponsoring a bug bounty program for vulnerabilities in [`xrpld`](https://github.com/XRPLF/rippled) (and other related projects, like [`Clio`](https://github.com/XRPLF/clio), [`xrpl.js`](https://github.com/XRPLF/xrpl.js), [`xrpl-py`](https://github.com/XRPLF/xrpl-py), [`xrpl4j`](https://github.com/XRPLF/xrpl4j)).

This program allows us to recognize and reward individuals or groups that identify and report bugs.

We have partnered with Bugcrowd to manage this program. It is a private program, and security researchers can participate based on invitation. If you need access to the program, please email bugs@ripple.com with your Bugcrowd handle or Bugcrowd registered email, and we will get you added to the program. Once you have been added, please submit vulnerability reports through Bugcrowd, not by email. The detailed bug bounty policy is available on the Bugcrowd website.
