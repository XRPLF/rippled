Function.prototype.method = function(name,func) {
  this.prototype[name] = func;

  return this;
};

var filterErr = function(code, done) {
  return function(e) {
      done(e.code !== code ? e : undefined);
    };
};

var throwErr = function(done) {
  return function(e) {
      if (e)
	throw e;
      
      done();
    };
};
 
// apply function to elements of array. Return first true value to done or undefined.
var mapOr = function(func, array, done) {
  if (array.length) {
    func(array[array.length-1], function(v) {
	if (v) {
	  done(v);
	}
	else {
	  array.length -= 1;
	  mapOr(func, array, done);
	}
      });
  }
  else {
    done();
  }
};

var trace = function(comment, func) {
  return function() {
      console.log("%s: %s", trace, arguments.toString);
      func(arguments);
    };
};

var arraySet = function (count, value) {
  var a = new Array(count);
  var i;

  for (i = 0; i != count; i += 1)
    a[i] = value;

  return a;
};

var hexToString = function (h) {
  var	a = [];
  var	i = 0;

  if (h.length % 2) {
    a.push(String.fromCharCode(parseInt(h.substring(0, 1), 16)));
    i = 1;
  }

  for (; i != h.length; i += 2) {
    a.push(String.fromCharCode(parseInt(h.substring(i, i+2), 16)));
  }
  
  return a.join("");
};

var stringToHex = function (s) {
  return Array.prototype.map.call(s, function (c) {
      var b = c.charCodeAt(0);

      return b < 16 ? "0" + b.toString(16) : b.toString(16);
    }).join("");
};

var stringToArray = function (s) {
  var a = new Array(s.length);
  var i;

  for (i = 0; i != a.length; i += 1)
    a[i] = s.charCodeAt(i);

  return a;
};

exports.mapOr	    = mapOr;
exports.trace	    = trace;
exports.arraySet    = arraySet;
exports.hexToString = hexToString;
exports.stringToArray = stringToArray;
exports.stringToHex = stringToHex;

// vim:sw=2:sts=2:ts=8
