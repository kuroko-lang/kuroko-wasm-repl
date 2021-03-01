/**
 * Kuroko WASM worker, runs like a normal interpreter?
 */
#include <stdint.h>
#include <string.h>
#include <emscripten.h>
#include <unistd.h>

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

#define X(s) s,sizeof(s)-1

/**
 * This is built with NO_EXIT_RUNTIME, so when `main` returns none of the
 * normal exit routines are run and the VM stays "active" in the background.
 */
void krk_run_worker(char * data, int size) {
	/* Set up VM with no flags */
	vm.binpath = "/usr/local/bin/kuroko";
	krk_initVM(0);

	/* Initialize the built-in C modules */
	BUNDLED(math);

	/* Set up the interpreter session */
	krk_startModule("__main__");
	krk_attachNamedValue(&krk_currentThread.module->fields,"__doc__", NONE_VAL());

	chdir(data);
	data += strlen(data) + 1;
	emscripten_worker_respond_provisionally(X("iWorker is started."));

	KrkValue result = krk_runfile(data,data);

	if (IS_STRING(result)) {
		char * tmp = malloc(AS_STRING(result)->length + 2);
		tmp[0] = 'x';
		tmp[1] = 'S';
		memcpy(tmp+2,AS_CSTRING(result),AS_STRING(result)->length);
		emscripten_worker_respond(tmp, AS_STRING(result)->length+2);
		free(tmp);
	} else if (IS_INTEGER(result)) {
		char tmp[100];
		sprintf(tmp, "xI%d", (int)AS_INTEGER(result));
		emscripten_worker_respond(tmp,strlen(tmp)+1);
	} else {
		emscripten_worker_respond("xN",2);
	}

	krk_freeVM();
}

