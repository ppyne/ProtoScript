function LinkedListNode(value) {
    this.value = value;
    this.next = null;
}

function LinkedList() {
    this.head = null;
    this.tail = null;
    this.length = 0;
}

LinkedList.prototype.push = function (value) {
    var node = new LinkedListNode(value);
    if (!this.head) {
        this.head = node;
        this.tail = node;
    } else {
        this.tail.next = node;
        this.tail = node;
    }
    this.length++;
    return node;
};

LinkedList.prototype.prepend = function (value) {
    var node = new LinkedListNode(value);
    if (!this.head) {
        this.head = node;
        this.tail = node;
    } else {
        node.next = this.head;
        this.head = node;
    }
    this.length++;
    return node;
};

LinkedList.prototype.find = function (value) {
    var cur = this.head;
    while (cur) {
        if (cur.value === value) return cur;
        cur = cur.next;
    }
    return null;
};

LinkedList.prototype.remove = function (value) {
    var prev = null;
    var cur = this.head;
    while (cur) {
        if (cur.value === value) {
            if (prev) {
                prev.next = cur.next;
            } else {
                this.head = cur.next;
            }
            if (cur === this.tail) {
                this.tail = prev;
            }
            this.length--;
            return true;
        }
        prev = cur;
        cur = cur.next;
    }
    return false;
};

LinkedList.prototype.toArray = function () {
    var out = [];
    var cur = this.head;
    while (cur) {
        out.push(cur.value);
        cur = cur.next;
    }
    return out;
};
