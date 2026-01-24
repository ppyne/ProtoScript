include "Trie.js";

var t = new Trie();
t.insert("cat");
t.insert("car");
t.insert("dog");

Io.print(t.contains("cat") ? "cat ok\n" : "cat bad\n");
Io.print(t.contains("cap") ? "cap ok\n" : "cap bad\n");
Io.print(t.startsWith("ca") ? "ca ok\n" : "ca bad\n");
Io.print(t.startsWith("do") ? "do ok\n" : "do bad\n");
