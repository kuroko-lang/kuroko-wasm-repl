#!/bin/bash

cd '..'

CORE="builtins.c chunk.c compiler.c debug.c memory.c object.c scanner.c table.c value.c vm.c"
MODS="src/dis.c  src/fileio.c  src/os.c  src/time.c"
INTERPRETER="wasm/wasmmain.c"
EMCFLAGS="-s ALLOW_MEMORY_GROWTH=1 -s NO_EXIT_RUNTIME=1 --use-preload-plugins -s WASM=1 -s EXPORTED_FUNCTIONS=[\"_krk_call\",\"_main\"] -s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\"] -s EMULATE_FUNCTION_POINTER_CASTS=1"

emcc ${EMCFLAGS} -O2 -I. -o wasm/index.js ${CORE} ${INTERPRETER} ${MODS} -DDEBUG
