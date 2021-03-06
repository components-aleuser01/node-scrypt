/*
scrypt_params.cc 

Copyright (C) 2013 Barry Steyn (http://doctrina.org/Scrypt-Authentication-For-Node.html)

This source code is provided 'as-is', without any express or implied
warranty. In no event will the author be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this source code must not be misrepresented; you must not
claim that you wrote the original source code. If you use this source code
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original source code.

3. This notice may not be removed or altered from any source distribution.

Barry Steyn barry.steyn@gmail.com
*/

#include <node.h>
#include <nan.h>
#include <v8.h>
#include <string>

//Scrypt is a C library and there needs c linkings
extern "C" {
	#include "pickparams.h"
}

using namespace v8;
#include "common.h"
#include "scrypt_config_object.h"
namespace {

//
// Structure to hold information
//
struct TranslationInfo {
	//Async callback function
	Persistent<Function> callback;

	//Custom data
	int result;
	size_t maxmem;
	double maxmemfrac;
	double maxtime;
	int N;
	uint32_t r;
	uint32_t p;

	//Constructor / Destructor
	TranslationInfo(Handle<Object> config) { 
		maxmem = static_cast<node::encoding>(config->Get(NanNew<String>("maxmem"))->ToUint32()->Value());
		maxmemfrac = static_cast<node::encoding>(config->Get(NanNew<String>("maxmemfrac"))->ToUint32()->Value());
		NanDisposePersistent(callback); 
	}
	~TranslationInfo() { NanDisposePersistent(callback); }
};

//
// Assigns and validates arguments from JavaScript land
//
int 
AssignArguments(_NAN_METHOD_ARGS_TYPE args, std::string& errorMessage, TranslationInfo &translationInfo) {
	if (args.Length() == 0) {
		errorMessage = "at least one argument is needed - the maxtime";
		return ADDONARG;
	}

	if (args.Length() > 0 && args[0]->IsFunction()) {
		errorMessage = "at least one argument is needed before the callback - the maxtime";
		return ADDONARG;
	}

	for (int i=0; i < args.Length(); i++) {
		v8::Handle<v8::Value> currentVal = args[i];

		//Undefined or null will choose default value
		if (currentVal->IsUndefined() || currentVal->IsNull()) {
			continue;
		} 

		if (i > 0 && currentVal->IsFunction()) { //An async signature
			NanAssignPersistent(translationInfo.callback, args[i].As<Function>());
			return 0;
		}

		switch(i) {
			case 0: //maxtime
				if (!currentVal->IsNumber()) {
					errorMessage = "maxtime argument must be a number";
					return ADDONARG;
				}

				translationInfo.maxtime = currentVal->ToNumber()->Value();
				if (translationInfo.maxtime <= 0) {
					errorMessage = "maxtime must be greater than 0";
					return ADDONARG;
				}

				break;

			case 1: //maxmem
				if (!currentVal->IsUndefined()) {
					if (!currentVal->IsNumber()) {
						errorMessage = "maxmem argument must be a number";
						return ADDONARG;
					}

					if (currentVal->ToInteger()->Value() > 0) {
						translationInfo.maxmem = (size_t)Local<Number>(args[i]->ToInteger())->Value();
					}
				}

				break;

			case 2: //maxmemfrac
				if (!currentVal->IsUndefined()) {
					if (!currentVal->IsNumber()) {
						errorMessage = "max_memfrac argument must be a number";
						return ADDONARG;
					}

					if (currentVal->ToNumber()->Value() > 0) {
						translationInfo.maxmemfrac = Local<Number>(args[i]->ToNumber())->Value();
					}
				}

				break; 
		}
	}

	return 0;
}

//
// Creates the actual JSON object that will be returned to the user
//
void 
createJSONObject(Local<Object> &obj, const int &N, const uint32_t &r, const uint32_t &p) {
	obj = NanNew<Object>();
	obj->Set(NanNew<String>("N"), NanNew<Integer>(N));
	obj->Set(NanNew<String>("r"), NanNew<Integer>(r));
	obj->Set(NanNew<String>("p"), NanNew<Integer>(p));
}

//
// Work funtion: Work performed here
//
void
ParamsWork(TranslationInfo* translationInfo) {
	translationInfo->result = pickparams(&translationInfo->N, &translationInfo->r, &translationInfo->p, translationInfo->maxtime, translationInfo->maxmem, translationInfo->maxmemfrac);
}

//
// Asynchronous: Wrapper to work function
//
void 
ParamsAsyncWork(uv_work_t* req) {
	ParamsWork(static_cast<TranslationInfo*>(req->data));
}

//
// Synchronous: After work function
//
void
ParamsSyncAfterWork(Local<Object>& obj, const TranslationInfo *translationInfo) {
	if (translationInfo->result) { //There has been an error
		NanThrowError(Internal::MakeErrorObject(SCRYPT,translationInfo->result));
	} else {
		createJSONObject(obj, translationInfo->N, translationInfo->r, translationInfo->p);
	}
}

//
// Asynchronous: After work function
//
void
ParamsAsyncAfterWork(uv_work_t* req) {
	NanScope();
	TranslationInfo* translationInfo = static_cast<TranslationInfo*>(req->data);
	Local<Object> obj;

	if (!translationInfo->result) {
		createJSONObject(obj, translationInfo->N, translationInfo->r, translationInfo->p);
	}

	Local<Value> argv[2] = {
		Internal::MakeErrorObject(SCRYPT,translationInfo->result),
		NanNew(obj)
	};

	TryCatch try_catch;

	NanMakeCallback(NanGetCurrentContext()->Global(), NanNew(translationInfo->callback), 2, argv);
	if (try_catch.HasCaught()) {
		node::FatalException(try_catch);
	}

	//Clean up
	delete translationInfo;
	delete req;
}

//
// Params: Parses arguments and determines what type (sync or async) this function is
//
NAN_METHOD(Params) {
	NanScope();

	uint8_t parseResult = 0;
	Local<Object> params;
	std::string validateMessage;
	TranslationInfo* translationInfo = new TranslationInfo(Local<Object>::Cast(args.Holder()->Get(NanNew<String>("config"))));

	//Validate arguments and determine function type
	if ((parseResult = AssignArguments(args, validateMessage, *translationInfo))) {
		NanThrowError(Internal::MakeErrorObject(parseResult, validateMessage));
	} else {
		if (translationInfo->callback.IsEmpty()) { 
			//Synchronous
			
			ParamsWork(translationInfo);
			ParamsSyncAfterWork(params, translationInfo);
		} else { 
			//Asynchronous work request
			uv_work_t *req = new uv_work_t();
			req->data = translationInfo;

			//Schedule work request
			int status = uv_queue_work(uv_default_loop(), req, ParamsAsyncWork, (uv_after_work_cb)ParamsAsyncAfterWork);
			if (status != 0)
				assert(status == 0);
		}
	}

	//Only clean up heap if synchronous
	if (translationInfo->callback.IsEmpty()) {
		delete translationInfo;
		NanReturnValue(params);
	}	

	NanReturnUndefined();
}

} //unnamed namespace

//
// The Construtor That Is Exposed To JavaScript
//
NAN_METHOD(CreateParameterFunction) {
	NanScope();

	Local<ObjectTemplate> params = ObjectTemplate::New();

	params->SetCallAsFunctionHandler(Params);
	params->Set(NanNew<String>("config"), CreateScryptConfigObject("params"), v8::ReadOnly);

	NanReturnValue(NanNew(params)->NewInstance());
}
