function DoublyLinkedListNode(value) {
    this.value = value;
    this.next = null;
    this.prev = null;
}

function DoublyLinkedList() {
    this.head = null;
    this.tail = null;
    this.length = 0;
}

DoublyLinkedList.prototype.push = function (value) {
    var node = new DoublyLinkedListNode(value);
    if (!this.head) {
        this.head = node;
        this.tail = node;
    } else {
        node.prev = this.tail;
        this.tail.next = node;
        this.tail = node;
    }
    this.length++;
    return node;
};

DoublyLinkedList.prototype.prepend = function (value) {
    var node = new DoublyLinkedListNode(value);
    if (!this.head) {
        this.head = node;
        this.tail = node;
    } else {
        node.next = this.head;
        this.head.prev = node;
        this.head = node;
    }
    this.length++;
    return node;
};

DoublyLinkedList.prototype.find = function (value) {
    var cur = this.head;
    while (cur) {
        if (cur.value === value) return cur;
        cur = cur.next;
    }
    return null;
};

DoublyLinkedList.prototype.remove = function (value) {
    var cur = this.head;
    while (cur) {
        if (cur.value === value) {
            if (cur.prev) {
                cur.prev.next = cur.next;
            } else {
                this.head = cur.next;
            }
            if (cur.next) {
                cur.next.prev = cur.prev;
            } else {
                this.tail = cur.prev;
            }
            this.length--;
            return true;
        }
        cur = cur.next;
    }
    return false;
};

DoublyLinkedList.prototype.toArray = function () {
    var out = [];
    var cur = this.head;
    while (cur) {
        out.push(cur.value);
        cur = cur.next;
    }
    return out;
};

DoublyLinkedList.prototype.toArrayReverse = function () {
    var out = [];
    var cur = this.tail;
    while (cur) {
        out.push(cur.value);
        cur = cur.prev;
    }
    return out;
};
