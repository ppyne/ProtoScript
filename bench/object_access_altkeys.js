var o = { x: 1, y: 2 };
var sum = 0;
for (var i = 0; i < 10000000; i = i + 1) {
  if ((i & 1) === 0) sum = sum + o.x;
  else              sum = sum + o.y;
}
ProtoScript.exit(sum);
