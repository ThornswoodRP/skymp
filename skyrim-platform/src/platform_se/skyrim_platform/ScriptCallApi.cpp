#include "ScriptCallApi.h"
#include "CallNative.h"
#include "CallNativeApi.h"
#include "CreatePromise.h"
#include "NullPointerException.h"
#include "ScriptCall.h"
#include "Sp3NativeValueCasts.h"
#include "StringHolder.h"
#include "VmCallback.h"
#include "VmFunctionArguments.h"

#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>

extern CallNativeApi::NativeCallRequirements g_nativeCallRequirements;

/*
  THORNSWOOD PATCH (Thornswood #4511). A plugin runs a script's own function.

  callNative runs native functions only ("Function is not native"), so a
  plugin could not ask a mod's script to do anything the mod wrote in
  Papyrus. QuickLoot IE is why that matters: it keeps every option of its
  menu in its script and reads them back at each session's start from its own
  file, Data\SKSE\Plugins\QuickLootIE\DefaultConfig.json, but writes that file
  only from its Export settings button, so under SkyMP, with no save, it starts
  from its defaults every session. Its export steps (ExportSettings_General and
  the rest) are its own Papyrus functions; this is how a plugin runs them.

  The call is the VM's own DispatchMethodCall on the script bound to the form,
  so it runs on a Papyrus stack like any other call into that script. It
  finishes later, on the VM's thread; the Promise is settled from the JS queue
  once the VM says the function returned, so a plugin can run steps in order.
  Arguments are strings and booleans, which is what the calls it is for take;
  anything else is refused before anything is started.

  getScriptProperty reads one property of such a script as it is now, so a
  plugin can wait for the script's own state before it runs a step: QuickLoot
  sets IsInitialLoad to False once it has read its file at the session's
  start, and writing the file before then would put its defaults over what
  was kept.

  getScriptVariable and setScriptVariable read and write one of the script's
  own variables, arrays included. SkyUI keeps its favourite groups only in
  SKI_FavoritesManager's arrays (each group's items and their item ids, its
  main hand, off hand and icon), filled only by the Favorites menu's own
  events, and no function of it takes a whole group from outside. With no
  save they were empty at every session's start. A plugin reads them when
  the menu closes and puts them back at the next start. A value goes in
  only when it is the variable's own kind (ScriptValueMismatch), and an
  array is filled in place at its own size, every value checked before any
  is written, so the script never holds half of one.
*/
namespace {

using Arg = ScriptCallArg;

ScriptCallWaiting<Napi::FunctionReference> g_waiting;

void Settle(Napi::Env env, uint64_t id, bool ran)
{
  auto resolve = g_waiting.Take(id);
  if (!resolve) {
    return;
  }
  resolve->Value().Call(env.Undefined(), { Napi::Boolean::New(env, ran) });
}

// The script `scriptName` bound to form `formId`, or false with why not.
bool FindBound(VM* vm, uint32_t formId, const std::string& scriptName,
               RE::VMHandle& handle,
               RE::BSTSmartPointer<RE::BSScript::Object>& bound,
               std::string& why)
{
  if (!vm) {
    why = "there is no Papyrus VM yet";
    return false;
  }
  auto form = RE::TESForm::LookupByID(formId);
  if (!form) {
    why = "there is no such form";
    return false;
  }
  auto policy = vm->GetObjectHandlePolicy();
  if (!policy) {
    why = "the VM has no handle policy";
    return false;
  }
  handle = policy->GetHandleForObject(
    static_cast<RE::VMTypeID>(form->GetFormType()), form);
  if (!vm->FindBoundObject(handle, scriptName.c_str(), bound) || !bound) {
    why = "the script is not attached to it";
    return false;
  }
  return true;
}

// What the plugin is given for a script value as it is now: a number, a
// boolean, a string, a form, an array of one of those, null for a None
// object or array, or undefined for a kind there is no JavaScript value for.
Napi::Value ReadValue(Napi::Env env, const Variable* v, const char* what)
{
  if (!v) {
    return env.Undefined();
  }
  if (v->IsArray() && !v->GetArray()) {
    return env.Null();
  }
  try {
    return SP3NativeValueCasts::GetSingleton().NativeValueToJsValue(
      env, CallNative::VariableToAnySafe(*v));
  } catch (std::exception& e) {
    spdlog::warn("getScript{}: {}", what, e.what());
    return env.Undefined();
  }
}

ScriptValueKind KindOf(const TypeInfo& type)
{
  switch (type.GetUnmangledRawType()) {
    case TypeInfo::RawType::kInt:
      return ScriptValueKind::Int;
    case TypeInfo::RawType::kFloat:
      return ScriptValueKind::Float;
    case TypeInfo::RawType::kBool:
      return ScriptValueKind::Bool;
    case TypeInfo::RawType::kString:
      return ScriptValueKind::String;
    case TypeInfo::RawType::kObject:
      return ScriptValueKind::Object;
    default:
      return ScriptValueKind::Other;
  }
}

JsValueKind KindOf(const Napi::Value& v)
{
  if (v.IsNumber()) {
    return JsValueKind::Number;
  }
  if (v.IsBoolean()) {
    return JsValueKind::Boolean;
  }
  if (v.IsString()) {
    return JsValueKind::String;
  }
  if (v.IsNull() || v.IsUndefined()) {
    return JsValueKind::Nothing;
  }
  if (v.IsObject() && !v.IsArray()) {
    return JsValueKind::Object;
  }
  return JsValueKind::Other;
}

// `js` as a script value of `type`, or a throw that says why it is not one.
Variable ToScriptValue(const TypeInfo& type, const Napi::Value& js,
                       const std::string& what)
{
  const ScriptValueKind want = KindOf(type);
  const std::string why = ScriptValueMismatch(want, KindOf(js));
  if (!why.empty()) {
    throw std::runtime_error("setScriptVariable: " + what + ": " + why);
  }
  Variable out;
  switch (want) {
    case ScriptValueKind::Int:
      out.SetSInt(js.As<Napi::Number>().Int32Value());
      break;
    case ScriptValueKind::Float:
      out.SetFloat(js.As<Napi::Number>().FloatValue());
      break;
    case ScriptValueKind::Bool:
      out.SetBool(js.As<Napi::Boolean>().Value());
      break;
    case ScriptValueKind::String:
      out.SetString(js.As<Napi::String>().Utf8Value());
      break;
    default: {
      auto form = js.IsObject()
        ? SP3NativeValueCasts::GetSingleton().JsObjectToNativeObject(js)
        : CallNative::ObjectPtr();
      if (form) {
        out = CallNative::AnySafeToVariable(form, false);
      } else {
        out.SetObject(RE::BSTSmartPointer<RE::BSScript::Object>(),
                      type.GetRawType());
      }
      break;
    }
  }
  return out;
}

// Starts the call, or says in `why` why it was not started.
bool Dispatch(uint32_t formId, const std::string& scriptName,
              const std::string& functionName, const std::vector<Arg>& args,
              uint64_t id, std::string& why)
{
  auto vm = VM::GetSingleton();
  RE::VMHandle handle = 0;
  RE::BSTSmartPointer<RE::BSScript::Object> bound;
  if (!FindBound(vm, formId, scriptName, handle, bound, why)) {
    return false;
  }

  VmFunctionArguments vmArgs(
    [](void* n) { return (size_t)n; },
    [&args](size_t i) {
      Variable v;
      const Arg& a = args[i];
      if (a.isString) {
        v.SetString(StringHolder::ThreadSingleton()[a.text]);
      } else {
        v.SetBool(a.flag);
      }
      return v;
    },
    (void*)args.size());

  auto jsThrQ = g_nativeCallRequirements.jsThrQ;
  auto done = VmCallback::New([jsThrQ, id](const Variable&) {
    jsThrQ->AddTask([id](Napi::Env env) { Settle(env, id, true); });
  });

  const FixedString& cls = StringHolder::ThreadSingleton()[scriptName];
  const FixedString& fn = StringHolder::ThreadSingleton()[functionName];
  if (!vm->DispatchMethodCall2(handle, cls, fn, &vmArgs, done)) {
    why = "the VM would not start it (no such function, or the arguments do "
          "not match it)";
    return false;
  }
  return true;
}
}

Napi::Value ScriptCallApi::RunScriptFunction(const Napi::CallbackInfo& info)
{
  const auto env = info.Env();
  const uint32_t formId = NapiHelper::ExtractUInt32(info[0], "formId");
  const std::string scriptName =
    NapiHelper::ExtractString(info[1], "scriptName");
  const std::string functionName =
    NapiHelper::ExtractString(info[2], "functionName");

  std::vector<Arg> args;
  if (info.Length() > 3 && !info[3].IsUndefined()) {
    if (!info[3].IsArray()) {
      throw std::runtime_error("runScriptFunction: args must be an array");
    }
    auto list = info[3].As<Napi::Array>();
    for (uint32_t i = 0; i < list.Length(); ++i) {
      Napi::Value v = list.Get(i);
      Arg a;
      if (v.IsString()) {
        a.isString = true;
        a.text = v.As<Napi::String>().Utf8Value();
      } else if (v.IsBoolean()) {
        a.flag = v.As<Napi::Boolean>().Value();
      } else {
        throw std::runtime_error("runScriptFunction: args[" +
                                 std::to_string(i) +
                                 "] is not a string or a boolean");
      }
      args.push_back(std::move(a));
    }
  }

  if (!g_nativeCallRequirements.jsThrQ) {
    throw NullPointerException("jsThrQ");
  }

  auto executor = Napi::Function::New(
    env, [=](const Napi::CallbackInfo& p) -> Napi::Value {
      const uint64_t id =
        g_waiting.Add(Napi::Persistent(p[0].As<Napi::Function>()));
      std::string why;
      if (!Dispatch(formId, scriptName, functionName, args, id, why)) {
        spdlog::warn("runScriptFunction: {}.{} on {:x} not started: {}",
                     scriptName, functionName, formId, why);
        Settle(p.Env(), id, false);
      }
      return p.Env().Undefined();
    });
  return CreatePromise(executor);
}

Napi::Value ScriptCallApi::GetScriptProperty(const Napi::CallbackInfo& info)
{
  const auto env = info.Env();
  const uint32_t formId = NapiHelper::ExtractUInt32(info[0], "formId");
  const std::string scriptName =
    NapiHelper::ExtractString(info[1], "scriptName");
  const std::string propertyName =
    NapiHelper::ExtractString(info[2], "propertyName");

  RE::VMHandle handle = 0;
  RE::BSTSmartPointer<RE::BSScript::Object> bound;
  std::string why;
  if (!FindBound(VM::GetSingleton(), formId, scriptName, handle, bound,
                 why)) {
    return env.Undefined();
  }
  return ReadValue(
    env, bound->GetProperty(StringHolder::ThreadSingleton()[propertyName]),
    "Property");
}

Napi::Value ScriptCallApi::GetScriptVariable(const Napi::CallbackInfo& info)
{
  const auto env = info.Env();
  const uint32_t formId = NapiHelper::ExtractUInt32(info[0], "formId");
  const std::string scriptName =
    NapiHelper::ExtractString(info[1], "scriptName");
  const std::string variableName =
    NapiHelper::ExtractString(info[2], "variableName");

  RE::VMHandle handle = 0;
  RE::BSTSmartPointer<RE::BSScript::Object> bound;
  std::string why;
  if (!FindBound(VM::GetSingleton(), formId, scriptName, handle, bound,
                 why)) {
    return env.Undefined();
  }
  return ReadValue(
    env, bound->GetVariable(StringHolder::ThreadSingleton()[variableName]),
    "Variable");
}

Napi::Value ScriptCallApi::SetScriptVariable(const Napi::CallbackInfo& info)
{
  const auto env = info.Env();
  const uint32_t formId = NapiHelper::ExtractUInt32(info[0], "formId");
  const std::string scriptName =
    NapiHelper::ExtractString(info[1], "scriptName");
  const std::string variableName =
    NapiHelper::ExtractString(info[2], "variableName");
  const Napi::Value value = info[3];

  RE::VMHandle handle = 0;
  RE::BSTSmartPointer<RE::BSScript::Object> bound;
  std::string why;
  if (!FindBound(VM::GetSingleton(), formId, scriptName, handle, bound,
                 why)) {
    spdlog::warn("setScriptVariable: {}.{} on {:x} not written: {}",
                 scriptName, variableName, formId, why);
    return Napi::Boolean::New(env, false);
  }
  Variable* target =
    bound->GetVariable(StringHolder::ThreadSingleton()[variableName]);
  if (!target) {
    spdlog::warn("setScriptVariable: {} has no variable {}", scriptName,
                 variableName);
    return Napi::Boolean::New(env, false);
  }

  if (!target->varType.IsArray()) {
    *target = ToScriptValue(target->varType, value, variableName);
    return Napi::Boolean::New(env, true);
  }

  auto array = target->GetArray();
  const size_t given =
    value.IsArray() ? value.As<Napi::Array>().Length() : 0;
  why = ScriptArrayMismatch(static_cast<bool>(array),
                            array ? array->size() : 0, value.IsArray(), given);
  if (!why.empty()) {
    throw std::runtime_error("setScriptVariable: " + variableName + ": " +
                             why);
  }
  auto list = value.As<Napi::Array>();
  std::vector<Variable> next;
  next.reserve(given);
  for (uint32_t i = 0; i < given; ++i) {
    next.push_back(ToScriptValue(array->type_info(), list.Get(i),
                                 variableName + "[" + std::to_string(i) +
                                   "]"));
  }
  for (uint32_t i = 0; i < given; ++i) {
    (*array)[i] = next[i];
  }
  return Napi::Boolean::New(env, true);
}

void ScriptCallApi::Clear()
{
  g_waiting.Clear();
}

void ScriptCallApi::Register(Napi::Env env, Napi::Object& exports)
{
  exports.Set("runScriptFunction",
              Napi::Function::New(
                env, NapiHelper::WrapCppExceptions(RunScriptFunction)));
  exports.Set("getScriptProperty",
              Napi::Function::New(
                env, NapiHelper::WrapCppExceptions(GetScriptProperty)));
  exports.Set("getScriptVariable",
              Napi::Function::New(
                env, NapiHelper::WrapCppExceptions(GetScriptVariable)));
  exports.Set("setScriptVariable",
              Napi::Function::New(
                env, NapiHelper::WrapCppExceptions(SetScriptVariable)));
}
