var EventEmitter = function () {
  this._events = {};
};

EventEmitter.prototype.on = function (e, f) {
  console.log('on', e, f)
  if (e in this._events) {
    if (this._events[e].indexOf(f) < 0) {
      this._events[e].push(f);
    }
  } else {
    this._events[e] = [f];
  }
  return this;
};

EventEmitter.prototype.off = function (e, f) {
  if (f) {
    function eq(x) { return function (y) { return x === y; } }
    this._events[e] = this.listeners(e).filter(eq(f));
  } else {
    delete this._events[e];
  }
};

EventEmitter.prototype.removeListener = function (e, f) {
  this.off(e, f);
};

EventEmitter.prototype.removeAllListeners = function (e) {
  this.off(e);
};

EventEmitter.prototype.emit = function (e) {
  var args = Array.prototype.slice.call(arguments, 1),
      fs = this.listeners(e);
      console.log('emit', e, args)
  
  for (var i = 0; i < fs.length; i++) {
    fs[i].apply(e, args);
  }
};

EventEmitter.prototype.listeners = function (e) {
  return this._events[e] || [];
};

EventEmitter.prototype.once = function (e, f) {
  var that = this;
  this.on(e, function g() {
    f.apply(e, arguments);
    that.off(e, g);
  });
  return this;
};

exports.EventEmitter = EventEmitter;