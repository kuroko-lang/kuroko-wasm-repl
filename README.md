# Kuroko WASM REPL

This is the source for https://klange.dev/kuroko - a WASM-powered in-browser REPL for Kuroko.

This repository contains:

- `src-min/theme-sunsmoke.js`, theme for Ace.js implementing the _sunsmoke_ colorscheme from Bim.
- `src-min/mode-kuroko.js`, highlighter definition for Ace.js for Kuroko, based on the original Python highlighter.
- `wasmmain.c`, the C entry point for the WASM interpreter.
- `build.sh`, script to build the WASM interpreter.
- `index.html`, `base.js`, `style.css`, the REPL implementation.

The REPL provides an Ace.js instance to read a line (or multiple lines), passes them through the WASM interpreter, prints any `stdout` or `stderr` lines to new DOM elements, freezes the Ace editor and then creates a new one.

The enter key in Ace is intercepted to implemented "smart" handling; a single line of text not ending in a colon will be executed; a line ending in a colon, or multiple lines not ending in a blank line will instead insert a line-feed normally.
