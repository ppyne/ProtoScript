function AVLTreeNode(value) {
    this.value = value;
    this.left = null;
    this.right = null;
    this.height = 1;
}

function AVLTree(compare) {
    this.root = null;
    this.compare = compare || function (a, b) {
        return a < b ? -1 : (a > b ? 1 : 0);
    };
}

AVLTree.prototype._height = function (node) {
    return node ? node.height : 0;
};

AVLTree.prototype._updateHeight = function (node) {
    var lh = this._height(node.left);
    var rh = this._height(node.right);
    node.height = (lh > rh ? lh : rh) + 1;
};

AVLTree.prototype._balanceFactor = function (node) {
    return this._height(node.left) - this._height(node.right);
};

AVLTree.prototype._rotateRight = function (y) {
    var x = y.left;
    var t2 = x.right;
    x.right = y;
    y.left = t2;
    this._updateHeight(y);
    this._updateHeight(x);
    return x;
};

AVLTree.prototype._rotateLeft = function (x) {
    var y = x.right;
    var t2 = y.left;
    y.left = x;
    x.right = t2;
    this._updateHeight(x);
    this._updateHeight(y);
    return y;
};

AVLTree.prototype._rebalance = function (node) {
    var balance = this._balanceFactor(node);
    if (balance > 1) {
        if (this._balanceFactor(node.left) < 0) {
            node.left = this._rotateLeft(node.left);
        }
        return this._rotateRight(node);
    }
    if (balance < -1) {
        if (this._balanceFactor(node.right) > 0) {
            node.right = this._rotateRight(node.right);
        }
        return this._rotateLeft(node);
    }
    return node;
};

AVLTree.prototype.insert = function (value) {
    var cmp = this.compare;
    var self = this;

    function insertNode(node) {
        if (!node) return new AVLTreeNode(value);
        var diff = cmp(value, node.value);
        if (diff < 0) {
            node.left = insertNode(node.left);
        } else if (diff > 0) {
            node.right = insertNode(node.right);
        } else {
            return node;
        }
        self._updateHeight(node);
        return self._rebalance(node);
    }

    this.root = insertNode(this.root);
    return this.root;
};

AVLTree.prototype.contains = function (value) {
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

AVLTree.prototype.toArray = function () {
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
