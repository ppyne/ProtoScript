function QueueNode(value) {
    this.value = value;
    this.next = null;
}

function Queue() {
    this.head = null;
    this.tail = null;
    this.length = 0;
}

Queue.prototype.enqueue = function (value) {
    var node = new QueueNode(value);
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

Queue.prototype.dequeue = function () {
    if (!this.head) return null;
    var node = this.head;
    this.head = node.next;
    if (!this.head) {
        this.tail = null;
    }
    this.length--;
    return node.value;
};

Queue.prototype.peek = function () {
    return this.head ? this.head.value : null;
};

Queue.prototype.isEmpty = function () {
    return this.length === 0;
};

Queue.prototype.toArray = function () {
    var out = [];
    var cur = this.head;
    while (cur) {
        out.push(cur.value);
        cur = cur.next;
    }
    return out;
};
