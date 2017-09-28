'use strict';

var isArray  = Array.isArray
  , map      = Array.prototype.map
  , slice    = Array.prototype.slice
  , apply    = Function.prototype.apply
  , toArray  = require('../../Array/from')
  , indexOf  = require('../../Array/prototype/e-index-of')
  , callable = require('../../Object/valid-callable')

  , resolve;

resolve = function (args) {
	return this.map(function (r, i) {
		return r ? r(args[i]) : args[i];
	}).concat(slice.call(args, this.length));
};

// Implementation details:
//
// Results are saved internally within array matrix:
// cache[0] -> Result of calling function with no arguments
// cache[1] -> Matrix that keeps results for one argument function calls
// cache[1][0] -> Array of different arguments with which
//                function have been called
// cache[1][1] -> Array of results that matches cache[1][0] arguments array
// cache[2] -> Matrix that keeps results for two argument function calls
// cache[2][0] -> Array of different first (of two) arguments with which
//                function have been called
// cache[2][1] -> Matrixes that keeps results for two arguments function calls
//                Each matrix matches first argument found in cache[2][0]
// cache[2][1][x][0] -> Array of different second arguments with which
//                      function have been called.
// cache[2][1][x][1] -> Array of results that matches cache[2][1][x][0]
//                      arguments array
// ...and so on

module.exports = function (length, resolvers) {
	var fn, mfn, resolver, cache, find, save, clear, value;

	fn = callable(this);
	if (isArray(length)) {
		resolvers = length;
		length = fn.length;
	} else if (length == null) {
		length = fn.length;
	} else if (length !== false) {
		length = length >>> 0;
	}
	if (resolvers != null) {
		resolvers = toArray(resolvers);
		resolvers.forEach(function (r) {
			(r == null) || callable(r);
		});
		resolver = resolve.bind(resolvers);
	}

	cache = [];

	find =  function (length, args) {
		var index = 0, rset = cache, i;

		if (length === 0) {
			value = rset[length];
			return rset.hasOwnProperty(length);
		} else if ((rset = rset[length])) {
			while (index < (length - 1)) {
				i = indexOf.call(rset[0], args[index]);
				if (i === -1) {
					return false;
				}
				rset = rset[1][i];
				++index;
			}
			i = indexOf.call(rset[0], args[index]);
			if (i === -1) {
				return false;
			}
			value = rset[1][i];
			return true;
		}
		return false;
	};

	save = function (length, args, value) {
		var index = 0, rset = cache, i;

		if (length === 0) {
			rset[length] = value;
		} else {
			if (!rset[length]) {
				rset[length] = [[], []];
			}
			rset = rset[length];
			while (index < (length - 1)) {
				i = indexOf.call(rset[0], args[index]);
				if (i === -1) {
					i = rset[0].push(args[index]) - 1;
					rset[1].push([[], []]);
				}
				rset = rset[1][i];
				++index;
			}
			i = indexOf.call(rset[0], args[index]);
			if (i === -1) {
				i = rset[0].push(args[index]) - 1;
			}
			rset[1][i] = value;
		}
	};

	clear = function (length, args) {
		var index = 0, rset = cache, i, path = [];

		if (length === 0) {
			delete rset[length];
		} else if ((rset = rset[length])) {
			while (index < (length - 1)) {
				i = indexOf.call(rset[0], args[index]);
				if (i === -1) {
					return;
				}
				path.push(rset, i);
				rset = rset[1][i];
				++index;
			}
			i = indexOf.call(rset[0], args[index]);
			if (i === -1) {
				return;
			}
			rset[0].splice(i, 1);
			rset[1].splice(i, 1);
			while (!rset[0].length && path.length) {
				i = path.pop();
				rset = path.pop();
				rset[0].splice(i, 1);
				rset[1].splice(i, 1);
			}
		}
	};

	mfn = function () {
		var args, alength;
		args = resolver ? resolver(arguments) : arguments;
		alength = (length === false) ? args.length : length;

		if (find(alength, args)) {
			return value;
		} else {
			mfn.args = arguments;
			mfn.preventCache = false;
			value = apply.call(fn, this, args);
			if (!mfn.preventCache) {
				save(alength, args, value);
			}
			delete mfn.args;
			return value;
		}
	};

	mfn.clearCache = function () {
		var args, alength;
		args = resolver ? resolver(arguments) : arguments;
		alength = (length === false) ? args.length : length;

		clear(alength, args);
	};

	mfn.clearAllCache = function () {
		cache = [];
	};

	return mfn;
};
