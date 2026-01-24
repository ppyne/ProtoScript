# Structures examples

This folder contains small prototype-based data structures for ProtoScript.
Each entry lists the file to include and the example that uses it.

## LinkedList
A linked list is a linear collection of data elements, in which linear order is not given by their physical placement in memory.
- Include: `LinkedList.js`
- Example: `LinkedList_example.js`
- Usage:
  - `var list = new LinkedList();`
  - `list.push(value)`, `list.prepend(value)`
  - `list.find(value)`, `list.remove(value)`
  - `list.toArray()`

## DoublyLinkedList
A doubly linked list is a linked list where each node points to both its next and previous node.
- Include: `DoublyLinkedList.js`
- Example: `DoublyLinkedList_example.js`
- Usage:
  - `var list = new DoublyLinkedList();`
  - `list.push(value)`, `list.prepend(value)`
  - `list.find(value)`, `list.remove(value)`
  - `list.toArray()`, `list.toArrayReverse()`

## Queue
A queue is a FIFO (first-in, first-out) collection where elements are added at the back and removed from the front.
- Include: `Queue.js`
- Example: `Queue_example.js`
- Usage:
  - `var q = new Queue();`
  - `q.enqueue(value)`, `q.dequeue()`
  - `q.peek()`, `q.isEmpty()`, `q.toArray()`

## Stack
A stack is a LIFO (last-in, first-out) collection where elements are added and removed from the top.
- Include: `Stack.js`
- Example: `Stack_example.js`
- Usage:
  - `var s = new Stack();`
  - `s.push(value)`, `s.pop()`
  - `s.peek()`, `s.isEmpty()`, `s.toArray()`

## HashTable
A hash table maps keys to values using a hash function to index into buckets.
- Include: `HashTable.js`
- Example: `HashTable_example.js`
- Usage:
  - `var table = new HashTable(capacity);`
  - `table.set(key, value)`, `table.get(key)`
  - `table.has(key)`, `table.remove(key)`, `table.keys()`

## Heap
A heap is a tree-based structure that maintains a partial order (min-heap or max-heap).
- Include: `Heap.js`
- Example: `Heap_example.js`
- Usage:
  - `var h = new Heap([compare]);`
  - `h.push(value)`, `h.pop()`, `h.peek()`, `h.size()`

## PriorityQueue
A priority queue serves elements by priority rather than strictly by insertion order.
- Include: `PriorityQueue.js`
- Example: `PriorityQueue_example.js`
- Usage:
  - `var pq = new PriorityQueue([compare]);`
  - `pq.enqueue(value)`, `pq.dequeue()`, `pq.peek()`, `pq.size()`

## Trie
A trie (prefix tree) stores strings by shared prefixes for efficient prefix queries.
- Include: `Trie.js`
- Example: `Trie_example.js`
- Usage:
  - `var t = new Trie();`
  - `t.insert(word)`, `t.contains(word)`, `t.startsWith(prefix)`

## BinarySearchTree
A binary search tree keeps elements ordered so left < node < right for fast search.
- Include: `BinarySearchTree.js`
- Example: `BinarySearchTree_example.js`
- Usage:
  - `var bst = new BinarySearchTree([compare]);`
  - `bst.insert(value)`, `bst.contains(value)`, `bst.remove(value)`
  - `bst.toArray()`

## AVLTree
An AVL tree is a self-balancing binary search tree that maintains height balance.
- Include: `AVLTree.js`
- Example: `AVLTree_example.js`
- Usage:
  - `var avl = new AVLTree([compare]);`
  - `avl.insert(value)`, `avl.contains(value)`, `avl.toArray()`

## RedBlackTree
A red-black tree is a balanced binary search tree using node colors to enforce constraints.
- Include: `RedBlackTree.js`
- Example: `RedBlackTree_example.js`
- Usage:
  - `var rbt = new RedBlackTree([compare]);`
  - `rbt.insert(value)`, `rbt.contains(value)`, `rbt.toArray()`

## SegmentTree
A segment tree supports fast range queries and point updates on arrays.
- Include: `SegmentTree.js`
- Example: `SegmentTree_example.js`
- Usage:
  - `var seg = new SegmentTree(values, [combine], [defaultValue]);`
  - `seg.query(left, right)`, `seg.update(index, value)`

## FenwickTree (Binary Indexed Tree)
A Fenwick tree stores prefix sums to support fast updates and range queries.
- Include: `FenwickTree.js`
- Example: `FenwickTree_example.js`
- Usage:
  - `var ft = new FenwickTree(size);`
  - `ft.update(index, delta)`, `ft.query(index)`
  - `ft.rangeQuery(left, right)`

## Graph
A graph represents vertices connected by edges, which can be directed or undirected.
- Include: `Graph.js`
- Example: `Graph_example.js`
- Usage:
  - `var g = new Graph(directed);`
  - `g.addVertex(v)`, `g.addEdge(from, to)`, `g.removeEdge(from, to)`
  - `g.neighbors(v)`, `g.bfs(start)`, `g.dfs(start)`

## DisjointSet
A disjoint set (union-find) tracks elements partitioned into non-overlapping sets.
- Include: `DisjointSet.js`
- Example: `DisjointSet_example.js`
- Usage:
  - `var ds = new DisjointSet();`
  - `ds.makeSet(x)`, `ds.find(x)`
  - `ds.union(a, b)`, `ds.connected(a, b)`

## BloomFilter
A Bloom filter is a probabilistic set membership structure with false positives.
- Include: `BloomFilter.js`
- Example: `BloomFilter_example.js`
- Usage:
  - `var bf = new BloomFilter(size);`
  - `bf.add(value)`, `bf.has(value)`

## LeastRecentlyUsedCache
An LRU cache evicts the least recently accessed item when capacity is exceeded.
- Include: `LeastRecentlyUsedCache.js`
- Example: `LeastRecentlyUsedCache_example.js`
- Usage:
  - `var cache = new LeastRecentlyUsedCache(capacity);`
  - `cache.set(key, value)`, `cache.get(key)`
  - `cache.has(key)`, `cache.toArray()`
