#include "BookTextApi.h"
#include "BookText.h"
#include "CallNativeApi.h"
#include "NullPointerException.h"

extern CallNativeApi::NativeCallRequirements g_nativeCallRequirements;

/*
  THORNSWOOD PATCH (Thornswood #4566). A written book reads the server's
  words in the game's own Book Menu.

  Every book people write is one book record carried under a name the
  server issued, so the record's own words are the same for all of them. The
  plugin reads the server's words for the copy being opened and hands them
  here; this opens the Book Menu on that record with those words.

  BookMenu::OpenBookMenu takes the words as a borrowed const BSString&, and
  whether the game copies them before it returns is not established. So the
  words are held in g_held from before the open is queued until the Book
  Menu shown for them has closed (BookText.h), whichever way the game reads
  them. The menu's open and close come from the UI's MenuOpenCloseEvent, the
  same source BrowserApiTilted watches the Cursor Menu on; the sink is added
  before the first open is queued, so it hears that open.
*/
namespace {

BookTextHold<RE::BSString> g_held;

uint32_t ShownBook()
{
  auto ui = RE::UI::GetSingleton();
  if (!ui) {
    return 0;
  }
  auto menu = ui->GetMenu<RE::BookMenu>();
  if (!menu) {
    return 0;
  }
  auto book = menu->GetTargetForm();
  return book ? book->GetFormID() : 0;
}

class BookMenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::MenuOpenCloseEvent* event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
  {
    if (!event || event->menuName != RE::BookMenu::MENU_NAME) {
      return RE::BSEventNotifyControl::kContinue;
    }
    if (event->opening) {
      g_held.Opened(ShownBook());
    } else {
      g_held.Closed();
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

BookMenuSink g_sink;
bool g_watching = false;

bool Watch()
{
  if (g_watching) {
    return true;
  }
  auto ui = RE::UI::GetSingleton();
  if (!ui) {
    return false;
  }
  RE::BSTEventSource<RE::MenuOpenCloseEvent>* source = ui;
  source->AddEventSink(&g_sink);
  g_watching = true;
  spdlog::info("book text: watching the Book Menu");
  return true;
}
}

// openBookWithText(bookFormId, text): true when the open was queued. False,
// queueing nothing and holding nothing, when the game is not ready, the UI
// cannot be watched, or bookFormId is not a book.
Napi::Value BookTextApi::OpenBookWithText(const Napi::CallbackInfo& info)
{
  const auto env = info.Env();
  const uint32_t bookId = NapiHelper::ExtractUInt32(info[0], "bookFormId");
  const std::string text = NapiHelper::ExtractString(info[1], "text");

  if (!g_nativeCallRequirements.gameThrQ) {
    throw NullPointerException("gameThrQ");
  }
  if (!RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookId) || !Watch()) {
    return Napi::Boolean::New(env, false);
  }

  const RE::BSString* words = &g_held.Hold(bookId, text);
  g_nativeCallRequirements.gameThrQ->AddTask([bookId, words](Viet::Void) {
    auto book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookId);
    if (!book) {
      spdlog::warn("book text: book {:x} is gone before its menu opened",
                   bookId);
      return;
    }
    RE::BookMenu::OpenBookMenu(*words, nullptr, nullptr, book, RE::NiPoint3(),
                               RE::NiMatrix3(), 1.0f, true);
  });
  return Napi::Boolean::New(env, true);
}

void BookTextApi::Register(Napi::Env env, Napi::Object& exports)
{
  exports.Set("openBookWithText",
              Napi::Function::New(
                env, NapiHelper::WrapCppExceptions(OpenBookWithText)));
}
