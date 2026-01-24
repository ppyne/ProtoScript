var s = ProtoScript.perfStats();
var ok = !!s &&
    s.allocCount >= 0 &&
    s.allocBytes >= 0 &&
    s.objectNew >= 0 &&
    s.stringNew >= 0 &&
    s.functionNew >= 0 &&
    s.envNew >= 0 &&
    s.callCount >= 0 &&
    s.nativeCallCount >= 0 &&
    s.gcCollections >= 0 &&
    s.gcLiveBytes >= 0;

Io.print(ok ? "OK\n" : "bad\n");
