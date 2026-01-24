function PriorityQueue(compare) {
    this.items = [];
    this.compare = compare || function (a, b) {
        return a < b ? -1 : (a > b ? 1 : 0);
    };
}

PriorityQueue.prototype.size = function () {
    return this.items.length;
};

PriorityQueue.prototype.peek = function () {
    return this.items.length ? this.items[0] : null;
};

PriorityQueue.prototype.enqueue = function (value) {
    var items = this.items;
    items.push(value);
    this._siftUp(items.length - 1);
    return value;
};

PriorityQueue.prototype.dequeue = function () {
    var items = this.items;
    if (!items.length) return null;
    var top = items[0];
    var last = items.pop();
    if (items.length) {
        items[0] = last;
        this._siftDown(0);
    }
    return top;
};

PriorityQueue.prototype._siftUp = function (idx) {
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

PriorityQueue.prototype._siftDown = function (idx) {
    var items = this.items;
    var compare = this.compare;
    var len = items.length;
    while (1) {
        var left = idx * 2 + 1;
        var right = idx * 2 + 2;
        var best = idx;
        if (left < len && compare(items[left], items[best]) < 0) {
            best = left;
        }
        if (right < len && compare(items[right], items[best]) < 0) {
            best = right;
        }
        if (best !== idx) {
            var tmp = items[idx];
            items[idx] = items[best];
            items[best] = tmp;
            idx = best;
        } else {
            break;
        }
    }
};
