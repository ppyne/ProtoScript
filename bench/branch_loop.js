var x = 0;
for (var i = 0; i < 10000000; i = i + 1) {
  if ((i & 1) === 0) {
    x = x + 1;
  }
}
ProtoScript.exit(x);
