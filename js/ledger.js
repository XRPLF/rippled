//
// Tools for working with ledger entries.
//
// Fundamentally, we work in the more strict case of not trusting the ledger as presented.  If we have a server we trust, then the
// burden of verify the ledger entries is left to the server.  But, we work in the same fundamental units of information, ledger
// entries, to keep the code orthagonal.
//

var serializer = require("./serializer");

exports.getLedgerEntry = function(key, done) {
	var	id	= (ws.id += 1);

	ws.response[id] = done;

	ws.send({
			'command': 'getLedgerEntry',
			'id': id,
			'ledger': ledger,
			'account': accountId,
			'proof': false			// Eventually, we will want proof if the server is untrusted.
		});
};

exports.getAccountRootNode = function(ledger, accountId, done) {
	var s	= new Serializer();

	s.addUInt16('a');
	s.addUInt160(accountId);

	getLedgerEntry(s.getSHA512Half(), done);
};

// vim:ts=4
