/** Kuroko WASM REPL Shell */
var krk_call; /* krk_call(string) -> string */
var currentEditor; /* reference to newest Ace instance */
var consoleEnabled = false; /* whether to print to the browser console */
var scrollToBottom;
var blockCounter = 0;
var codeHistory = [];
var historySpot = 0;

var codeSamples = {
  helloworld: "print('Hello, world!')",
  variables: "let a, b, c = 1, 2.0, 'three'\nprint(a,b,c)",
  classes: "class Foo(object):\n  def __init__(self):\n    self.bar = 'baz'\n  def frob(self):\n    print(self.bar)\n\nlet foo = Foo()\nfoo.frob()",
  comprehensions: "let lst = [x * 5 for x in range(10)]\nlet dct = {str(x): x for x in lst}\nprint(lst)\nprint(dct)"
};

document.getElementById("container").innerText = "";

/**
 * Run the code in the current Ace editor.
 * Freezes the editor by extracting its lines and putting them back in the DOM,
 * destroys the editor, runs the code, and if it returns a value, prints it
 * into a new element in gray, like the command-line repl, then scrolls down
 * and sets up a new Ace instance.
 */
function runCode(editor) {
  var value = editor.getValue();
  if (!codeHistory.length || codeHistory[codeHistory.length-1] != value) {
    codeHistory.push(value);
  }
  historySpot = codeHistory.length;
  /* Freeze the editor. */
  editor.renderer.off("afterRender", scrollToBottom);
  editor.setReadOnly(true);
  editor.renderer.$cursorLayer.element.style.display = "none";
  var frozenEditor = document.createElement("pre");
  frozenEditor.className = "lines";
  var lines = editor.container.getElementsByClassName("ace_line");
  var len = lines.length;
  for (var i = 0; i < len; i = i + 1) {
    /* because detaching this apparently pops it ? */
    var child = lines[0];
    var lineNumber = document.createElement("a");
    lineNumber.href = "#_" + blockCounter + "_" + (i + 1);
    child.id = "_" + blockCounter + "_" + (i + 1);
    child.prepend(lineNumber);
    frozenEditor.appendChild(child);
  }
  blockCounter++;
  editor.container.remove();
  editor.destroy();
  document.getElementById("container").appendChild(frozenEditor);

  result = krk_call(value);

  if (result != "") {
    /* If krk_call gave us a result that wasn't empty, add new repl output node. */
    let newOutput = document.createElement("pre");
    newOutput.className = "repl";
    newOutput.appendChild(document.createTextNode(' => ' + result));
    document.getElementById("container").appendChild(newOutput);
  }
  /* stop using this editor but leave it in the document for visual history */
  currentEditor = createEditor();

}

/**
 * Ace command to perform smart enter-key handling.
 *
 * If the text ends in a colon, or if there are multiple lines and the last line
 * of the editor is not blank or all spaces, a line feed will be inserted at the
 * current cursor position. Otherwise, code will be executed and this editor
 * will be marked readonly and a new one will be created after the interpreter
 * returns.
 */
function enterCallback(editor) {
  var value = editor.getValue();
  if ((value.endsWith(":") || value.endsWith("\\")) || (value.split("\n").length > 1 && value.replace(/.*\n */g,"").length > 0)) {
    editor.insert("\n");
    return;
  }
  runCode(editor);
}

function historyBackIfOneLine(editor) {
  var value = editor.getValue();
  if (value.split("\n").length == 1 && codeHistory.length > 0) {
    editor.setValue(codeHistory[historySpot-1],1);
    historySpot--;
    if (historySpot == 0) historySpot = 1;
  } else {
    var current = editor.getCursorPosition();
    editor.moveCursorTo(current.row - 1, current.column, true);
  }
}

function historyForwardIfOneLine(editor) {
  var value = editor.getValue();
  if (value.split("\n").length == 1 && codeHistory.length > 0) {
    if (historySpot == codeHistory.length) {
      editor.setValue('',1);
    } else {
      editor.setValue(codeHistory[historySpot],1);
      historySpot++;
    }
  } else {
    var current = editor.getCursorPosition();
    editor.moveCursorTo(current.row + 1, current.column, true);
  }
}

/**
 * Builds an Ace editor and configures it for Kuroko.
 */
function createEditor() {
  let newDiv = document.createElement("div");
  newDiv.className = "editor";
  document.getElementById("container").appendChild(newDiv);
  const editor = ace.edit(newDiv, {
    minLines: 1,
    maxLines: 1000,
    highlightActiveLine: false,
    showPrintMargin: false,
    useSoftTabs: true,
    indentedSoftWrap: false,
    wrap: true
  });
  editor.setTheme("ace/theme/sunsmoke");
  editor.setBehavioursEnabled(false);
  editor.session.setMode("ace/mode/kuroko");
  editor.commands.bindKey("Return", enterCallback);
  editor.commands.bindKey("Up", historyBackIfOneLine);
  editor.commands.bindKey("Down", historyForwardIfOneLine);
  editor.focus();
  scrollToBottom = editor.renderer.on('afterRender', function() {
    newDiv.scrollIntoView();
  });
  return editor;
}

function addText(mode, text) {
  let newOutput = document.createElement("pre");
  newOutput.className = mode;
  newOutput.appendChild(document.createTextNode(text));
  if (!text.length) newOutput.appendChild(document.createElement("wbr"));
  document.getElementById("container").appendChild(newOutput);
}

function insertCode(code) {
  currentEditor.setValue(code,1);
  window.setTimeout(function() { runCode(currentEditor); }, 100);
  return false;
}

function insertNext() {
  insertCode('next()');
  return false;
}

function insertThis(e) {
  insertCode(e.innerText);
  return false;
}

var Module = {
  preRun: [function() {
    FS.mkdir('/usr');
    FS.mkdir('/usr/local');
    FS.mkdir('/usr/local/lib');
    FS.mkdir('/usr/local/lib/kuroko');
    FS.mkdir('/usr/local/lib/kuroko/syntax');
    FS.mkdir('/usr/local/lib/kuroko/foo');
    FS.mkdir('/usr/local/lib/kuroko/foo/bar');

    /* Load source modules from web server */
    const modules = ["help.krk","collections.krk","json.krk","string.krk","web.krk","dummy.krk"];
    for (const i in modules) {
      FS.createPreloadedFile('/usr/local/lib/kuroko', modules[i], "res/" + modules[i], 1, 0)
    }
    FS.createPreloadedFile('/usr/local/lib/kuroko/syntax', '__init__.krk', 'res/init.krk', 1, 0)
    FS.createPreloadedFile('/usr/local/lib/kuroko/syntax', 'highlighter.krk', 'res/highlighter.krk', 1, 0)
    FS.createPreloadedFile('/usr/local/lib/kuroko/foo', '__init__.krk', 'res/init.krk', 1, 0)
    FS.createPreloadedFile('/usr/local/lib/kuroko/foo/bar', '__init__.krk', 'res/init.krk', 1, 0)
    FS.createPreloadedFile('/usr/local/lib/kuroko/foo/bar', 'baz.krk', 'res/baz.krk', 1, 0)
  }],
  postRun: [function() {
    /* Bind krk_call */
    krk_call = Module.cwrap('krk_call', 'string', ['string']);

    /* Print some startup text. */
    krk_call("import kuroko\nprint('Kuroko',kuroko.version,kuroko.builddate,'(wasm)')\nkuroko.set_clean_output(True)\n");
    krk_call("print('Type `tutorial()` for an interactive guide, `license` for copyright information.')\n");
    krk_call("def tutorial(n=0):\n from web import tutorial as actual\n actual(n)\n");

    /* Start the first repl line editor */
    currentEditor = createEditor();
    const urlParams = new URLSearchParams(window.location.search);
    const codeParam = urlParams.get('c');
    if (codeParam) {
      currentEditor.insert(codeParam);
    }
    const runImmediately = urlParams.get('r');
    if (runImmediately == 'y') {
      window.setTimeout(function() {runCode(currentEditor); }, 100);
    }
  }],

  print: function(text) {
    if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
    if (consoleEnabled) console.log(text);
    addText('printed', text);
  },
  printErr: function(text) {
    if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
    if (consoleEnabled) console.error(text);
    addText('error', text);
  }
}

