/**
 * Kuroko WASM worker, runs like a normal interpreter?
 */
#include <stdint.h>
#include <string.h>
#include <emscripten.h>
#include <unistd.h>

#include <setjmp.h>

#include "kuroko.h"
#include "vm.h"
#include "debug.h"

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

EM_JS(int, check_status, (), {
	return Module.awakeStatus;
});

EM_JS(void, reset_status, (), {
	Module.awakeStatus = 0;
});

EM_JS(void, report_debugger, (const char *str), {
	_craftMessage("d" + UTF8ToString(str));
});

int worker_debugger_callback(KrkCallFrame * frame) {
	reset_status();

	char tmp[4096];
	sprintf(tmp,"{"
		"\"offset\": %ld,"
		"\"function\": \"%s\","
		"\"file\": \"%s\","
		"\"line\": %lu,"
		"\"opcode\": %u"
		"}",
		(unsigned long)(frame->ip - frame->closure->function->chunk.code),
		frame->closure->function->name->chars,
		frame->closure->function->chunk.filename->chars,
		(unsigned long)krk_lineNumber(&frame->closure->function->chunk,
			(unsigned long)(frame->ip - frame->closure->function->chunk.code)),
		(unsigned int)(*frame->ip));

	report_debugger(tmp);
	int result = 0;
	do {
		result = check_status();
		if (result != 0) break;
		emscripten_sleep(20);
	} while (1);

	switch (result) {
		case 1:
			return KRK_DEBUGGER_CONTINUE;
		case 2:
			return KRK_DEBUGGER_RAISE;
		case 3:
			return KRK_DEBUGGER_STEP;
		case 4:
			return KRK_DEBUGGER_QUIT;
		default:
			return KRK_DEBUGGER_CONTINUE;
	}
}

EM_JS(void, resume_status, (), {
	Module.awakeStatus = 1;
});

/**
 * This is built with NO_EXIT_RUNTIME, so when `main` returns none of the
 * normal exit routines are run and the VM stays "active" in the background.
 */
void krk_run_worker(char * data, int size) {
	int flags = 0;

	/* Retrieve cwd from caller */
	chdir(data);
	data += strlen(data) + 1;

	/* Extract flags */
	while (*data) {
		switch (*data) {
			case 's':
				flags |= KRK_THREAD_SINGLE_STEP;
				break;
		}
		data++;
	}

	data++;

	/* Set up VM with no flags */
	vm.binpath = "/usr/local/bin/kuroko";
	krk_initVM(flags);

	/* Initialize the built-in C modules */
	BUNDLED(math);

	/* Set up the interpreter session */
	krk_startModule("__main__");
	krk_attachNamedValue(&krk_currentThread.module->fields,"__doc__", NONE_VAL());

	emscripten_worker_respond_provisionally(X("iWorker is started."));

	krk_debug_registerCallback(worker_debugger_callback);

	KrkValue result = krk_runfile(data,data);

	if (IS_STRING(result)) {
		char * tmp = malloc(AS_STRING(result)->length + 2);
		tmp[0] = 'x';
		tmp[1] = 'S';
		memcpy(tmp+2,AS_CSTRING(result),AS_STRING(result)->length);
		emscripten_worker_respond_provisionally(tmp, AS_STRING(result)->length+2);
		free(tmp);
	} else if (IS_INTEGER(result)) {
		char tmp[100];
		sprintf(tmp, "xI%d", (int)AS_INTEGER(result));
		emscripten_worker_respond_provisionally(tmp,strlen(tmp)+1);
	} else {
		emscripten_worker_respond_provisionally("xN",2);
	}

	krk_freeVM();

	EM_ASM(
		if (_idbfsSuccess) {
			FS.syncfs(function (err) {
				console.log(err);
				_craftMessage('F',true);
			});
		}
	);
}
