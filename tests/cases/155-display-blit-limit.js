Display.open(2, 2, "blit");
Display.clear(1, 2, 3);

var src = Buffer.alloc(16);
// p00
src[0] = 10; src[1] = 20; src[2] = 30; src[3] = 40;
// p10
src[4] = 50; src[5] = 60; src[6] = 70; src[7] = 80;
// p01
src[8] = 90; src[9] = 100; src[10] = 110; src[11] = 120;
// p11
src[12] = 130; src[13] = 140; src[14] = 150; src[15] = 160;

Display.blitRGBA(src, 2, 2, 0, 0, 1, 2);

var fb = Display.framebuffer();
var ok = true;

// p00 copied
ok = ok && fb[0] == 10 && fb[1] == 20 && fb[2] == 30 && fb[3] == 40;
// p01 copied
ok = ok && fb[8] == 90 && fb[9] == 100 && fb[10] == 110 && fb[11] == 120;
// p10 untouched (cleared)
ok = ok && fb[4] == 1 && fb[5] == 2 && fb[6] == 3 && fb[7] == 255;
// p11 untouched (cleared)
ok = ok && fb[12] == 1 && fb[13] == 2 && fb[14] == 3 && fb[15] == 255;

Display.close();
Io.print(ok ? "ok\n" : "fail\n");
