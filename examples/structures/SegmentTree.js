function SegmentTree(values, combine, defaultValue) {
    this.n = values.length;
    this.combine = combine || function (a, b) { return a + b; };
    this.defaultValue = defaultValue === undefined ? 0 : defaultValue;
    this.tree = [];
    if (this.n > 0) {
        this._build(1, 0, this.n - 1, values);
    }
}

SegmentTree.prototype._build = function (idx, left, right, values) {
    if (left === right) {
        this.tree[idx] = values[left];
        return;
    }
    var mid = ((left + right) / 2) >> 0;
    this._build(idx * 2, left, mid, values);
    this._build(idx * 2 + 1, mid + 1, right, values);
    this.tree[idx] = this.combine(this.tree[idx * 2], this.tree[idx * 2 + 1]);
};

SegmentTree.prototype.query = function (ql, qr) {
    return this._query(1, 0, this.n - 1, ql, qr);
};

SegmentTree.prototype._query = function (idx, left, right, ql, qr) {
    if (ql > right || qr < left) return this.defaultValue;
    if (ql <= left && right <= qr) return this.tree[idx];
    var mid = ((left + right) / 2) >> 0;
    var l = this._query(idx * 2, left, mid, ql, qr);
    var r = this._query(idx * 2 + 1, mid + 1, right, ql, qr);
    return this.combine(l, r);
};

SegmentTree.prototype.update = function (pos, value) {
    this._update(1, 0, this.n - 1, pos, value);
};

SegmentTree.prototype._update = function (idx, left, right, pos, value) {
    if (left === right) {
        this.tree[idx] = value;
        return;
    }
    var mid = ((left + right) / 2) >> 0;
    if (pos <= mid) {
        this._update(idx * 2, left, mid, pos, value);
    } else {
        this._update(idx * 2 + 1, mid + 1, right, pos, value);
    }
    this.tree[idx] = this.combine(this.tree[idx * 2], this.tree[idx * 2 + 1]);
};
