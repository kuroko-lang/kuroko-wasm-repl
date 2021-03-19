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
#include "util.h"

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

EM_JS(void, report_input, (const char *str), {
	reset_status();
	waitingForInput = 1;
	_craftMessage("i" + UTF8ToString(str));
});

EM_JS(char *, get_stdin_line, (void), {
	var bytes = lengthBytesUTF8(Module.stdin_line)+1;
	var heapObj = _malloc(bytes);
	stringToUTF8(Module.stdin_line, heapObj, bytes);
	return heapObj;
});

static char * get_line(void) {
	int result = 0;
	do {
		result = check_status();
		if (result != 0) break;
		emscripten_sleep(20);
	} while (1);

	return get_stdin_line();
}

static KrkValue input(int argc, KrkValue argv[], int hasKw) {
	if (argc) {
		if (!IS_STRING(argv[0])) return krk_runtimeError(vm.exceptions->typeError, "expected str");
		report_input(AS_CSTRING(argv[0]));
	} else {
		report_input("");
	}

	char * str = get_line();

	return OBJECT_VAL(krk_takeString(str,strlen(str)));
}

/**
 * This is built with NO_EXIT_RUNTIME, so when `main` returns none of the
 * normal exit routines are run and the VM stays "active" in the background.
 */
void krk_run_worker(char * data, int size) {
	int flags = 0;
	int interactive = 0;

	/* Retrieve cwd from caller */
	chdir(data);
	data += strlen(data) + 1;

	/* Extract flags */
	while (*data) {
		switch (*data) {
			case 's':
				flags |= KRK_THREAD_SINGLE_STEP;
				break;
			case 'i':
				interactive = 1;
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
	krk_defineNative(&vm.builtins->fields, "input", input);

	krk_debug_registerCallback(worker_debugger_callback);

	if (!interactive) {
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
	} else {
		KrkValue systemModule;
		if (krk_tableGet(&vm.modules, OBJECT_VAL(krk_copyString("kuroko",6)), &systemModule)) {
			KrkValue version, buildenv, builddate;
			krk_tableGet(&AS_INSTANCE(systemModule)->fields, OBJECT_VAL(krk_copyString("version",7)), &version);
			krk_tableGet(&AS_INSTANCE(systemModule)->fields, OBJECT_VAL(krk_copyString("buildenv",8)), &buildenv);
			krk_tableGet(&AS_INSTANCE(systemModule)->fields, OBJECT_VAL(krk_copyString("builddate",9)), &builddate);

			fprintf(stdout, "Kuroko %s (%s) with %s\n",
				AS_CSTRING(version), AS_CSTRING(builddate), AS_CSTRING(buildenv));
		}
		fprintf(stdout, "Type `help` for guidance, `exit` to quit, `license` for copyright information.\n");

		while (1) {
			report_input(">>> ");
			char * allData = get_line();
			fprintf(stdout, ">>> %s\n", allData);

			if (!strcmp(allData, "exit()") || !strcmp(allData, "quit()") ||
				!strcmp(allData, "exit") || !strcmp(allData, "quit")) {
				break;
			}

			/* Run it... */
			KrkValue result = krk_interpret(allData, "<stdin>");
			if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
				krk_currentThread.flags &= ~(KRK_THREAD_HAS_EXCEPTION);
			}
			if (!IS_NONE(result)) {
				KrkClass * type = krk_getType(result);
				const char * formatStr = " \033[1;90m=> %s\033[0m\n";
				if (type->_reprer) {
					krk_push(result);
					result = krk_callSimple(OBJECT_VAL(type->_reprer), 1, 0);
				} else if (type->_tostr) {
					krk_push(result);
					result = krk_callSimple(OBJECT_VAL(type->_tostr), 1, 0);
				}
				if (!IS_STRING(result)) {
					fprintf(stdout, " \033[1;91m=> Unable to produce representation for value.\033[0m\n");
				} else {
					fprintf(stdout, formatStr, AS_CSTRING(result));
				}
			}
			krk_resetStack();
			free(allData);
		}
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

