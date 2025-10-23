## Renaming ripple(d) to xrpl(d)

In the initial phases of development of the XRPL, the open source codebase was
called "rippled" and it remains with that name even today. Today, over 1000
nodes run the application, and code contributions have been submitted by
developers located around the world. The XRPL community is larger than ever.
In light of the decentralized and diversified nature of XRPL, we will rename any
references to `ripple` and `rippled` to `xrpl` and `xrpld`, when appropriate.

See [here](https://xls.xrpl.org/xls/XLS-0095-rename-rippled-to-xrpld.html) for
more information.

### Scripts

To facilitate this transition, there will be multiple scripts that developers
can run on their own PRs and forks to minimize conflicts. Each script should be
run from the repository root.

1. `.github/scripts/rename/definitions.sh`: This script will rename all
   definitions, such as include guards, from `RIPPLE_XXX` and `RIPPLED_XXX` to
   `XRPL_XXX`.
