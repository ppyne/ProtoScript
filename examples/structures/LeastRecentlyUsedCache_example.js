ProtoScript.include("LeastRecentlyUsedCache.js");
var cache = new LeastRecentlyUsedCache(2);
cache.set("a", 1);
cache.set("b", 2);
Io.print(cache.toArray().join(",") + "\n");

cache.get("a");
cache.set("c", 3);
Io.print(cache.has("b") ? "has b\n" : "no b\n");
Io.print(cache.toArray().join(",") + "\n");
