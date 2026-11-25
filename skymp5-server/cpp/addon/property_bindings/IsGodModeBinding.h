#pragma once
#include "PropertyBinding.h"

class IsGodModeBinding : public PropertyBinding
{
public:
  std::string GetPropertyName() const override { return "isGodMode"; }
  Napi::Value Get(Napi::Env env, ScampServer& server,
                  uint32_t formId) override;
  void Set(Napi::Env env, ScampServer& server, uint32_t formId,
           Napi::Value value) override;
};
