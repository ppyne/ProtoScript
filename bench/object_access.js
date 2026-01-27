var o = { x: 0 };
var sum = 0;
for (var i = 0; i < 10000000; i = i + 1) {
  sum = sum + o.x;
}
ProtoScript.exit(sum);
