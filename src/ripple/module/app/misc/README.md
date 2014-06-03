
# Amendment

An Amendment is a new or proposed change to a ledger rule. Ledger rules affect 
transaction processing and consensus; peers must use the same set of rules for 
consensus to succeed, otherwise different instances of rippled will get 
different results. Amendments can be almost anything but they must be accepted 
by a network majority through a consensus process before they are utilized. An 
Amendment must receive at least an 80% approval rate from validating nodes for 
a period of two weeks before being accepted. The following example outlines the 
process of an Amendment from its conception to approval and usage. 

*	A community member makes proposes to change transaction processing in some 
  way. The proposal is discussed amongst the community and receives its support 
  creating a community or human consensus. 

*	Some members contribute their time and work to develop the Amendment.

*	A pull request is created and the new code is folded into a rippled build 
  and made available for use.

*	The consensus process begins with the validating nodes.

*	If the Amendment holds an 80% majority for a two week period, nodes will begin 
  including the transaction to enable it in their initial sets.

Nodes may veto Amendments they consider undesirable by never announcing their 
support for those Amendments. Just a few nodes vetoing an Amendment will normally 
keep it from being accepted. Nodes could also vote yes on an Amendments even 
before it obtains a super-majority. This might make sense for a critical bug fix.