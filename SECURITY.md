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

Please report the bug directly to us and limit further disclosure. If you want to prove that you knew the bug as of a given time, consider using a cryptographic precommitment: hash the content of your report and publish the hash on a medium of your choice (e.g. on Twitter or as a memo in a transaction) as "proof" that you had written the text at a given point in time.

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

|Email Address|bugs@ripple.com                                      |
|:-----------:|:----------------------------------------------------|
|Short Key ID | `0xC57929BE`                                        |
|Long Key ID  | `0xCD49A0AFC57929BE`                                |
|Fingerprint  | `24E6 3B02 37E0 FA9C 5E96 8974 CD49 A0AF C579 29BE` |

The full PGP key for this address, which is also available on several key servers (e.g. on [keys.gnupg.net](https://keys.gnupg.net)), is: 
```
-----BEGIN PGP PUBLIC KEY BLOCK-----
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
7Tz8SQkuxltX/yBXMV3Ddy0t6nWV2SZEfuxJAQI=
=spg4
-----END PGP PUBLIC KEY BLOCK-----
```
