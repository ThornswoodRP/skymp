#pragma once

#include "NapiHelper.h"

// THORNSWOOD PATCH (Thornswood #4566). BookText.h says what it is.
namespace BookTextApi {

// openBookWithText(bookFormId, text): opens the game's own Book Menu on the
// book record bookFormId, showing `text` in place of the record's own words.
Napi::Value OpenBookWithText(const Napi::CallbackInfo& info);

void Register(Napi::Env env, Napi::Object& exports);
}
