#pragma once

#include "NapiHelper.h"

namespace Win32Api {
Napi::Value LoadUrl(const Napi::CallbackInfo& info);

Napi::Value ExitProcess(const Napi::CallbackInfo& info);

// Thornswood #4523. The window that has the keyboard (ForegroundWindow.h).
Napi::Value ForegroundWindow(const Napi::CallbackInfo& info);

inline void Register(Napi::Env env, Napi::Object& exports)
{
  auto win32 = Napi::Object::New(env);
  win32.Set(
    "loadUrl",
    Napi::Function::New(env,
                        NapiHelper::WrapCppExceptions(
                          [=](const Napi::CallbackInfo& info) -> Napi::Value {
                            return LoadUrl(info);
                          })));

  win32.Set(
    "exitProcess",
    Napi::Function::New(env,
                        NapiHelper::WrapCppExceptions(
                          [=](const Napi::CallbackInfo& info) -> Napi::Value {
                            return ExitProcess(info);
                          })));

  win32.Set(
    "foregroundWindow",
    Napi::Function::New(env,
                        NapiHelper::WrapCppExceptions(
                          [=](const Napi::CallbackInfo& info) -> Napi::Value {
                            return ForegroundWindow(info);
                          })));

  exports.Set("win32", win32);
}
}
