function StackNode(value) {
    this.value = value;
    this.next = null;
}

function Stack() {
    this.top = null;
    this.length = 0;
}

Stack.prototype.push = function (value) {
    var node = new StackNode(value);
    node.next = this.top;
    this.top = node;
    this.length++;
    return node;
};

Stack.prototype.pop = function () {
    if (!this.top) return null;
    var node = this.top;
    this.top = node.next;
    this.length--;
    return node.value;
};

Stack.prototype.peek = function () {
    return this.top ? this.top.value : null;
};

Stack.prototype.isEmpty = function () {
    return this.length === 0;
};

Stack.prototype.toArray = function () {
    var out = [];
    var cur = this.top;
    while (cur) {
        out.push(cur.value);
        cur = cur.next;
    }
    return out;
};
