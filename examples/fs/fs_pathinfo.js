var infos = Fs.pathInfo("/a/b.txt");
for (info in infos) Io.print(info + ': ' + infos[info] + Io.EOL);
