var fish = ["Lion", , "Angel"]; // fish[1] is undefined
Io.print("fish-len:" + fish.length + "\n");
var fishCount = 0;
for (i in fish) fishCount++;
Io.print("fish-count:" + fishCount + "\n");

var birds = ["Lion", "Angel",];
Io.print("birds-len:" + birds.length + "\n");
var birdCount = 0;
for (i in birds) birdCount++;
Io.print("birds-count:" + birdCount + "\n");

var holes = [, ,];
Io.print("holes-len:" + holes.length + "\n");
var holeCount = 0;
for (i in holes) holeCount++;
Io.print("holes-count:" + holeCount + "\n");
