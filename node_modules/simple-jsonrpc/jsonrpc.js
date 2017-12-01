/**
 * Simple JSON-RPC
 */

var JSONRPCClient, URL_REGEXP;

// Take url in parts: scheme, host, root zone, port, and path of request
URL_REGEXP = /(http|https):\/\/([\w\-_]+(?:\.([\w\-_]+))+)(?::([\d]+))*([\w\-\.,@?^=%&amp;:/~\+#]*[\w\-\@?^=%&amp;/~\+#])?/;

/**
 * JSON-RPC client
 * @param {String} scheme Scheme of request (http|https)
 * @param {String} host JSON-RPC server
 * @param {Number} port Port
 * @param {String} path Path of request
 */
JSONRPCClient = function (scheme, host, port, path) {
    this.scheme = scheme;
    this.host = host;
    this.port = port;
    this.path = path;

    /**
     * Call method of JSON-RPC API
     * @param {String} method Method
     * @param {Array} params Params of request
     * @param {Function} [onSuccessCallback] Callback for success result
     * @param {Function} [onErrorCallback] Callback for error
     */
    this.call = function(method, params, onSuccessCallback, onErrorCallback) {
        var requestJSON, requestOptions, request;

        requestJSON = JSON.stringify({
            'id': '' + (new Date()).getTime(),
            'method': method,
            'params': params
        });

        requestOptions = {
            host: this.host,
            port: this.port,
            path: this.path,
            method: 'POST',
            headers: {
                'Content-Length': Buffer.byteLength(requestJSON, encoding='utf8')
            }
        }

        // Send request
        request = require(this.scheme).request(requestOptions, onComplete);
	request.on('error', function (error) {
            onErrorCallback && onErrorCallback(error);
        });
        request.write(requestJSON);
        request.end();

        function onComplete (res) {
            var buffer = '';

            res.setEncoding('utf8');

            res.on('data', function(chunk) {
                buffer = buffer + chunk;
            });

            res.on('end', function () {
                var decoded = JSON.parse(buffer);
                if(decoded.result) {
                    onSuccessCallback && onSuccessCallback(decoded.result);
                } else {
                    onErrorCallback && onErrorCallback(decoded.error);
                }
            });

            res.on('error', function (error) {
                onErrorCallback && onErrorCallback(error);
            });
        }
    };
}

module.exports = {

    /**
     * Create JSON-RPC client
     * @param {String} url Server url
     * @return {JSONRPCClient} client
     */
    client: function (url) {
        var urlParts = url.match(URL_REGEXP),
            scheme = urlParts[1] || 'http',
            host = urlParts[2],
            port = urlParts[4] || (scheme === 'https' ? 443 : 80),
            path = urlParts[5];

        return new JSONRPCClient(scheme, host, port, path);
    }

};
