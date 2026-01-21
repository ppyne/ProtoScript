var s = Gc.stats();
var ok = (s.totalBytes >= 0) && (s.liveBytes >= 0) && (s.collections >= 0) && (s.threshold > 0);
Io.print((ok ? "OK" : "FAIL") + "\n");
