OBJS = $(patsubst %.c, %.em.o, $(filter-out ../src/rline.c ../src/kuroko.c,$(sort $(wildcard ../src/*.c))))
MODS = $(patsubst ../modules/%.krk, res/%.krk, $(sort $(wildcard ../modules/*.krk)))
HEADERS = $(wildcard ../src/*.h)
CC = emcc
CFLAGS = -O3 -I../src/ -DDEBUG
EMCFLAGS  = -s ALLOW_MEMORY_GROWTH=1
EMCFLAGS += -s NO_EXIT_RUNTIME=1
EMCFLAGS += -s WASM=1
EMCFLAGS += -s EXPORTED_FUNCTIONS='["_krk_call","_main"]'
EMCFLAGS += -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]'
EMCFLAGS += -s EMULATE_FUNCTION_POINTER_CASTS=1
EMCFLAGS += --use-preload-plugins

all: index.js ${MODS} res/init.krk res/baz.krk

%.em.o: %.c ${HEADERS}
	${CC} ${CFLAGS} ${EMCFLAGS} -c -o $@ $<

index.js: ${OBJS} wasmmain.c
	${CC} ${CFLAGS} ${EMCFLAGS} -o $@ wasmmain.c ${OBJS}

res/%.krk: ../modules/%.krk
	cp $< $@

res/init.krk:
	touch $@

res/baz.krk: ../modules/foo/bar/baz.krk
	cp $< $@

.PHONY: clean
clean:
	@rm -f ../src/*.em.o index.wasm index.js
