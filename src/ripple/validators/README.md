# Validators

The Validators module has these responsibilities:

- Provide an administrative interface for maintaining the list _Source_
  locations.
- Report performance statistics on _Source_ locations
- Report performance statistics on _validators_ provided by _Source_ locations.
- Choose a suitable random subset of observed _Validators_ to become the
  _Chosen Validators_ set.
- Update the _Chosen Validators_ set as needed to meet performance requirements.

## Description

The consensus process used by the Ripple payment protocol requires that ledger
hashes be signed by _Validators_, producing a _Validation_. The integrity of
the process is mathematically assured when each node chooses a random subset
of _Validators_ to trust, where each _Validator_ is a public verifiable entity
that is independent. Or more specifically, no entity should be in control of
any significant number of _validators_ chosen by each node.

The list of _Validators_ a node chooses to trust is called the _Chosen
Validators_. The **Validators** module implements business logic to automate the
selection of _Chosen Validators_ by allowing the administrator to provide one
or more trusted _Sources_, from which _Validators_ are learned. Performance
statistics are tracked for these _Validators_, and the module chooses a
suitable subset from which to form the _Chosen Validators_ list.

The module looks for these criteria to determine suitability:

- Different validators are not controlled by the same entity.
- Each validator participates in a majority of ledgers.
- A validator does not sign ledgers that fail the consensus process.

## Terms

<table>
<tr>
  <td>Chosen Validators</td>
  <td>A set of validators chosen by the Validators module. This is the new term
      for what was formerly known as the Unique Node List.
  </td>
</tr>
<tr>
  <td>Source</td>
  <td>A trusted source of validator descriptors. Examples: the rippled
      configuration file, a local text file,  or a trusted URL such
      as https://ripple.com/validators.txt.
  </td></tr>
</tr>
<tr>
  <td>Validation</td>
  <td>A closed ledger hash signed by a validator.
  </td>
</tr>
<tr>
  <td>Validator</td>
  <td>A publicly verifiable entity which signs ledger hashes with its private
      key, and makes its public key available through out of band means.
  </td>
</tr>
</table>
