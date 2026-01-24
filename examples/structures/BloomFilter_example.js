include "BloomFilter.js";

var bf = new BloomFilter(64);
bf.add("apple");
bf.add("banana");

Io.print(bf.has("apple") ? "apple maybe\n" : "apple no\n");
Io.print(bf.has("pear") ? "pear maybe\n" : "pear no\n");
