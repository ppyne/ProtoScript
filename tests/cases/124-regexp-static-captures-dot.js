var re = /a(b)(c)/;
re.exec('abc');
Io.print((RegExp.$1 )+ "\n");
Io.print((RegExp.$2 )+ "\n");
