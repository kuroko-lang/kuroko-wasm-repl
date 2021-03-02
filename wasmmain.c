/**
 * Kuroko WASM REPL C entry point.
 */
#include <stdint.h>
#include <string.h>
#include <emscripten.h>
#include <unistd.h>
#include "kuroko.h"
#include "vm.h"
#include "memory.h"
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

EM_JS(int32_t, krk_getKeyCount, (int i), {
	console.log('Getting key count for object: ' + i);
	if (typeof Module.krkb[i] === 'object' && Module.krkb[i] !== null)
		return Object.keys(Module.krkb[i]).length;
	return 0;
});

EM_JS(const char *, krk_getKey, (int obj, int key), {
	var output = Object.keys(Module.krkb[obj])[key];
	var bytes = lengthBytesUTF8(output)+1;
	var heapObj = _malloc(bytes);
	stringToUTF8(output, heapObj, bytes);
	return heapObj;
});

EM_JS(const char *, krk_jsAsString, (int obj), {
	var output = Module.krkb[obj];
	var bytes = lengthBytesUTF8(output)+1;
	var heapObj = _malloc(bytes);
	stringToUTF8(output, heapObj, bytes);
	return heapObj;
});

EM_JS(int32_t, krk_jsAsInt, (int obj), {
	return Module.krkb[obj];
});

EM_JS(float, krk_jsAsFloat, (int obj), {
	return Module.krkb[obj];
});

EM_JS(const char *, krk_jsErr, (), {
	var output = Module.krkerr.name;
	var bytes = lengthBytesUTF8(output)+1;
	var heapObj = _malloc(bytes);
	stringToUTF8(output, heapObj, bytes);
	return heapObj;
});

EM_JS(int32_t, krk_jsType, (int i), {
	if (Module.krkb[i] == null) return -1;
	if (typeof Module.krkb[i] === 'string') return 1;
	if (typeof Module.krkb[i] === 'boolean') return 2;
	if (typeof Module.krkb[i] === 'number') return 3;
	if (typeof Module.krkb[i] === 'function') return 4;
	if (Array.isArray(Module.krkb[i])) return 5;
	return 0;
});

static KrkInstance * jsModule;
static KrkClass * jsObjectClass;

struct JSObject {
	KrkInstance inst;
	KrkString * str;
};

static void _jsobject_ongcscan(KrkInstance * self) {
	krk_markObject((KrkObj*)((struct JSObject *)self)->str);
}

static KrkValue _jsobject_init(int argc, KrkValue argv[], int hasKw) {
	if (argc != 2) {
		krk_runtimeError(vm.exceptions->argumentError, "Need a string argument of an object to build on");
		return NONE_VAL();
	}

	if (!IS_STRING(argv[1])) {
		krk_runtimeError(vm.exceptions->typeError, "Argument must be string");
		return NONE_VAL();
	}

	KrkInstance * self = AS_INSTANCE(argv[0]);

	char * tmp = malloc(40 + 2 * AS_STRING(argv[1])->length);
	sprintf(tmp, "(typeof %s === 'undefined') ? -1 : Module.krkb.push(%s)-1", AS_CSTRING(argv[1]), AS_CSTRING(argv[1]));
	int returnValue = emscripten_run_script_int(tmp);
	free(tmp);

	if (returnValue == -1) {
		krk_runtimeError(vm.exceptions->nameError, "%s", AS_CSTRING(argv[1]));
		return NONE_VAL();
	}

	int type = krk_jsType(returnValue);

	switch (type) {
		case -1: return NONE_VAL();
		case 1: {
			const char * bytes = krk_jsAsString(returnValue);
			return OBJECT_VAL(krk_takeString((char*)bytes, strlen(bytes)));
		}
		case 2: return BOOLEAN_VAL(krk_jsAsInt(returnValue));
		case 3: return FLOATING_VAL(krk_jsAsFloat(returnValue));
	}


	krk_attachNamedValue(&self->fields, "__jsname__", argv[1]);
	((struct JSObject*)self)->str = AS_STRING(argv[1]);
	krk_attachNamedValue(&self->fields, "__index__", INTEGER_VAL(returnValue));

	return OBJECT_VAL(self);
}

static KrkValue _jsobject_dir(int argc, KrkValue argv[], int hasKw) {
	KrkInstance * self = AS_INSTANCE(argv[0]);
	KrkValue outputList = krk_dirObject(argc,argv,hasKw);
	krk_push(outputList);

	KrkValue index;
	krk_tableGet(&self->fields, OBJECT_VAL(krk_copyString("__index__",9)), &index);

	int returnValue = AS_INTEGER(index);

	int32_t keyCount = krk_getKeyCount(returnValue);
	for (int32_t i = 0; i < keyCount; ++i) {
		const char * val = krk_getKey(returnValue, i);
		krk_writeValueArray(AS_LIST(outputList), OBJECT_VAL(krk_takeString((char*)val, strlen(val))));
	}

	krk_pop(); /* outputList */
	return outputList;
}

static KrkValue _jsobject_getattr(int argc, KrkValue argv[], int hasKw) {
	if (argc != 2) {
		krk_runtimeError(vm.exceptions->argumentError, "Need a string argument of an object to build on");
		return NONE_VAL();
	}

	if (!IS_STRING(argv[1])) {
		krk_runtimeError(vm.exceptions->typeError, "Argument must be string");
		return NONE_VAL();
	}

	KrkInstance * self = AS_INSTANCE(argv[0]);
	char * tmp = malloc(10 + AS_STRING(argv[1])->length + ((struct JSObject*)self)->str->length);
	sprintf(tmp, "('%s' in %s)", AS_CSTRING(argv[1]), ((struct JSObject*)self)->str->chars);
	int valid = emscripten_run_script_int(tmp);
	if (!valid) {
		free(tmp);
		krk_runtimeError(vm.exceptions->attributeError, "%s", AS_CSTRING(argv[1]));
		return NONE_VAL();
	}

	KrkInstance * newField = krk_newInstance(jsObjectClass);
	krk_push(OBJECT_VAL(newField));

	sprintf(tmp, "%s.%s", ((struct JSObject*)self)->str->chars, AS_CSTRING(argv[1]));
	krk_push(OBJECT_VAL(krk_takeString(tmp, strlen(tmp))));

	KrkValue actualResult = _jsobject_init(2, (KrkValue[]){krk_peek(1),krk_peek(0)},0);

	krk_pop();
	krk_pop();

	if (IS_INSTANCE(actualResult)) {
		krk_push(actualResult);
		krk_tableSet(&self->fields, argv[1], krk_peek(0));
		krk_pop();
		return actualResult;
	} else {
		return actualResult;
	}
}

static KrkValue _jsobject_call(int argc, KrkValue argv[], int hasKw) {
	KrkInstance * self = AS_INSTANCE(argv[0]);
	KrkValue index;
	krk_tableGet(&self->fields, OBJECT_VAL(krk_copyString("__index__",9)), &index);
	KrkValue jsname;
	krk_tableGet(&self->fields, OBJECT_VAL(krk_copyString("__jsname__",10)), &jsname);
	int returnValue = AS_INTEGER(index);

	if (argc == 1) {
		/* Just try to call it */
		EM_ASM({Module.krkb[$0]();}, returnValue);
		return NONE_VAL();
	} else if (argc == 2) {
		KrkClass * type = krk_getType(argv[1]);
		krk_push(argv[1]);
		KrkValue repr = krk_callSimple(OBJECT_VAL(type->_reprer), 1, 0);
		krk_push(repr);
		char * tmp = malloc(AS_STRING(repr)->length + AS_STRING(jsname)->length + 100);
		sprintf(tmp, "(function (){try {\n%s(%s)\n} catch(e){Module.krkerr=e; return 1;} return 0;})()",AS_CSTRING(jsname), AS_CSTRING(repr));
		int32_t result = emscripten_run_script_int(tmp);
		free(tmp);
		krk_pop();
		if (result == 1) {
			const char * s = krk_jsErr();
			krk_runtimeError(vm.exceptions->valueError, "JS Error: %s", s);
			free((char*)s);
			return NONE_VAL();
		}
		return NONE_VAL();
	} else {
		krk_runtimeError(vm.exceptions->typeError, "Don't know how to call that.");
		return NONE_VAL();
	}
}

static KrkValue _jsexec(int argc, KrkValue argv[], int hasKw) {
	if (argc != 1 || !IS_STRING(argv[0])) {
		krk_runtimeError(vm.exceptions->typeError, "must be string");
		return NONE_VAL();
	}
	char * tmp = malloc(AS_STRING(argv[0])->length + 100);
	sprintf(tmp, "(function (){try {\n%s\n} catch(e){Module.krkerr=e; return 1;} return 0;})()", AS_CSTRING(argv[0]));
	int32_t result = emscripten_run_script_int(tmp);
	free(tmp);
	if (result == 1) {
		const char * s = krk_jsErr();
		krk_runtimeError(vm.exceptions->valueError, "JS Error: %s", s);
		free((char*)s);
		return NONE_VAL();
	}
	return NONE_VAL();
}

static void _jsworker_callback(char * data, int size, void * arg) {
	/* Is this the final result? */
	if (size > 0 && data[0] == 'x') {
		KrkObj * callback = arg;
		krk_push(OBJECT_VAL(callback));

		/* Figure out what to do with it. */
		if (data[1] == 'S') {
			krk_push(OBJECT_VAL(krk_copyString(data+2,size-2)));
		} else if (data[1] == 'I') {
			int x = atoi(&data[2]);
			krk_push(INTEGER_VAL(x));
		} else if (data[1] == 'N') {
			krk_push(NONE_VAL());
		}
		krk_callValue(OBJECT_VAL(callback), 1, 1);
		krk_runNext();
	} else if (size > 0 && data[0] == 'O') {
		fputs(data+1,stdout);
		fputs("\n",stdout);
	} else if (size > 0 && data[0] == 'E') {
		fputs(data+1,stderr);
		fputs("\n",stderr);
	} else if (size > 0 && data[0] == 'F') {
		/* filesystem sync is completed, pull in changes */
		EM_ASM(
			window.setTimeout(function () {
				FS.syncfs(true,function (err) {
					if (err) { console.log(err); }
				});
			},200);
		);
	} else if (size > 0 && data[0] == 'd') {
		KrkValue emModule = NONE_VAL();
		krk_tableGet(&vm.modules,OBJECT_VAL(S("emscripten")),&emModule);
		if (!IS_INSTANCE(emModule)) return;
		KrkValue emCallback = NONE_VAL();
		krk_tableGet(&AS_INSTANCE(emModule)->fields,OBJECT_VAL(S("debuggerCallback")),&emCallback);
		if (!IS_OBJECT(emModule)) return;
		krk_push(OBJECT_VAL(krk_copyString(&data[1],size-1)));
		krk_callSimple(emCallback, 1, 0);
	} else if (size > 0 && data[0] == 'i') {
		/* Internal debug info, ignore. */
	}
}

static KrkValue _jsrun_worker(int argc, KrkValue argv[], int hasKw) {
	if (argc < 4 || !IS_STRING(argv[0]) || !IS_STRING(argv[1]) || !IS_OBJECT(argv[2]) || !IS_STRING(argv[3])) {
		krk_runtimeError(vm.exceptions->typeError, "expected str, str, callback, str");
		return NONE_VAL();
	}

	char * url = AS_CSTRING(argv[0]);
	char * arg = AS_CSTRING(argv[1]);
	KrkObj * callback = AS_OBJECT(argv[2]);
	char * flags = AS_CSTRING(argv[3]);

	char tmp[1024];
	getcwd(tmp,1024);

	size_t finalSize = strlen(tmp) + 1 + strlen(flags) + 1 + strlen(arg) + 1;
	char * finalArg = malloc(finalSize);
	snprintf(finalArg, finalSize, "%s%c%s%c%s", tmp, '\0', flags, '\0', arg);

	worker_handle myWorker = emscripten_create_worker(url);
	emscripten_call_worker(myWorker, "krk_run_worker", finalArg, finalSize, _jsworker_callback, callback);

	{
		char tmp[1024];
		sprintf(tmp, "__worker_%d_data", myWorker);
		krk_attachNamedValue(&jsModule->fields, tmp, argv[2]);
	}

	return INTEGER_VAL(myWorker);
}

static KrkValue _jsdestroy_worker(int argc, KrkValue argv[], int hasKw) {
	if (argc != 1 || !IS_INTEGER(argv[0])) {
		return krk_runtimeError(vm.exceptions->typeError, "Expected int");
	}

	emscripten_destroy_worker(AS_INTEGER(argv[0]));

	return NONE_VAL();
}

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

	emscripten_run_script("Module.krkb = [];");

	jsModule = krk_newInstance(vm.baseClasses->moduleClass);
	krk_attachNamedObject(&vm.modules, "js", (KrkObj*)jsModule);
	jsObjectClass = krk_newClass(krk_copyString("JSObject",8), vm.baseClasses->objectClass);
	jsObjectClass->allocSize = sizeof(struct JSObject);
	jsObjectClass->_ongcscan = _jsobject_ongcscan;
	krk_attachNamedObject(&jsModule->fields, "JSObject", (KrkObj*)jsObjectClass);
	/* Okay, how do we want to do this... */
	krk_defineNative(&jsObjectClass->methods, ".__init__", _jsobject_init);
	krk_defineNative(&jsObjectClass->methods, ".__getattr__", _jsobject_getattr);
	krk_defineNative(&jsObjectClass->methods, ".__dir__", _jsobject_dir);
	krk_defineNative(&jsObjectClass->methods, ".__call__", _jsobject_call);
	krk_finalizeClass(jsObjectClass);

	krk_defineNative(&jsModule->fields, "exec", _jsexec);
	krk_defineNative(&jsModule->fields, "run_worker", _jsrun_worker);
	krk_defineNative(&jsModule->fields, "destroy_worker", _jsdestroy_worker);

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
		KrkClass * type = krk_getType(result);
		krk_push(result);
		result = krk_callSimple(OBJECT_VAL(type->_reprer), 1, 0);
		krk_resetStack();
		return AS_CSTRING(result);
	}
	return NULL;
}
