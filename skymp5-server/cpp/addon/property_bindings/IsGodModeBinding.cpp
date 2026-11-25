#include "IsGodModeBinding.h"
#include "NapiHelper.h"

Napi::Value IsGodModeBinding::Get(Napi::Env env, ScampServer& server,
                                  uint32_t formId)
{
  auto& actor = server.GetPartOne()->worldState.GetFormAt<MpActor>(formId);
  return Napi::Boolean::New(env, actor.IsGodMode());
}

void IsGodModeBinding::Set(Napi::Env env, ScampServer& server, uint32_t formId,
                           Napi::Value value)
{
  auto& actor = server.GetPartOne()->worldState.GetFormAt<MpActor>(formId);
  actor.SetGodMode(NapiHelper::ExtractBoolean(value, "isGodMode"));
}
