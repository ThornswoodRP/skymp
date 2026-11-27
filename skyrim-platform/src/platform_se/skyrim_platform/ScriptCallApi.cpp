#include "ScriptCallApi.h"
#include "CallNativeApi.h"
#include "CreatePromise.h"
#include "NullPointerException.h"
#include "ScriptCall.h"
#include "StringHolder.h"
#include "VmCallback.h"
#include "VmFunctionArguments.h"

#include <spdlog/spdlog.h>

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
  const Variable* v =
    bound->GetProperty(StringHolder::ThreadSingleton()[propertyName]);
  if (!v) {
    return env.Undefined();
  }
  switch (v->varType.GetUnmangledRawType()) {
    case TypeInfo::RawType::kBool:
      return Napi::Boolean::New(env, v->GetBool());
    case TypeInfo::RawType::kInt:
      return Napi::Number::New(env, v->GetSInt());
    case TypeInfo::RawType::kFloat:
      return Napi::Number::New(env, v->GetFloat());
    case TypeInfo::RawType::kString:
      return Napi::String::New(env, v->GetString().data());
    default:
      return env.Undefined();
  }
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
}
