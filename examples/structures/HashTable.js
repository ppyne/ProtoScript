function HashTable(capacity) {
    this.capacity = capacity || 16;
    this.buckets = [];
    this.length = 0;
}

HashTable.prototype._hash = function (key) {
    var s = String(key);
    var h = 2166136261;
    var i = 0;
    while (i < s.length) {
        h ^= s.charCodeAt(i);
        h = (h * 16777619) >>> 0;
        i++;
    }
    return h;
};

HashTable.prototype._index = function (key) {
    return this._hash(key) % this.capacity;
};

HashTable.prototype.set = function (key, value) {
    var idx = this._index(key);
    var bucket = this.buckets[idx];
    if (!bucket) {
        bucket = [];
        this.buckets[idx] = bucket;
    }
    var i = 0;
    while (i < bucket.length) {
        if (bucket[i][0] === key) {
            bucket[i][1] = value;
            return true;
        }
        i++;
    }
    bucket.push([key, value]);
    this.length++;
    return true;
};

HashTable.prototype.get = function (key) {
    var idx = this._index(key);
    var bucket = this.buckets[idx];
    if (!bucket) return null;
    var i = 0;
    while (i < bucket.length) {
        if (bucket[i][0] === key) return bucket[i][1];
        i++;
    }
    return null;
};

HashTable.prototype.has = function (key) {
    var idx = this._index(key);
    var bucket = this.buckets[idx];
    if (!bucket) return false;
    var i = 0;
    while (i < bucket.length) {
        if (bucket[i][0] === key) return true;
        i++;
    }
    return false;
};

HashTable.prototype.remove = function (key) {
    var idx = this._index(key);
    var bucket = this.buckets[idx];
    if (!bucket) return false;
    var i = 0;
    while (i < bucket.length) {
        if (bucket[i][0] === key) {
            bucket.splice(i, 1);
            this.length--;
            return true;
        }
        i++;
    }
    return false;
};

HashTable.prototype.keys = function () {
    var out = [];
    var i = 0;
    while (i < this.buckets.length) {
        var bucket = this.buckets[i];
        if (bucket) {
            var j = 0;
            while (j < bucket.length) {
                out.push(bucket[j][0]);
                j++;
            }
        }
        i++;
    }
    return out;
};
