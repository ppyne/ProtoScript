function Heap(compare) {
    this.items = [];
    this.compare = compare || function (a, b) {
        return a < b ? -1 : (a > b ? 1 : 0);
    };
}

Heap.prototype.size = function () {
    return this.items.length;
};

Heap.prototype.peek = function () {
    return this.items.length ? this.items[0] : null;
};

Heap.prototype.push = function (value) {
    var items = this.items;
    items.push(value);
    this._siftUp(items.length - 1);
    return value;
};

Heap.prototype.pop = function () {
    var items = this.items;
    if (!items.length) return null;
    var root = items[0];
    var last = items.pop();
    if (items.length) {
        items[0] = last;
        this._siftDown(0);
    }
    return root;
};

Heap.prototype._siftUp = function (idx) {
    var items = this.items;
    var compare = this.compare;
    while (idx > 0) {
        var parent = ((idx - 1) / 2) >> 0;
        if (compare(items[idx], items[parent]) < 0) {
            var tmp = items[idx];
            items[idx] = items[parent];
            items[parent] = tmp;
            idx = parent;
        } else {
            break;
        }
    }
};

Heap.prototype._siftDown = function (idx) {
    var items = this.items;
    var compare = this.compare;
    var len = items.length;
    while (1) {
        var left = idx * 2 + 1;
        var right = idx * 2 + 2;
        var smallest = idx;
        if (left < len && compare(items[left], items[smallest]) < 0) {
            smallest = left;
        }
        if (right < len && compare(items[right], items[smallest]) < 0) {
            smallest = right;
        }
        if (smallest !== idx) {
            var tmp = items[idx];
            items[idx] = items[smallest];
            items[smallest] = tmp;
            idx = smallest;
        } else {
            break;
        }
    }
};
