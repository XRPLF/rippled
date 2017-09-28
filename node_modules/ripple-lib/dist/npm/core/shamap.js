'use strict';

var util = require('util');
var hashprefixes = require('./hashprefixes');

var UInt256 = require('./uint256').UInt256;
var SerializedObject = require('./serializedobject').SerializedObject;

/**
 * Abstract class representing a node in a SHAMap tree.
 *
 * Can be either SHAMapTreeNodeInner or SHAMapTreeNodeLeaf.
 *
 * @class
 */
function SHAMapTreeNode() {}

SHAMapTreeNode.TYPE_INNER = 1;
SHAMapTreeNode.TYPE_TRANSACTION_NM = 2;
SHAMapTreeNode.TYPE_TRANSACTION_MD = 3;
SHAMapTreeNode.TYPE_ACCOUNT_STATE = 4;

/**
* @param {String} tag (64 hexadecimal characters)
* @param {SHAMapTreeNode} node
* @return {void}
* @virtual
*/
/*eslint-disable no-unused-vars*/
SHAMapTreeNode.prototype.add_item = function (tag, node) {
  throw new Error('Called unimplemented virtual method SHAMapTreeNode#add_item.');
};
/*eslint-enable no-unused-vars*/

SHAMapTreeNode.prototype.hash = function () {
  throw new Error('Called unimplemented virtual method SHAMapTreeNode#hash.');
};

/**
 * Inner (non-leaf) node in a SHAMap tree.
 * @param {Number} depth (i.e. how many parent inner nodes)
 * @class
 */
function SHAMapTreeNodeInner(depth) {
  SHAMapTreeNode.call(this);

  this.leaves = {};

  this.type = SHAMapTreeNode.INNER;
  this.depth = depth === undefined ? 0 : depth;

  this.empty = true;
}

util.inherits(SHAMapTreeNodeInner, SHAMapTreeNode);

/**
 * @param {String} tag (equates to a ledger entry `index`)
 * @param {SHAMapTreeNode} node (to add)
 * @return {void}
 */
SHAMapTreeNodeInner.prototype.add_item = function (tag, node) {
  var depth = this.depth;
  var existing_node = this.get_node(tag[depth]);

  if (existing_node) {
    // A node already exists in this slot
    if (existing_node instanceof SHAMapTreeNodeInner) {
      // There is an inner node, so we need to go deeper
      existing_node.add_item(tag, node);
    } else if (existing_node.tag === tag) {
      // Collision
      throw new Error('Tried to add a node to a SHAMap that was already in there.');
    } else {
      // Turn it into an inner node
      var new_inner_node = new SHAMapTreeNodeInner(depth + 1);

      // Parent new and existing node
      new_inner_node.add_item(existing_node.tag, existing_node);
      new_inner_node.add_item(tag, node);

      // And place the newly created inner node in the slot
      this.set_node(tag[depth], new_inner_node);
    }
  } else {
    // Neat, we have a nice open spot for the new node
    this.set_node(tag[depth], node);
  }
};

/**
 * Overwrite the node that is currently in a given slot.
 * @param {String} slot (a character 0-F)
 * @param {SHAMapTreeNode} node (to place)
 * @return {void}
 */
SHAMapTreeNodeInner.prototype.set_node = function (slot, node) {
  this.leaves[slot] = node;
  this.empty = false;
};

SHAMapTreeNodeInner.prototype.get_node = function (slot) {
  return this.leaves[slot];
};

SHAMapTreeNodeInner.prototype.hash = function () {
  if (this.empty) {
    return UInt256.from_hex(UInt256.HEX_ZERO);
  }

  var hash_buffer = new SerializedObject();

  for (var i = 0; i < 16; i++) {
    var leafHash = UInt256.from_hex(UInt256.HEX_ZERO);
    var slot = i.toString(16).toUpperCase();

    if (typeof this.leaves[slot] === 'object') {
      leafHash = this.leaves[slot].hash();
    }

    hash_buffer.append(leafHash.to_bytes());
  }

  var hash = hash_buffer.hash(hashprefixes.HASH_INNER_NODE);

  return UInt256.from_bits(hash);
};

/**
 * Leaf node in a SHAMap tree.
 * @param {String} tag (equates to a ledger entry `index`)
 * @param {SerializedObject} node (bytes of account state, transaction etc)
 * @param {Number} type (one of TYPE_ACCOUNT_STATE, TYPE_TRANSACTION_MD etc)
 * @class
 */
function SHAMapTreeNodeLeaf(tag, node, type) {
  SHAMapTreeNode.call(this);

  if (typeof tag !== 'string') {
    throw new Error('Tag is unexpected type.');
  }

  this.tag = tag;
  this.tag_bytes = UInt256.from_hex(this.tag).to_bytes();
  this.type = type;
  this.node = node;
}

util.inherits(SHAMapTreeNodeLeaf, SHAMapTreeNode);

SHAMapTreeNodeLeaf.prototype.hash = function () {
  var buffer = new SerializedObject();
  switch (this.type) {
    case SHAMapTreeNode.TYPE_ACCOUNT_STATE:
      buffer.append(this.node);
      buffer.append(this.tag_bytes);
      return buffer.hash(hashprefixes.HASH_LEAF_NODE);
    case SHAMapTreeNode.TYPE_TRANSACTION_NM:
      return this.tag_bytes;
    case SHAMapTreeNode.TYPE_TRANSACTION_MD:
      buffer.append(this.node);
      buffer.append(this.tag_bytes);
      return buffer.hash(hashprefixes.HASH_TX_NODE);
    default:
      throw new Error('Tried to hash a SHAMap node of unknown type.');
  }
};

function SHAMap() {
  this.root = new SHAMapTreeNodeInner(0);
}

SHAMap.prototype.add_item = function (tag, node, type) {
  node = new SHAMapTreeNodeLeaf(tag, node, type);
  this.root.add_item(tag, node);
};

SHAMap.prototype.hash = function () {
  return this.root.hash();
};

exports.SHAMap = SHAMap;
exports.SHAMapTreeNode = SHAMapTreeNode;
exports.SHAMapTreeNodeInner = SHAMapTreeNodeInner;
exports.SHAMapTreeNodeLeaf = SHAMapTreeNodeLeaf;