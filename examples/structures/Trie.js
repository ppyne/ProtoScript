function TrieNode() {
    this.children = {};
    this.isWord = false;
}

function Trie() {
    this.root = new TrieNode();
}

Trie.prototype.insert = function (word) {
    var node = this.root;
    var i = 0;
    while (i < word.length) {
        var ch = word.charAt(i);
        if (!node.children[ch]) {
            node.children[ch] = new TrieNode();
        }
        node = node.children[ch];
        i++;
    }
    node.isWord = true;
};

Trie.prototype.contains = function (word) {
    var node = this.root;
    var i = 0;
    while (i < word.length) {
        var ch = word.charAt(i);
        if (!node.children[ch]) return false;
        node = node.children[ch];
        i++;
    }
    return !!node.isWord;
};

Trie.prototype.startsWith = function (prefix) {
    var node = this.root;
    var i = 0;
    while (i < prefix.length) {
        var ch = prefix.charAt(i);
        if (!node.children[ch]) return false;
        node = node.children[ch];
        i++;
    }
    return true;
};
