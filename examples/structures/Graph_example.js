include "Graph.js";

var undirected = new Graph(false);
undirected.addEdge("A", "B");
undirected.addEdge("A", "C");
undirected.addEdge("B", "D");
Io.print("undir bfs=" + undirected.bfs("A").join(",") + "\n");

var directed = new Graph(true);
directed.addEdge("A", "B");
directed.addEdge("B", "C");
directed.addEdge("C", "A");
Io.print("dir dfs=" + directed.dfs("A").join(",") + "\n");
