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

// getScriptProperty(formId, scriptName, propertyName): the property's value
// as it is now (a boolean, a number, a string, a form, or an array of one of
// those; null for a None object or array), or undefined when the script is
// not attached to the form or has no such property.
Napi::Value GetScriptProperty(const Napi::CallbackInfo& info);

// getScriptVariable(formId, scriptName, variableName): the same for one of
// the script's own variables, the ones it does not show as properties.
Napi::Value GetScriptVariable(const Napi::CallbackInfo& info);

// setScriptVariable(formId, scriptName, variableName, value): writes the
// variable. An array is filled in place and keeps its size, so `value` is
// an array of exactly that many. Each value must be the variable's kind (a
// number, a boolean, a string, or a form or null for an object). true once
// written, false when the script is not attached to the form or has no such
// variable; a value that does not fit throws, and nothing is written.
Napi::Value SetScriptVariable(const Napi::CallbackInfo& info);

// Lets go of every Promise still waiting, when the plugins are reloaded.
void Clear();

void Register(Napi::Env env, Napi::Object& exports);
}
