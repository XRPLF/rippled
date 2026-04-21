### Operating an XRP Ledger server securely

For more details on operating an XRP Ledger server securely, please visit https://xrpl.org/manage-the-rippled-server.html.

# Security Policy

## Supported Versions

Software constantly evolves. In order to focus resources, we only generally only accept vulnerability reports that affect recent and current versions of the software. We always accept reports for issues present in the **master**, **release** or **develop** branches, and with proposed, [open pull requests](https://github.com/ripple/rippled/pulls).

## Identifying and Reporting Vulnerabilities

We take security seriously and we do our best to ensure that all our releases are bug free. But we aren't perfect and sometimes things will slip through.

### Responsible Investigation

We urge you to examine our code carefully and responsibly, and to disclose any issues that you identify in a responsible fashion.

Responsible investigation includes, but isn't limited to, the following:

- Not performing tests on the main network. If testing is necessary, use the [Testnet or Devnet](https://xrpl.org/xrp-testnet-faucet.html).
- Not targeting physical security measures, or attempting to use social engineering, spam, distributed denial of service (DDOS) attacks, etc.
- Investigating bugs in a way that makes a reasonable, good faith effort not to be disruptive or harmful to the XRP Ledger and the broader ecosystem.

### Responsible Disclosure

If you discover a vulnerability or potential threat, or if you _think_
you have, please reach out by dropping an email using the contact
information below.

Your report should include the following:

- Your contact information (typically, an email address);
- The description of the vulnerability;
- The attack scenario (if any);
- The steps to reproduce the vulnerability;
- Any other relevant details or artifacts, including code, scripts or patches.

In your email, please describe the issue or potential threat. If possible, include a "repro" (code that can reproduce the issue) or describe the best way to reproduce and replicate the issue. Please make your report as detailed and comprehensive as possible.

For more information on responsible disclosure, please read this [Wikipedia article](https://en.wikipedia.org/wiki/Responsible_disclosure).

## Report Handling Process

Please report the bug directly to us and limit further disclosure. If you want to prove that you knew the bug as of a given time, consider using a cryptographic pre-commitment: hash the content of your report and publish the hash on a medium of your choice (e.g. on Twitter or as a memo in a transaction) as "proof" that you had written the text at a given point in time.

Once we receive a report, we:

1. Assign two people to independently evaluate the report;
2. Consider their recommendations;
3. If action is necessary, formulate a plan to address the issue;
4. Communicate privately with the reporter to explain our plan.
5. Prepare, test and release a version which fixes the issue; and
6. Announce the vulnerability publicly.

We will triage and respond to your disclosure within 24 hours. Beyond that, we will work to analyze the issue in more detail, formulate, develop and test a fix.

While we commit to responding with 24 hours of your initial report with our triage assessment, we cannot guarantee a response time for the remaining steps. We will communicate with you throughout this process, letting you know where we are and keeping you updated on the timeframe.

## Bug Bounty Program

[Ripple](https://ripple.com) is generously sponsoring a bug bounty program for vulnerabilities in [`rippled`](https://github.com/XRPLF/rippled) (and other related projects, like [`xrpl.js`](https://github.com/XRPLF/xrpl.js), [`xrpl-py`](https://github.com/XRPLF/xrpl-py), [`xrpl4j`](https://github.com/XRPLF/xrpl4j)).

This program allows us to recognize and reward individuals or groups that identify and report bugs. In summary, in order to qualify for a bounty, the bug must be:

1. **In scope**. Only bugs in software under the scope of the program qualify. Currently, that means `rippled`, `xrpl.js`, `xrpl-py`, `xrpl4j`.
2. **Relevant**. A security issue, posing a danger to user funds, privacy, or the operation of the XRP Ledger.
3. **Original and previously unknown**. Bugs that are already known and discussed in public do not qualify. Previously reported bugs, even if publicly unknown, are not eligible.
4. **Specific**. We welcome general security advice or recommendations, but we cannot pay bounties for that.
5. **Fixable**. There has to be something we can do to permanently fix the problem. Note that bugs in other people’s software may still qualify in some cases. For example, if you find a bug in a library that we use which can compromise the security of software that is in scope and we can get it fixed, you may qualify for a bounty.
6. **Unused**. If you use the exploit to attack the XRP Ledger, you do not qualify for a bounty. If you report a vulnerability used in an ongoing or past attack and there is specific, concrete evidence that suggests you are the attacker we reserve the right not to pay a bounty.

The amount paid varies dramatically. Vulnerabilities that are harmless on their own, but could form part of a critical exploit will usually receive a bounty. Full-blown exploits can receive much higher bounties. Please don’t hold back partial vulnerabilities while trying to construct a full-blown exploit. We will pay a bounty to anyone who reports a complete chain of vulnerabilities even if they have reported each component of the exploit separately and those vulnerabilities have been fixed in the meantime. However, to qualify for a the full bounty, you must to have been the first to report each of the partial exploits.

### Contacting Us

To report a qualifying bug, please send a detailed report to:

| Email Address | bugs@ripple.com                                     |
| :-----------: | :-------------------------------------------------- |
| Short Key ID  | `0xA9F514E0`                                        |
|  Long Key ID  | `0xD900855AA9F514E0`                                |
|  Fingerprint  | `B72C 0654 2F2A E250 2763 A268 D900 855A A9F5 14E0` |

The full PGP key for this address, which is also available on several key servers (e.g. on [keyserver.ubuntu.com](https://keyserver.ubuntu.com)), is:

```
-----BEGIN PGP PUBLIC KEY BLOCK-----
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
=nGuD
-----END PGP PUBLIC KEY BLOCK-----
```
