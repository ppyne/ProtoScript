function BinarySearchTreeNode(value) {
    this.value = value;
    this.left = null;
    this.right = null;
}

function BinarySearchTree(compare) {
    this.root = null;
    this.compare = compare || function (a, b) {
        return a < b ? -1 : (a > b ? 1 : 0);
    };
}

BinarySearchTree.prototype.insert = function (value) {
    var node = new BinarySearchTreeNode(value);
    if (!this.root) {
        this.root = node;
        return node;
    }
    var cur = this.root;
    var cmp = 0;
    while (cur) {
        cmp = this.compare(value, cur.value);
        if (cmp < 0) {
            if (!cur.left) {
                cur.left = node;
                return node;
            }
            cur = cur.left;
        } else if (cmp > 0) {
            if (!cur.right) {
                cur.right = node;
                return node;
            }
            cur = cur.right;
        } else {
            return cur;
        }
    }
    return node;
};

BinarySearchTree.prototype.contains = function (value) {
    var cur = this.root;
    while (cur) {
        var cmp = this.compare(value, cur.value);
        if (cmp < 0) {
            cur = cur.left;
        } else if (cmp > 0) {
            cur = cur.right;
        } else {
            return true;
        }
    }
    return false;
};

BinarySearchTree.prototype._findMin = function (node) {
    var cur = node;
    while (cur && cur.left) {
        cur = cur.left;
    }
    return cur;
};

BinarySearchTree.prototype.remove = function (value) {
    var removed = false;
    var cmp = this.compare;
    var self = this;

    function removeNode(node, target) {
        if (!node) return null;
        var diff = cmp(target, node.value);
        if (diff < 0) {
            node.left = removeNode(node.left, target);
            return node;
        }
        if (diff > 0) {
            node.right = removeNode(node.right, target);
            return node;
        }
        removed = true;
        if (!node.left && !node.right) return null;
        if (!node.left) return node.right;
        if (!node.right) return node.left;
        var min = self._findMin(node.right);
        node.value = min.value;
        node.right = removeNode(node.right, min.value);
        return node;
    }

    this.root = removeNode.call(this, this.root, value);
    return removed;
};

BinarySearchTree.prototype.toArray = function () {
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
