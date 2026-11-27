#pragma once

#include "NapiHelper.h"

// THORNSWOOD PATCH (Thornswood #4511). ScriptCallApi.cpp says what it is.
namespace ScriptCallApi {

// runScriptFunction(formId, scriptName, functionName, args): runs a function
// of the script `scriptName` attached to form `formId`, the script's own
// Papyrus function and not only a native one, with `args` (strings and
// booleans). A Promise: true once the function has returned, false when it
// could not be started.
Napi::Value RunScriptFunction(const Napi::CallbackInfo& info);

// Lets go of every Promise still waiting, when the plugins are reloaded.
void Clear();

void Register(Napi::Env env, Napi::Object& exports);
}
