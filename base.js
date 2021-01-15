/** Kuroko WASM REPL Shell */
var krk_call; /* krk_call(string) -> string */
var currentEditor; /* reference to newest Ace instance */
var consoleEnabled = false; /* whether to print to the browser console */
var scrollToBottom;
var blockCounter = 0;

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
  /* Freeze the editor. */
  editor.setReadOnly(true);
  editor.renderer.$cursorLayer.element.style.display = "none";
  editor.renderer.off("afterRender", scrollToBottom);
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
    /* and scroll to the bottom */
    newOutput.scrollIntoView();
  }
  /* stop using this editor but leave it in the document for visual history */
  currentEditor = createEditor();
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
  newOutput.scrollIntoView();
}

var Module = {
  preRun: [], /* We only do interesting things after the VM has initialized. */

  postRun: [function() {
    /* XXX: Does this do recursive directory creation? */
    FS.mkdir('/usr');
    FS.mkdir('/usr/local');
    FS.mkdir('/usr/local/lib');
    FS.mkdir('/usr/local/lib/kuroko');

    /* Load source modules from web server */
    const modules = ["help.krk","collections.krk","json.krk","string.krk"];
    for (const i in modules) {
      FS.createPreloadedFile('/usr/local/lib/kuroko', modules[i], modules[i], 1, 0)
    }

    /* Bind krk_call */
    krk_call = Module.cwrap('krk_call', 'string', ['string']);

    /* Print some startup text. */
    krk_call("import kuroko\nprint('Kuroko',kuroko.version,kuroko.builddate,'(wasm)')\nkuroko.set_clean_output(True)\n");

    /* Start the first repl line editor */
    currentEditor = createEditor();
    const urlParams = new URLSearchParams(window.location.search);
    const codeParam = urlParams.get('c');
    if (codeParam) {
      currentEditor.insert(codeParam + '\n');
    }
    const runImmediately = urlParams.get('r');
    if (runImmediately == 'y') {
      window.setTimeout(function() {enterCallback(currentEditor); }, 100);
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

