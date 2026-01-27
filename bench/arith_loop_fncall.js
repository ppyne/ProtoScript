// Research / measurement only (not part of default perf narrative).
function f(n) {
  var sum = 0;
  for (var i = 0; i < n; i = i + 1) sum = sum + i;
  return sum;
}
var total = 0;
for (var k = 0; k < 2000; k = k + 1) total = total + f(10000);
ProtoScript.exit(total);
