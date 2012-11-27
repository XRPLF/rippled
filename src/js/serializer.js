//

var serializer = {};

serializer.addUInt16 = function(value) {
	switch (typeof value) {
	case 'string':
		addUInt16(value.charCodeAt(0));
		break;

	case 'integer':
		for (i = 16/8; i; i -=1) {
			raw.push(value & 255);
			value >>= 8;
		}
		break;
	
	default:
		throw 'UNEXPECTED_TYPE';
	}
};

serializer.addUInt160 = function(value) {
	switch (typeof value) {
	case 'array':
		raw.concat(value);
		break;
	
	case 'integer':
		for (i = 160/8; i; i -=1) {
			raw.push(value & 255);
			value >>= 8;
		}
		break;
	
	default:
		throw 'UNEXPECTED_TYPE';
	}
};

serializer.getSHA512Half = function() {
};

// vim:sw=2:sts=2:ts=8:et
