#include <stdint.h>
#include "kuroko.h"
#include "vm.h"

#define BUNDLED(name) do { \
	extern KrkValue krk_module_onload_ ## name (); \
	KrkValue moduleOut = krk_module_onload_ ## name (); \
	krk_attachNamedValue(&vm.modules, # name, moduleOut); \
	krk_attachNamedObject(&AS_INSTANCE(moduleOut)->fields, "__name__", (KrkObj*)krk_copyString(#name, sizeof(#name)-1)); \
	krk_attachNamedValue(&AS_INSTANCE(moduleOut)->fields, "__file__", NONE_VAL()); \
} while (0)

int main() {
	krk_initVM(0);
	atexit(krk_freeVM);

	BUNDLED(fileio);
	BUNDLED(dis);
	BUNDLED(os);
	BUNDLED(time);

	krk_startModule("<module>");
	krk_attachNamedValue(&vm.module->fields,"__doc__", NONE_VAL());

	return 0;
}

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
