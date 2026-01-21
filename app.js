const sample = `var s = "hello world";
Io.print("len = " + s.length + "\\n");
Io.print("indexOf('o') = " + s.indexOf("o") + "\\n");
Io.print("substring(0, 5) = " + s.substring(0, 5) + "\\n");
Io.print("slice(-5) = " + s.slice(-5) + "\\n");
Io.print("replace('world','proto') = " + s.replace("world", "proto") + "\\n");
`;

const outputEl = document.getElementById("output");
const sourceEl = document.getElementById("source");
const runBtn = document.getElementById("run");
const statusEl = document.getElementById("status");

let editor = null;

if (window.CodeMirror) {
  editor = CodeMirror.fromTextArea(sourceEl, {
    mode: "javascript",
    lineNumbers: true,
    lineWrapping: true,
  });
  editor.setValue(sample);
} else {
  sourceEl.value = sample;
}

runBtn.disabled = true;

const appendOut = (text) => {
  outputEl.textContent += text + "\n";
};

const clearOut = () => {
  outputEl.textContent = "";
};

let runtime = null;

const moduleConfig = {
  print: (text) => appendOut(text),
  printErr: (text) => appendOut(`[error] ${text}`),
};

window.startProtoScript = () => {
  if (typeof ProtoScript !== "function") {
    appendOut("[error] protoscript.js did not load");
    return;
  }
  ProtoScript(moduleConfig).then((instance) => {
    runtime = instance;
    statusEl.textContent = "Runtime ready";
    runBtn.disabled = false;
  });
};

const runProgram = () => {
  if (!runtime || !runtime.FS || !runtime.callMain) {
    appendOut("[error] runtime not ready");
    return;
  }
  clearOut();
  statusEl.textContent = "Running…";
  runBtn.disabled = true;
  try {
    const code = editor ? editor.getValue() : sourceEl.value;
    runtime.FS.writeFile("/program.js", code);
    runtime.callMain(["/program.js"]);
  } catch (err) {
    appendOut(`[exception] ${err}`);
  } finally {
    runBtn.disabled = false;
    statusEl.textContent = "Runtime ready";
  }
};

runBtn.addEventListener("click", runProgram);
