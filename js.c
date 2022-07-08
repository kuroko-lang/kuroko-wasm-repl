/**
 * @brief JS-KRK interaction
 *
 * This is a partial port of Hiwire from Pyodide.
 *
 * @see https://github.com/pyodide/pyodide/blob/main/src/core/hiwire.c
 */
#include <emscripten.h>
#include <unistd.h>
#include <kuroko/util.h>

static KrkInstance * jsModule;
static KrkClass * JSObject;

struct _JsRefStruct {};
typedef struct _JsRefStruct* JsRef;

struct JSObject {
	KrkInstance inst;
	JsRef js;
	JsRef this;
};

EMSCRIPTEN_KEEPALIVE const JsRef Js_undefined = ((JsRef)(2));
EMSCRIPTEN_KEEPALIVE const JsRef Js_true = ((JsRef)(4));
EMSCRIPTEN_KEEPALIVE const JsRef Js_false = ((JsRef)(6));
EMSCRIPTEN_KEEPALIVE const JsRef Js_null = ((JsRef)(8));
EMSCRIPTEN_KEEPALIVE const JsRef Js_novalue = ((JsRef)(10));

EM_JS(int, js_krk_init, (), {
	let _hiwire = {
		objects: new Map(),
		obj_to_key: new Map(),
		counter: new Uint32Array([1])
	};

	window.Hiwire = {};

	Hiwire.UNDEFINED = HEAPU8[_Js_undefined];
	_hiwire.objects.set(Hiwire.UNDEFINED, [ undefined, -1 ]);
	_hiwire.obj_to_key.set(undefined, Hiwire.UNDEFINED);

	Hiwire.TRUE = HEAPU8[_Js_true];
	_hiwire.objects.set(Hiwire.TRUE, [ true, -1 ]);
	_hiwire.obj_to_key.set(true, Hiwire.TRUE);

	Hiwire.FALSE = HEAPU8[_Js_false];
	_hiwire.objects.set(Hiwire.FALSE, [ false, -1 ]);
	_hiwire.obj_to_key.set(false, Hiwire.FALSE);

	Hiwire.JSNULL = HEAPU8[_Js_null];
	_hiwire.objects.set(Hiwire.JSNULL, [ null, -1 ]);
	_hiwire.obj_to_key.set(null, Hiwire.JSNULL);

	let next_permanent = HEAPU8[_Js_novalue] + 2;

	Hiwire.new_value = function(jsval) {
		let idval = _hiwire.obj_to_key.get(jsval);
		if (idval !== undefined) {
			_hiwire.objects.get(idval)[1]++;
			return idval;
		}
		while (_hiwire.objects.has(_hiwire.counter[0])) {
			_hiwire.counter[0] += 2;
		}
		idval = _hiwire.counter[0];
		_hiwire.objects.set(idval, [ jsval, 1 ]);
		_hiwire.obj_to_key.set(jsval, idval);
		_hiwire.counter[0] += 2;
		return idval;
	};

	Hiwire.intern_object = function(obj) {
		let id = next_permanent;
		next_permanent += 2;
		_hiwire.objects.set(id, [ obj, -1 ]);
		return id;
	};

	Hiwire.num_keys = function() {
		return Array.from(_hiwire.objects.keys()).filter((x) => x % 2).length;
	};

	Hiwire.get_value = function(idval) {
		if (!idval) {
			console.error("idval is unset in get_value");
			throw new Error("idval is unset in get_value");
		}

		if (!_hiwire.objects.has(idval)) {
			console.error(`idval not found ${idval}`);
			throw new Error(`idval not found ${idval}`);
		}

		return _hiwire.objects.get(idval)[0];
	};

	Hiwire.decref = function(idval) {
		if ((idval & 1) === 0) return;
		let pair = _hiwire.objects.get(idval);
		let new_refcnt = --pair[1];
		if (new_refcnt === 0) {
			_hiwire.objects.delete(idval);
			_hiwire.obj_to_key.delete(pair[0]);
		}
	};

	Hiwire.incref = function(idval) {
		if ((idval & 1) === 0) return;
		_hiwire.objects.get(idval)[1]++;
	};

	Hiwire.pop_value = function(idval) {
		let result = Hiwire.get_value(idval);
		Hiwire.decref(idval);
		return result;
	};

	Hiwire.registry = new FinalizationRegistry(_krk_cleanup);

	return 0;
});

EM_JS(JsRef, hiwire_int, (int val), {
	return Hiwire.new_value(val);
});

EM_JS(JsRef, hiwire_object, (), {
	return Hiwire.new_value({});
});

EM_JS(JsRef, hiwire_krk_wrapper, (int id), {
	let krk_func = function() {
		let argsid = Hiwire.new_value(arguments);
		let resid = _krk_call_args(id, argsid);
		Hiwire.decref(argsid);
		if (resid != 0) {
			let output = Hiwire.pop_value(resid);
			return output;
		} else {
			let jsid = _krk_get_currentException();
			let jsobj = Hiwire.pop_value(jsid);
			let error = new Error(jsobj[0]);
			error.__krkval__ = jsobj[1];
			throw error;
		}
	};
	krk_func.__krk__ = true;
	krk_func.__id__ = id;
	Hiwire.registry.register(krk_func, id);
	return Hiwire.new_value(krk_func);
});

EM_JS(int, hiwire_out_krk, (JsRef idval), {
	let jsobj = Hiwire.get_value(idval);
	return jsobj.__id__;
});

EM_JS(int, hiwire_args_count, (JsRef idval), {
	let jsobj = Hiwire.get_value(idval);
	return jsobj.length;
});

EM_JS(JsRef, hiwire_args_get, (JsRef idval, int arg), {
	let jsobj = Hiwire.get_value(idval);
	return Hiwire.new_value(jsobj[arg]);
});

EM_JS(JsRef, hiwire_float, (double val), {
	return Hiwire.new_value(val);
});

EM_JS(JsRef, hiwire_string_utf8, (const char* ptr), {
	return Hiwire.new_value(UTF8ToString(ptr));
});

EM_JS(double, hiwire_out_float, (JsRef idval), {
	let jsobj = Hiwire.get_value(idval);
	return jsobj;
});

EM_JS(void, hiwire_decref, (JsRef idval), {
	Hiwire.decref(idval);
});

EM_JS(JsRef, hiwire_incref, (JsRef idval), {
	if (idval & 1) {
		Hiwire.incref(idval);
	}
	return idval;
});

EM_JS(JsRef, hiwire_document, (), {
	return Hiwire.new_value(document);
});

JsRef hiwire_global(const char * name) {

	char buf[1024];
	snprintf(buf, 1024, "Hiwire.new_value(%s)", name);
	JsRef val = (JsRef)emscripten_run_script_int(buf);

	return val;
}

EM_JS(JsRef, hiwire_to_string, (JsRef idobj), {
	let jsobj = Hiwire.get_value(idobj);
	if (jsobj === undefined) return Hiwire.new_value('None');
	let jsstr = jsobj.toString();
	if (typeof jsstr != 'string') return Hiwire.new_value('None');
	return Hiwire.new_value(jsstr);
});

EM_JS(char *, hiwire_to_str, (JsRef idobj), {
	var output = Hiwire.get_value(idobj);
	if (typeof output !== 'string') output = '<undefined>';
	var bytes = lengthBytesUTF8(output)+1;
	var heapObj = _malloc(bytes);
	stringToUTF8(output, heapObj, bytes);
	return heapObj;
});

EM_JS(JsRef, hiwire_get_error, (), {
	return Hiwire.new_value(Hiwire.exception);
});


EM_JS(JsRef, obj_getitem, (JsRef idobj, JsRef idkey), {
	let jsobj = Hiwire.get_value(idobj);
	let jskey = Hiwire.get_value(idkey);
	let result = jsobj[jskey];
	if (result === undefined && !(jskey in jsobj)) return 0;
	return Hiwire.new_value(result);
});

EM_JS(void, obj_setitem, (JsRef idobj, JsRef idkey, JsRef idval), {
	let jsobj = Hiwire.get_value(idobj);
	let jskey = Hiwire.get_value(idkey);
	let jsval = Hiwire.get_value(idval);
	jsobj[jskey] = jsval;
});

EM_JS(JsRef, obj_getattr, (JsRef idobj, const char *name), {
	let jsobj = Hiwire.get_value(idobj);
	let jskey = UTF8ToString(name);
	let result = jsobj[jskey];
	if (result === undefined && !(jskey in jsobj)) return 0;
	return Hiwire.new_value(result);
});

EM_JS(void, obj_setattr, (JsRef idobj, const char *name, JsRef idval), {
	let jsobj = Hiwire.get_value(idobj);
	let jskey = UTF8ToString(name);
	let jsval = Hiwire.get_value(idval);
	jsobj[jskey] = jsval;
});

EM_JS(void, obj_delattr, (JsRef idobj, const char *name), {
	let jsobj = Hiwire.get_value(idobj);
	let jskey = UTF8ToString(name);
	delete jsobj[jskey];
});

EM_JS(JsRef, obj_call, (JsRef idobj, JsRef idthis, JsRef idargs), {
	let jsfunc = Hiwire.get_value(idobj);
	let jsthis = idthis === 0 ? null : Hiwire.get_value(idthis);
	let jsargs = Hiwire.get_value(idargs);
	try {
		return Hiwire.new_value(jsfunc.apply(jsthis,jsargs));
	} catch (error) {
		console.log(error);
		Hiwire.exception = error;
		return 0;
	}
});

EM_JS(JsRef, obj_dir, (JsRef idobj), {
	let jsobj = Hiwire.get_value(idobj);
	let result = [];
	do {
		result.push(... Object.getOwnPropertyNames(jsobj).filter(
			s => {
				let c = s.charCodeAt(0);
				return c < 48 || c > 57;
			}
		));
	} while (jsobj = Object.getPrototypeOf(jsobj));
	return Hiwire.new_value(result);
});

EM_JS(int, obj_isfunction, (JsRef idobj), {
	return typeof Hiwire.get_value(idobj) === 'function';
});

EM_JS(int, obj_isstring, (JsRef idobj), {
	return typeof Hiwire.get_value(idobj) === 'string';
});

EM_JS(int, obj_isnumber, (JsRef idobj), {
	let jsobj = Hiwire.get_value(idobj);
	if (typeof jsobj === 'number') {
		return Number.isSafeInteger(jsobj) ? 2 : 1;
	}
	return 0;
});

EM_JS(int, obj_iskrk, (JsRef idobj), {
	let jsobj = Hiwire.get_value(idobj);
	if (typeof jsobj === 'function') {
		return jsobj.hasOwnProperty('__krk__') && jsobj.hasOwnProperty('__id__');
	}
	return 0;
});

EM_JS(JsRef, JsArray_New, (), {
	return Hiwire.new_value([]);
});

EM_JS(void, JsArray_Push, (JsRef idarr, JsRef idval), {
	Hiwire.get_value(idarr).push(Hiwire.get_value(idval));
});

EM_JS(JsRef, JsArray_Get, (JsRef idobj, int ind), {
	let jsobj = Hiwire.get_value(idobj);
	let result = jsobj[ind];
	if (result === undefined && !(ind in jsobj)) return 0;
	return Hiwire.new_value(result);
});

/**
 * Everything from here onwards is the Kuroko bindings.
 */
static void _jsobject_ongcsweep(KrkInstance * self) {
	struct JSObject * _self = (void*)self;
	if (_self->js) {
		hiwire_decref(_self->js);
		_self->js = 0;
	}
}

#define CURRENT_CTYPE struct JSObject *
#define CURRENT_NAME  self
#define IS_JSObject(o) (krk_isInstanceOf(o, JSObject))
#define AS_JSObject(o) ((struct JSObject*)AS_OBJECT(o))

#define IS_method(o)     IS_BOUND_METHOD(o)
#define IS_function(o)   (IS_CLOSURE(o)|IS_NATIVE(o))
static KrkValue _objToId;
static KrkValue _objects;
static uint32_t counter = 0;

/* Generally based on js2python */
static KrkValue fromJs(JsRef ref, JsRef this_) {

	/* Simple immutable constants */
	if (ref == Js_undefined || ref == Js_null) return NONE_VAL();
	if (ref == Js_true) return BOOLEAN_VAL(1);
	if (ref == Js_false) return BOOLEAN_VAL(0);

	int r;
	if ((r = obj_isnumber(ref))) {
		/**
		 * JS only has 'numbers' so we should try to be a bit smarter...
		 */
		double val = hiwire_out_float(ref);
		hiwire_decref(ref);
		if (r == 2) { /* Number.isSafeInteger() was true */
			long long asInt = val;
			if (val < 0x800000000000LL && val > -0x800000000000LL) {
				/* For sufficiently small ints, produce an int */
				return INTEGER_VAL(val);
			}
			/* For larger integer numbers, give up and try to string parse them into longs */
			char tmp[100];
			snprintf(tmp, 100, "%lld", asInt);
			return krk_parse_int(tmp, strlen(tmp), 10);
		} else {
			/* Number.isSafeInteger() was false; this is a double. */
			return FLOATING_VAL(val);
		}
	}

	if (obj_isstring(ref)) {
		char * asString = hiwire_to_str(ref);
		krk_push(OBJECT_VAL(krk_takeString(asString,strlen(asString))));
		hiwire_decref(ref);
		return krk_pop();
	}

	if (obj_iskrk(ref)) {
		/* Extract value, and decref */
		KrkValue id = INTEGER_VAL(hiwire_out_krk(ref));
		KrkValue objList;
		if (krk_tableGet(AS_DICT(_objects), id, &objList)) {
			krk_push(AS_LIST(objList)->values[0]);
			hiwire_decref(ref);
			return krk_pop();
		} else {
			hiwire_decref(ref);
			return krk_runtimeError(vm.exceptions->typeError, "invalid object?\n");
		}
	}

	struct JSObject * outVal = (void*)krk_newInstance(JSObject);
	outVal->js = ref;

	if (obj_isfunction(ref) ) {
		outVal->this = this_;
	}

	return OBJECT_VAL(outVal);
}

static JsRef make_proxy(KrkValue val) {
	KrkValue id;
	if (krk_tableGet(AS_DICT(_objToId), val, &id)) {
		/* Already exists, increment */
		KrkValue objList;
		krk_tableGet(AS_DICT(_objects), id, &objList);
		AS_LIST(objList)->values[1] = INTEGER_VAL(AS_INTEGER(AS_LIST(objList)->values[1])+1);
		/* return id */
	} else {
		/* Make a new one */
		KrkValue garbage;
		while (krk_tableGet(AS_DICT(_objects), INTEGER_VAL(counter), &garbage)) {
			counter++;
		}
		id = INTEGER_VAL(counter);

		krk_push(krk_list_of(2,(KrkValue[]){ val, INTEGER_VAL(1)},0));
		krk_tableSet(AS_DICT(_objects), id, krk_peek(0));
		krk_pop();
		krk_tableSet(AS_DICT(_objToId), val, id);
	}

	/* Now we need to wrap the value */
	return hiwire_krk_wrapper(AS_INTEGER(id));
}

static JsRef fromKrk(KrkValue val) {
	if (IS_JSObject(val)) {
		JsRef out = AS_JSObject(val)->js;
		hiwire_incref(out);
		return out;
	} if (IS_STRING(val)) {
		return hiwire_string_utf8(AS_CSTRING(val));
	} else if (IS_BOOLEAN(val)) {
		return AS_BOOLEAN(val) ? Js_true : Js_false;
	} else if (IS_INTEGER(val)) {
		return hiwire_int(AS_INTEGER(val));
	} else if (IS_FLOATING(val)) {
		return hiwire_float(AS_FLOATING(val));
	} else if (IS_NONE(val)) {
		return Js_undefined;
	} else if (IS_list(val)) {
		JsRef array = JsArray_New();
		for (size_t i = 0; i < AS_LIST(val)->count; ++i) {
			JsRef entry = fromKrk(AS_LIST(val)->values[i]);
			if (entry == 0) {
				hiwire_decref(array);
				return 0; /* Inner exception raised */
			}
			JsArray_Push(array, entry);
			hiwire_decref(entry);
		}
		return array;
	} else if (IS_function(val) || IS_method(val)) {
		return make_proxy(val);
	} else {
		krk_runtimeError(vm.exceptions->typeError, "Unable to convert '%s' to JSObject\n", krk_typeName(val));
		return 0;
	}
}

KRK_Method(JSObject,__init__) {
	if (argc == 1) {
		return fromJs(hiwire_object(), 0);
	} else {
		return krk_runtimeError(vm.exceptions->typeError, "TODO");
	}
}

KRK_Method(JSObject,__repr__) {
	struct StringBuilder sb = {0};
	pushStringBuilderStr(&sb, "<JSObject ", 10);

	char asNumber[30];
	snprintf(asNumber, 30, "id=%zu", (uintptr_t)self->js);
	pushStringBuilderStr(&sb, asNumber, strlen(asNumber));

	JsRef s = hiwire_to_string(self->js);
	char * asString = hiwire_to_str(s);
	krk_push(OBJECT_VAL(krk_takeString(asString,strlen(asString))));
	hiwire_decref(s);

	/* repr str */
	KrkValue result = krk_callDirect(vm.baseClasses->strClass->_reprer, 1);
	if (IS_STRING(result)) {
		krk_push(result);
		pushStringBuilder(&sb,' ');
		pushStringBuilderStr(&sb, AS_CSTRING(result), AS_STRING(result)->length);
		krk_pop();
	}

	pushStringBuilder(&sb,'>');
	return finishStringBuilder(&sb);
}

KRK_Method(JSObject,__str__) {
	JsRef s = hiwire_to_string(self->js);
	char * asString = hiwire_to_str(s);
	krk_push(OBJECT_VAL(krk_takeString(asString,strlen(asString))));
	hiwire_decref(s);
	return krk_pop();
}

KRK_Method(JSObject,__getattr__) {
	METHOD_TAKES_EXACTLY(1);
	if (!IS_STRING(argv[1])) return krk_runtimeError(vm.exceptions->typeError, "expected str");

	JsRef val = obj_getattr(self->js, AS_CSTRING(argv[1]));

	if (val == 0) {
		return krk_runtimeError(vm.exceptions->attributeError, "JSObject has no attribute '%s'", AS_CSTRING(argv[1]));
	}

	return fromJs(val,self->js);
}

KRK_Method(JSObject,__setattr__) {
	METHOD_TAKES_EXACTLY(2);
	if (!IS_STRING(argv[1])) return krk_runtimeError(vm.exceptions->typeError, "expected str");

	if (AS_CSTRING(argv[1])[0] == '_' &&  AS_CSTRING(argv[1])[1] == '_') {
		return krk_runtimeError(vm.exceptions->attributeError, "not assignable");
	}

	JsRef val = fromKrk(argv[2]);
	if (!val) return NONE_VAL();
	obj_setattr(self->js, AS_CSTRING(argv[1]), val);
	hiwire_decref(val);
	return argv[2];
}

KRK_Method(JSObject,__call__) {

	/* Collect whatever args we want, this is where things get fun... */
	if (hasKw) {
		return krk_runtimeError(vm.exceptions->typeError, "keyword arguments unsupported in call");
	}

	JsRef args = JsArray_New();

	for (size_t i = 1; i < argc; ++i) {
		if (IS_JSObject(argv[i])) {
			JsArray_Push(args, AS_JSObject(argv[i])->js);
		} else {
			JsRef val = fromKrk(argv[i]);
			if (!val) {
				hiwire_decref(args);
				return NONE_VAL();
			}
			JsArray_Push(args, val);
			hiwire_decref(val);
		}
	}

	JsRef result = obj_call(self->js, self->this, args);
	hiwire_decref(args);

	if (result == 0) {
		JsRef    excp = hiwire_get_error();
		JsRef    maybe_krk = obj_getattr(excp, "__krkval__");
		if (maybe_krk) {
			hiwire_decref(excp);
			krk_currentThread.currentException = fromJs(maybe_krk,0);
			krk_currentThread.flags |= KRK_THREAD_HAS_EXCEPTION;
		} else {
			JsRef name = obj_getattr(excp,"name");
			JsRef msg  = obj_getattr(excp,"message");
			hiwire_decref(excp);
			char * _name, * _msg;

			if (name) {
				_name = hiwire_to_str(name);
				hiwire_decref(name);
			} else {
				_name = strdup("(unnamed)");
			}

			if (msg) {
				_msg = hiwire_to_str(msg);
				hiwire_decref(msg);
			} else {
				_msg = strdup("");
			}

			if (!strcmp(_name, "TypeError")) {
				krk_runtimeError(vm.exceptions->typeError, "%s", _msg);
			} else if (!strcmp(_name, "ReferenceError")) {
				krk_runtimeError(vm.exceptions->nameError, "%s", _msg);
			} else {
				krk_runtimeError(vm.exceptions->valueError, "%s: %s", _name, _msg);
			}

			free(_name);
			free(_msg);
		}
		return NONE_VAL();
	}

	return fromJs(result,0);
}

FUNC_SIG(list,append);
FUNC_SIG(list,sort);

KRK_Method(JSObject,__dir__) {
	METHOD_TAKES_NONE();

	KrkValue myList = krk_dirObject(1,argv,0);
	krk_push(myList);

	JsRef results = obj_dir(self->js);

	int i = 0;
	do {
		JsRef val = JsArray_Get(results,i);
		if (val == 0) break;
		krk_push(fromJs(val,0));
		FUNC_NAME(list,append)(2,(KrkValue[]){myList,krk_peek(0)},0);
		krk_pop();
		i++;
	} while (1);

	hiwire_decref(results);

	FUNC_NAME(list,sort)(1,(KrkValue[]){krk_peek(0)},0);

	return krk_pop();
}

KRK_Method(JSObject,__getitem__) {
	METHOD_TAKES_EXACTLY(1);

	JsRef key = fromKrk(argv[1]);
	if (!key) return NONE_VAL();
	JsRef val = obj_getitem(self->js, key);
	hiwire_decref(key);
	return fromJs(val,0);
}

EMSCRIPTEN_KEEPALIVE int krk_cleanup(int index) {
	KrkValue id = INTEGER_VAL(index);
	KrkValue objList;
	if (krk_tableGet(AS_DICT(_objects), id, &objList)) {
		int count = AS_INTEGER(AS_LIST(objList)->values[1]);
		if (count == 1) {
			KrkValue val   = AS_LIST(objList)->values[0];
			krk_tableDelete(AS_DICT(_objects), id);
			krk_tableDelete(AS_DICT(_objToId), val);
		} else {
			AS_LIST(objList)->values[1] = INTEGER_VAL(count-1);
		}
	}

	return 0;
}

EMSCRIPTEN_KEEPALIVE JsRef krk_call_args(int krkindex, JsRef jsargsindex) {
	int num_args = hiwire_args_count(jsargsindex);
	KrkValue id = INTEGER_VAL(krkindex);
	KrkValue objList;
	if (krk_tableGet(AS_DICT(_objects), id, &objList)) {
		krk_push(AS_LIST(objList)->values[0]);
		for (int i = 0; i < num_args; ++i) {
			krk_push(fromJs(hiwire_args_get(jsargsindex, i), 0));
		}
		KrkValue result = krk_callStack(num_args);
		if (unlikely(krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION)) {
			return 0;
		}
		return fromKrk(result);
	}

	return 0;
}

EMSCRIPTEN_KEEPALIVE JsRef krk_get_currentException(void) {
	/* Unset exception */
	krk_currentThread.flags &= ~(KRK_THREAD_HAS_EXCEPTION);
	/* Repr it */
	KrkClass * type = krk_getType(krk_currentThread.currentException);
	KrkValue result = NONE_VAL();
	if (type->_reprer) {
		krk_push(krk_currentThread.currentException);
		result = krk_callDirect(type->_reprer, 1);
	} else if (type->_tostr) {
		krk_push(krk_currentThread.currentException);
		result = krk_callDirect(type->_tostr, 1);
	}
	JsRef arr  = JsArray_New();
	JsRef desc = hiwire_string_utf8(IS_STRING(result) ? AS_CSTRING(result): "(unrepresentable)");
	JsArray_Push(arr, desc);
	hiwire_decref(desc);
	JsRef excp = make_proxy(krk_currentThread.currentException);
	JsArray_Push(arr, excp);
	hiwire_decref(excp);
	return arr;
}


/**
 * Worker interfaces
 *
 * This allows us to communicate with a worker instance running
 * the worker.c build of the interpreter. We hijack the normal
 * emscripten worker interfaces to do this, and an unmodified
 * Emscripten boilerplate will probably log a bunch of errors,
 * but it should still work.
 */
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
		krk_push(emCallback);
		krk_push(OBJECT_VAL(krk_copyString(&data[1],size-1)));
		krk_callStack(1);
	} else if (size > 0 && data[0] == 'i') {
		/* Input request */
		KrkValue emModule = NONE_VAL();
		krk_tableGet(&vm.modules,OBJECT_VAL(S("emscripten")),&emModule);
		if (!IS_INSTANCE(emModule)) return;
		KrkValue emCallback = NONE_VAL();
		krk_tableGet(&AS_INSTANCE(emModule)->fields,OBJECT_VAL(S("inputCallback")),&emCallback);
		if (!IS_OBJECT(emModule)) return;
		krk_push(emCallback);
		krk_push(OBJECT_VAL(krk_copyString(&data[1],strlen(&data[1]))));
		krk_callStack(1);
	}
}

KRK_Function(run_worker) {
	FUNCTION_TAKES_EXACTLY(4);
	CHECK_ARG(0,str,KrkString*,_url);
	CHECK_ARG(1,str,KrkString*,_arg);
	CHECK_ARG(2,OBJECT,KrkObj*,callback);
	CHECK_ARG(3,str,KrkString*,_flags);

	char * url   = _url->chars;
	char * arg   = _arg->chars;
	char * flags = _flags->chars;

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

KRK_Function(destroy_worker) {
	FUNCTION_TAKES_EXACTLY(1);
	CHECK_ARG(0,int,krk_integer_type,workerId);
	emscripten_destroy_worker(workerId);
	return NONE_VAL();
}

void init_jsModule(void) {

	/* Set up module */
	jsModule = krk_newInstance(vm.baseClasses->moduleClass);
	krk_attachNamedObject(&vm.modules, "js", (KrkObj*)jsModule);
	krk_attachNamedObject(&jsModule->fields, "__name__", (KrkObj*)S("js"));
	krk_attachNamedValue(&jsModule->fields, "__file__", NONE_VAL());

	_objToId = krk_dict_of(0,NULL,0);
	krk_attachNamedValue(&jsModule->fields, "__cache_objToId__", _objToId);
	_objects = krk_dict_of(0,NULL,0);
	krk_attachNamedValue(&jsModule->fields, "__cache_objects__", _objects);

	js_krk_init();

	krk_makeClass(jsModule, &JSObject, "JSObject", vm.baseClasses->objectClass);

	JSObject->allocSize = sizeof(struct JSObject);
	JSObject->_ongcsweep = _jsobject_ongcsweep;

	BIND_METHOD(JSObject,__init__);
	BIND_METHOD(JSObject,__repr__);
	BIND_METHOD(JSObject,__str__);
	BIND_METHOD(JSObject,__getattr__);
	BIND_METHOD(JSObject,__setattr__);
	BIND_METHOD(JSObject,__call__);
	BIND_METHOD(JSObject,__getitem__);
	BIND_METHOD(JSObject,__dir__);

	krk_finalizeClass(JSObject);

#define ATTACH(thing) \
	struct JSObject * _krk_ ## thing = (struct JSObject*)krk_newInstance(JSObject); \
	krk_attachNamedObject(&jsModule->fields, # thing, (KrkObj*)_krk_ ## thing); \
	_krk_ ## thing -> js = Js_ ## thing;

	ATTACH(undefined)
	ATTACH(true)
	ATTACH(false)
	ATTACH(null)

	JsRef Js_document = hiwire_global("document");
	ATTACH(document)

	JsRef Js_window = hiwire_global("window");
	ATTACH(window)

	BIND_FUNC(jsModule,destroy_worker);
	BIND_FUNC(jsModule,run_worker);
}
