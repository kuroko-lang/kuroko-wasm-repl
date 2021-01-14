/**
 * Kuroko WASM REPL C entry point.
 */
#include <stdint.h>
#include "kuroko.h"
#include "vm.h"

/**
 * Copied over from the main interpreter; packages the C modules directly.
 * Emscripten has a dlopen() interface, but these modules should probably
 * always be available instead of having to make requests for them.
 */
#define BUNDLED(name) do { \
	extern KrkValue krk_module_onload_ ## name (); \
	KrkValue moduleOut = krk_module_onload_ ## name (); \
	krk_attachNamedValue(&vm.modules, # name, moduleOut); \
	krk_attachNamedObject(&AS_INSTANCE(moduleOut)->fields, "__name__", (KrkObj*)krk_copyString(#name, sizeof(#name)-1)); \
	krk_attachNamedValue(&AS_INSTANCE(moduleOut)->fields, "__file__", NONE_VAL()); \
} while (0)

/**
 * This is built with NO_EXIT_RUNTIME, so when `main` returns none of the
 * normal exit routines are run and the VM stays "active" in the background.
 */
int main() {
	/* Set up VM with no flags */
	krk_initVM(0);

	/* If/when we do actually call exit, free the VM */
	atexit(krk_freeVM);

	/* Initialize the built-in C modules */
	BUNDLED(fileio);
	BUNDLED(dis);
	BUNDLED(os);
	BUNDLED(time);

	/* Set up the interpreter session */
	krk_startModule("<module>");
	krk_attachNamedValue(&vm.module->fields,"__doc__", NONE_VAL());

	return 0;
}

/**
 * This is exposed to JavaScript and is how we implement the repl.
 * Interprets a snippet of code and returns the last thing popped
 * from the stack, much like in the regular repl. If that thing
 * is not None, call repr on it to get a printable version and
 * return that to the JavaScript caller for processing.
 */
char * krk_call(char * src) {
	KrkValue result = krk_interpret(src, 0, "<stdin>", "<stdin>");
	if (!IS_NONE(result)) {
		KrkClass * type = AS_CLASS(krk_typeOf(1,&result));
		krk_push(result);
		result = krk_callSimple(OBJECT_VAL(type->_reprer), 1, 0);
		krk_resetStack();
		return AS_CSTRING(result);
	}
	return NULL;
}
