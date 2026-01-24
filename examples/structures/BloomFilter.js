function BloomFilter(size) {
    this.size = size || 128;
    this.bits = [];
    var i = 0;
    while (i < this.size) {
        this.bits[i] = 0;
        i++;
    }
}

BloomFilter.prototype._hashes = function (value) {
    var s = String(value);
    var h1 = 0;
    var h2 = 0;
    var i = 0;
    while (i < s.length) {
        var code = s.charCodeAt(i);
        h1 = (h1 * 31 + code) % this.size;
        h2 = (h2 * 131 + code) % this.size;
        i++;
    }
    return [h1, h2];
};

BloomFilter.prototype.add = function (value) {
    var hs = this._hashes(value);
    this.bits[hs[0]] = 1;
    this.bits[hs[1]] = 1;
};

BloomFilter.prototype.has = function (value) {
    var hs = this._hashes(value);
    return !!(this.bits[hs[0]] && this.bits[hs[1]]);
};
