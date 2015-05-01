/*
 * This node.js server is for development only!
 * Please do not use in a production environment.
 */
var express = require('express');
var https = require('https');
var http = require('http');
var fs = require('fs');

var app = express();
app.use(function (req, res, next) {
  res.setHeader('Access-Control-Allow-Headers', 'X-Requested-With,content-type');
  res.setHeader('Access-Control-Allow-Methods', 'GET');
  res.setHeader('Access-Control-Allow-Origin', 'http://local.ripple.com');
  res.setHeader('Access-Control-Allow-Credentials', true);
  // Pass to next layer of middleware
  next();
});
app.use(express.static(__dirname + '/../build/bundle/web'));

http.createServer(app).listen(18080);
console.log('This server is for development only! Do not use in production!');
console.log('Server started. Access through http://local.ripple.com/index_debug.html');
