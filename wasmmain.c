/**
 * Kuroko WASM REPL C entry point.
 */
#include <stdint.h>
#include <string.h>
#include <emscripten.h>
#include <unistd.h>
#include <kuroko/kuroko.h>
#include <kuroko/vm.h>
#include <kuroko/memory.h>
#include <kuroko/util.h>

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
	vm.binpath = "/usr/local/bin/kuroko";
	krk_initVM(0);

	/* If/when we do actually call exit, free the VM */
	atexit(krk_freeVM);

	/* Initialize the built-in C modules */
	BUNDLED(math);
	BUNDLED(random);

	emscripten_run_script("Module.krkb = [];");

	extern void init_jsModule();
	init_jsModule();

	/* Set up the interpreter session */
	krk_startModule("__main__");
	krk_attachNamedValue(&krk_currentThread.module->fields,"__doc__", NONE_VAL());

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
	krk_resetStack();
	KrkValue result = krk_interpret(src, "<stdin>");
	if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
		krk_dumpTraceback();
		krk_currentThread.flags &= ~(KRK_THREAD_HAS_EXCEPTION);
	}
	if (!IS_NONE(result)) {
		krk_attachNamedValue(&vm.builtins->fields, "_", result);
		KrkClass * type = krk_getType(result);
		krk_push(result);
		result = krk_callDirect(type->_reprer, 1);
		krk_resetStack();
		return AS_CSTRING(result);
	}
	return NULL;
}
