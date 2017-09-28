'use strict';

var now       = Date.now

// To make id smaller we get miliseconds count from more recent date
  , start     = Date.UTC(2011, 8, 21)

// Prefix with number, it reduces chances of collision with variable names
// (helpful if used as property names on objects)
  , prefix    = String(Math.floor(Math.random() * 10))

// Make it more unique
  , postfix   = Math.floor(Math.random() * 36).toString(36)

// Cache used timestamps to prevent duplicate creation
  , generated = {};

module.exports = function () {
	var id = now() - start;
	while (generated.hasOwnProperty(id)) {
		++id;
	}
	generated[id] = true;
	return prefix + id.toString(36) + postfix;
};
