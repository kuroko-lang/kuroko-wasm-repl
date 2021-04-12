OBJS    = $(patsubst %.c, %.em.o, $(filter-out ../src/rline.c ../src/kuroko.c,$(sort $(wildcard ../src/*.c))))
OBJS_W  = $(patsubst %.em.o, %.emw.o, $(OBJS))
MODS    = $(patsubst ../modules/%.krk, res/%.krk, $(sort $(wildcard ../modules/*.krk)))
HEADERS = $(wildcard ../src/*.h ../src/kuroko/*.h)

CC = emcc
CFLAGS = -O3 -I../src/
# Turn this on if you think you really need it.
EMCFLAGS  = -s ALLOW_MEMORY_GROWTH=1
EMCFLAGS += -s WASM=1
EMCFLAGS += --use-preload-plugins

EMCFLAGS_MAIN  = -s NO_EXIT_RUNTIME=1
EMCFLAGS_MAIN += -s EXPORTED_FUNCTIONS='["_krk_call","_main"]'
EMCFLAGS_MAIN += -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]'

EMCFLAGS_WORKER  = -s BUILD_AS_WORKER=1
EMCFLAGS_WORKER += -s EXPORTED_FUNCTIONS='["_krk_run_worker"]'
EMCFLAGS_WORKER += -s ASYNCIFY
EMCFLAGS_WORKER += --pre-js workerWrapper.js

FINALLINK = -g4 --source-map-base 'http://localhost:8080/' -lidbfs.js

# Threads require special headers to be sent by the server
# that provides the main page and possibly also the JS/WASM,
# so unfortunately this isn't going to work in a lot of cases.
ifeq (1,${ENABLE_THREADS})
    EMCFLAGS += -pthread
    CFLAGS += -DENABLE_THREADING
endif

all: index.js ${MODS} res/init.krk res/baz.krk kuroko.js

%.em.o: %.c ${HEADERS}
	${CC} ${CFLAGS} ${EMCFLAGS} ${EMCFLAGS_MAIN} -c -o $@ $<

index.js: ${OBJS} wasmmain.c
	${CC} ${CFLAGS} ${EMCFLAGS} ${EMCFLAGS_MAIN} ${FINALLINK} -o $@ wasmmain.c ${OBJS}
	chmod -x index.wasm

%.emw.o: %.c ${HEADERS}
	${CC} ${CFLAGS} ${EMCFLAGS} ${EMCFLAGS_WORKER} -c -o $@ $<

kuroko.js: ${OBJS_W} worker.c workerWrapper.js
	${CC} ${CFLAGS} ${EMCFLAGS} ${EMCFLAGS_WORKER} ${FINALLINK} -o $@ worker.c ${OBJS_W}
	chmod -x kuroko.wasm

res/%.krk: ../modules/%.krk
	cp $< $@

res/init.krk:
	touch $@

res/baz.krk: ../modules/foo/bar/baz.krk
	cp $< $@

.PHONY: clean
clean:
	@rm -f ../src/*.em.o index.wasm index.js
	@rm -f ../src/*.emw.o kuroko.wasm kuroko.js

.PHONY: deploy
deploy:
	cp index.js index.wasm index.wasm.map ../../kuroko-lang.github.io/
	cp kuroko.js kuroko.wasm kuroko.wasm.map ../../kuroko-lang.github.io/
