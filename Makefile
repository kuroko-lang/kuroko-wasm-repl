OBJS = $(patsubst %.c, %.em.o, $(filter-out ../src/rline.c ../src/kuroko.c,$(sort $(wildcard ../src/*.c))))
MODS = $(patsubst ../modules/%.krk, res/%.krk, $(sort $(wildcard ../modules/*.krk)))
HEADERS = $(wildcard ../src/*.h)
CC = emcc
CFLAGS = -O3 -I../src/ -DDEBUG
# Turn this on if you think you really need it.
#EMCFLAGS  = -s ALLOW_MEMORY_GROWTH=1
EMCFLAGS += -s NO_EXIT_RUNTIME=1
EMCFLAGS += -s WASM=1
EMCFLAGS += -s EXPORTED_FUNCTIONS='["_krk_call","_main"]'
EMCFLAGS += -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]'
EMCFLAGS += --use-preload-plugins

# Threads require special headers to be sent by the server
# that provides the main page and possibly also the JS/WASM,
# so unfortunately this isn't going to work in a lot of cases.
ifeq (1,${ENABLE_THREADS})
    EMCFLAGS += -pthread
    CFLAGS += -DENABLE_THREADING
endif

all: index.js ${MODS} res/init.krk res/baz.krk

%.em.o: %.c ${HEADERS}
	${CC} ${CFLAGS} ${EMCFLAGS} -c -o $@ $<

index.js: ${OBJS} wasmmain.c
	${CC} ${CFLAGS} ${EMCFLAGS} -o $@ wasmmain.c ${OBJS}
	chmod -x index.wasm

res/%.krk: ../modules/%.krk
	cp $< $@

res/init.krk:
	touch $@

res/baz.krk: ../modules/foo/bar/baz.krk
	cp $< $@

.PHONY: clean
clean:
	@rm -f ../src/*.em.o index.wasm index.js
