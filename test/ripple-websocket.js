// npm install ws
// WS_ADDRESS=127.0.0.1:6006 node ripple-websocket.js

var WebSocket = require('ws')

console.log(process.env.WS_ADDRESS)
var ws = new WebSocket('ws://'+process.env.WS_ADDRESS)

ws.on('error', function(error){
  console.log(error)
})

ws.on('open', function () {
  ws.send(JSON.stringify({
    "id": 1,
    "command": "server_info"
  }))
})

ws.on('message', function(dataString, flags) {
  var data = JSON.parse(dataString)
  console.log(data)
})
