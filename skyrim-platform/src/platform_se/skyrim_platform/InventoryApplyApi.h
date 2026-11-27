#pragma once

#include "NapiHelper.h"

namespace InventoryApplyApi {
void Register(Napi::Env env, Napi::Object& exports);
// Called synchronously at native lifecycle boundaries, before JS events.
void CancelAll();
void CancelReference(uint32_t referenceId);
}
