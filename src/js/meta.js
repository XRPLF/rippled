var extend = require('extend');

/**
 * Meta data processing facility.
 */
var Meta = function (raw_data)
{
  this.nodes = [];

  for (var i = 0, l = raw_data.AffectedNodes.length; i < l; i++) {
    var an = raw_data.AffectedNodes[i],
        result = {};

    ["CreatedNode", "ModifiedNode", "DeletedNode"].forEach(function (x) {
      if (an[x]) result.diffType = x;
    });

    if (!result.diffType) return null;

    an = an[result.diffType];

    result.entryType = an.LedgerEntryType;
    result.ledgerIndex = an.LedgerIndex;

    result.fields = extend({}, an.PreviousFields, an.NewFields, an.FinalFields);
    result.fieldsPrev = an.PreviousFields || {};
    result.fieldsNew = an.NewFields || {};
    result.fieldsFinal = an.FinalFields || {};

    this.nodes.push(result);
  }
};

/**
 * Execute a function on each affected node.
 *
 * The callback is passed two parameters. The first is a node object which looks
 * like this:
 *
 *   {
 *     // Type of diff, e.g. CreatedNode, ModifiedNode
 *     diffType: 'CreatedNode'
 *
 *     // Type of node affected, e.g. RippleState, AccountRoot
 *     entryType: 'RippleState',
 *
 *     // Index of the ledger this change occurred in
 *     ledgerIndex: '01AB01AB...',
 *
 *     // Contains all fields with later versions taking precedence
 *     //
 *     // This is a shorthand for doing things like checking which account
 *     // this affected without having to check the diffType.
 *     fields: {...},
 *
 *     // Old fields (before the change)
 *     fieldsPrev: {...},
 *
 *     // New fields (that have been added)
 *     fieldsNew: {...},
 *
 *     // Changed fields
 *     fieldsFinal: {...}
 *   }
 *
 * The second parameter to the callback is the index of the node in the metadata
 * (first entry is index 0).
 */
Meta.prototype.each = function (fn)
{
  for (var i = 0, l = this.nodes.length; i < l; i++) {
    fn(this.nodes[i], i);
  }
}

exports.Meta = Meta;
