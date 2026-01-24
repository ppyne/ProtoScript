function FenwickTree(size) {
    this.size = size;
    this.tree = [];
    var i = 0;
    while (i <= size) {
        this.tree[i] = 0;
        i++;
    }
}

FenwickTree.prototype.update = function (index, delta) {
    var i = index + 1;
    while (i <= this.size) {
        this.tree[i] = this.tree[i] + delta;
        i += i & -i;
    }
};

FenwickTree.prototype.query = function (index) {
    var sum = 0;
    var i = index + 1;
    while (i > 0) {
        sum += this.tree[i];
        i -= i & -i;
    }
    return sum;
};

FenwickTree.prototype.rangeQuery = function (left, right) {
    if (right < left) return 0;
    var res = this.query(right);
    if (left > 0) {
        res -= this.query(left - 1);
    }
    return res;
};
