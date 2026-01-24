function DisjointSet() {
    this.parent = {};
    this.rank = {};
}

DisjointSet.prototype.makeSet = function (x) {
    if (this.parent[x]) return;
    this.parent[x] = x;
    this.rank[x] = 0;
};

DisjointSet.prototype.find = function (x) {
    if (!this.parent[x]) {
        this.makeSet(x);
    }
    if (this.parent[x] !== x) {
        this.parent[x] = this.find(this.parent[x]);
    }
    return this.parent[x];
};

DisjointSet.prototype.union = function (a, b) {
    var rootA = this.find(a);
    var rootB = this.find(b);
    if (rootA === rootB) return false;
    var rankA = this.rank[rootA];
    var rankB = this.rank[rootB];
    if (rankA < rankB) {
        this.parent[rootA] = rootB;
    } else if (rankA > rankB) {
        this.parent[rootB] = rootA;
    } else {
        this.parent[rootB] = rootA;
        this.rank[rootA] = rankA + 1;
    }
    return true;
};

DisjointSet.prototype.connected = function (a, b) {
    return this.find(a) === this.find(b);
};
