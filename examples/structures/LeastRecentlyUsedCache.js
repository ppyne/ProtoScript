function LRUNode(key, value) {
    this.key = key;
    this.value = value;
    this.prev = null;
    this.next = null;
}

function LeastRecentlyUsedCache(capacity) {
    this.capacity = capacity || 4;
    this.map = {};
    this.size = 0;
    this.head = null;
    this.tail = null;
}

LeastRecentlyUsedCache.prototype._addFront = function (node) {
    node.prev = null;
    node.next = this.head;
    if (this.head) this.head.prev = node;
    this.head = node;
    if (!this.tail) this.tail = node;
};

LeastRecentlyUsedCache.prototype._removeNode = function (node) {
    if (node.prev) node.prev.next = node.next;
    if (node.next) node.next.prev = node.prev;
    if (node === this.head) this.head = node.next;
    if (node === this.tail) this.tail = node.prev;
    node.prev = null;
    node.next = null;
};

LeastRecentlyUsedCache.prototype._moveToFront = function (node) {
    if (node === this.head) return;
    this._removeNode(node);
    this._addFront(node);
};

LeastRecentlyUsedCache.prototype.get = function (key) {
    var node = this.map[key];
    if (!node) return null;
    this._moveToFront(node);
    return node.value;
};

LeastRecentlyUsedCache.prototype.set = function (key, value) {
    var node = this.map[key];
    if (node) {
        node.value = value;
        this._moveToFront(node);
        return true;
    }
    node = new LRUNode(key, value);
    this.map[key] = node;
    this._addFront(node);
    this.size++;
    if (this.size > this.capacity) {
        var evict = this.tail;
        if (evict) {
            this._removeNode(evict);
            this.map[evict.key] = null;
            this.size--;
        }
    }
    return true;
};

LeastRecentlyUsedCache.prototype.has = function (key) {
    return !!this.map[key];
};

LeastRecentlyUsedCache.prototype.toArray = function () {
    var out = [];
    var cur = this.head;
    while (cur) {
        out.push(cur.key + ":" + cur.value);
        cur = cur.next;
    }
    return out;
};
