function _craftMessage(data) {
  var buf = new Uint8Array(lengthBytesUTF8(data)+1);
  var actualNumBytes = stringToUTF8Array(data, buf, 0, buf.length);
  var transferObject = {
    'callbackId': workerCallbackId,
    'finalResponse': false,
    'data': buf
  };
  postMessage(transferObject, [transferObject.data.buffer]);
}
var _idbfsSuccess = false;
var Module = {
  preRun: [function() {
    /* Load IDBFS */
    FS.mount(IDBFS, {}, '/home/web_user');
    FS.mkdir('/scratch');
    FS.mount(IDBFS, {}, '/scratch');
    FS.syncfs(true, function (err) {
      if (err) {
        _craftMessage('E(debug) Error loading filesystem.');
      } else {
        _idbfsSuccess = true;
      }
    });
    /* Create directories for runtime */
    FS.mkdir('/usr');
    FS.mkdir('/usr/local');
    FS.mkdir('/usr/local/lib');
    FS.mkdir('/usr/local/lib/kuroko');
    FS.mkdir('/usr/local/lib/kuroko/syntax');
    FS.mkdir('/usr/local/lib/kuroko/foo');
    FS.mkdir('/usr/local/lib/kuroko/foo/bar');
    /* Load source modules from web server */
    const modules = ["help.krk","collections.krk","json.krk","string.krk","web.krk","dummy.krk","emscripten.krk"];
    for (const i in modules) {
      FS.createPreloadedFile('/usr/local/lib/kuroko', modules[i], "/res/" + modules[i], 1, 0)
    }
    FS.createPreloadedFile('/usr/local/lib/kuroko/syntax', '__init__.krk', '/res/init.krk', 1, 0)
    FS.createPreloadedFile('/usr/local/lib/kuroko/syntax', 'highlighter.krk', '/res/highlighter.krk', 1, 0)
    FS.createPreloadedFile('/usr/local/lib/kuroko/foo', '__init__.krk', '/res/init.krk', 1, 0)
    FS.createPreloadedFile('/usr/local/lib/kuroko/foo/bar', '__init__.krk', '/res/init.krk', 1, 0)
    FS.createPreloadedFile('/usr/local/lib/kuroko/foo/bar', 'baz.krk', '/res/baz.krk', 1, 0)
    /* Go to home directory */
    FS.chdir('/home/web_user');
  }],
  postRun: [function() {
    if (_idbfsSuccess) {
      FS.syncfs(function (err) {
        if (err) {
          console.log(err);
        }
      });
    }
  }],

  print: function(text) {
    if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
    _craftMessage('O' + text);
  },
  printErr: function(text) {
    if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
    _craftMessage('E' + text);
  }
}

