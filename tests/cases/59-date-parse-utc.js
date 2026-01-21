Io.print(Date.parse("Thu Jan 01 1970 00:00:00 GMT"));
Io.print(Date.parse("nope"));
Io.print(Date.UTC(1970, 0, 1, 0, 0, 0, 0));
Io.print(Date.UTC(1970, 0, 2));
var d = new Date(0);
Io.print(d.getTime());
