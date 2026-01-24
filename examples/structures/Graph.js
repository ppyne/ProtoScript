function Graph(directed) {
    this.directed = !!directed;
    this.adj = {};
}

Graph.prototype.addVertex = function (v) {
    if (!this.adj[v]) {
        this.adj[v] = [];
    }
};

Graph.prototype.addEdge = function (from, to) {
    this.addVertex(from);
    this.addVertex(to);
    this.adj[from].push(to);
    if (!this.directed) {
        this.adj[to].push(from);
    }
};

Graph.prototype.removeEdge = function (from, to) {
    var list = this.adj[from];
    if (list) {
        var i = 0;
        while (i < list.length) {
            if (list[i] === to) {
                list.splice(i, 1);
                break;
            }
            i++;
        }
    }
    if (!this.directed) {
        var list2 = this.adj[to];
        if (list2) {
            var j = 0;
            while (j < list2.length) {
                if (list2[j] === from) {
                    list2.splice(j, 1);
                    break;
                }
                j++;
            }
        }
    }
};

Graph.prototype.neighbors = function (v) {
    return this.adj[v] ? this.adj[v].slice() : [];
};

Graph.prototype.bfs = function (start) {
    var visited = {};
    var order = [];
    var queue = [];
    if (!this.adj[start]) return order;
    queue.push(start);
    visited[start] = true;
    while (queue.length) {
        var v = queue.shift();
        order.push(v);
        var list = this.adj[v];
        var i = 0;
        while (i < list.length) {
            var n = list[i];
            if (!visited[n]) {
                visited[n] = true;
                queue.push(n);
            }
            i++;
        }
    }
    return order;
};

Graph.prototype.dfs = function (start) {
    var visited = {};
    var order = [];
    var self = this;
    function visit(v) {
        visited[v] = true;
        order.push(v);
        var list = self.adj[v];
        var i = 0;
        while (i < list.length) {
            var n = list[i];
            if (!visited[n]) visit(n);
            i++;
        }
    }
    if (this.adj[start]) visit(start);
    return order;
};
