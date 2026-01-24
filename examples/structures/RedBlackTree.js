function RedBlackTreeNode(value) {
    this.value = value;
    this.left = null;
    this.right = null;
    this.parent = null;
    this.red = true;
}

function RedBlackTree(compare) {
    this.root = null;
    this.compare = compare || function (a, b) {
        return a < b ? -1 : (a > b ? 1 : 0);
    };
}

RedBlackTree.prototype._rotateLeft = function (x) {
    var y = x.right;
    x.right = y.left;
    if (y.left) y.left.parent = x;
    y.parent = x.parent;
    if (!x.parent) {
        this.root = y;
    } else if (x === x.parent.left) {
        x.parent.left = y;
    } else {
        x.parent.right = y;
    }
    y.left = x;
    x.parent = y;
};

RedBlackTree.prototype._rotateRight = function (y) {
    var x = y.left;
    y.left = x.right;
    if (x.right) x.right.parent = y;
    x.parent = y.parent;
    if (!y.parent) {
        this.root = x;
    } else if (y === y.parent.left) {
        y.parent.left = x;
    } else {
        y.parent.right = x;
    }
    x.right = y;
    y.parent = x;
};

RedBlackTree.prototype.insert = function (value) {
    var node = new RedBlackTreeNode(value);
    var cur = this.root;
    var parent = null;
    var cmp = this.compare;
    while (cur) {
        parent = cur;
        var diff = cmp(value, cur.value);
        if (diff < 0) {
            cur = cur.left;
        } else if (diff > 0) {
            cur = cur.right;
        } else {
            return cur;
        }
    }
    node.parent = parent;
    if (!parent) {
        this.root = node;
    } else if (cmp(value, parent.value) < 0) {
        parent.left = node;
    } else {
        parent.right = node;
    }
    this._fixInsert(node);
    return node;
};

RedBlackTree.prototype._fixInsert = function (z) {
    while (z.parent && z.parent.red) {
        var gp = z.parent.parent;
        if (!gp) break;
        if (z.parent === gp.left) {
            var y = gp.right;
            if (y && y.red) {
                z.parent.red = false;
                y.red = false;
                gp.red = true;
                z = gp;
            } else {
                if (z === z.parent.right) {
                    z = z.parent;
                    this._rotateLeft(z);
                }
                z.parent.red = false;
                gp.red = true;
                this._rotateRight(gp);
            }
        } else {
            var y2 = gp.left;
            if (y2 && y2.red) {
                z.parent.red = false;
                y2.red = false;
                gp.red = true;
                z = gp;
            } else {
                if (z === z.parent.left) {
                    z = z.parent;
                    this._rotateRight(z);
                }
                z.parent.red = false;
                gp.red = true;
                this._rotateLeft(gp);
            }
        }
    }
    if (this.root) this.root.red = false;
};

RedBlackTree.prototype.contains = function (value) {
    var cur = this.root;
    while (cur) {
        var diff = this.compare(value, cur.value);
        if (diff < 0) {
            cur = cur.left;
        } else if (diff > 0) {
            cur = cur.right;
        } else {
            return true;
        }
    }
    return false;
};

RedBlackTree.prototype.toArray = function () {
    var out = [];
    function inOrder(node) {
        if (!node) return;
        inOrder(node.left);
        out.push(node.value);
        inOrder(node.right);
    }
    inOrder(this.root);
    return out;
};
