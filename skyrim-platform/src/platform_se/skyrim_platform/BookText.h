#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>

// THORNSWOOD (Thornswood #4566). The words openBookWithText (BookTextApi.cpp)
// shows in the game's own Book Menu, kept alive for as long as that menu may
// read them.
//
// A book people write in the world is one book record carried under a name
// the server issued, and its words are the server's, not the record's. The
// game opens a book's menu with BookMenu::OpenBookMenu, which takes the words
// as a borrowed const BSString&. Nothing in CommonLibSSE says whether the game
// copies them before it returns or keeps reading them while the menu is up,
// so neither is assumed: each text is held from before the open is queued
// until the Book Menu it was opened for has closed.
//
// WHICH OPEN IS OURS. Every written book is the one record, so the open that
// sits next to ours is the game opening that same record itself (a copy read
// the normal way), not some other book. A text is held from the moment
// openBookWithText is called, but it can be claimed by a Book Menu open only
// once its own task, on the game thread, has said it is about to call
// OpenBookMenu (Issued). An open of the record before that is the game's,
// and neither it nor its close can touch ours. Issued is said on the game
// thread just before the call, where the menu's events also arrive, so no
// open comes between the two; whether the game raises the open inside the
// call or on a later UI pass, the entry is already claimable.
//
// Among issued entries, an open of their record claims the oldest, and its
// close lets it go. An open of another record, or one whose record could not
// be read (0), claims nothing, and its close lets nothing go. A task that
// never calls OpenBookMenu (its book was gone) lets its own words go
// (Dropped); one that did keeps them until its menu closes. A text the game
// never opens after its call stays held; nothing here guesses it will not.
//
// Text is the string type the menu is given (RE::BSString in the game,
// std::string in misc/tests/node/test_book_text_native.cpp). A std::list
// keeps every held text at one address however many more are held.
template <class Text>
class BookTextHold
{
public:
  // Holds `text` for an open of `bookId`. The reference stays valid until
  // the Book Menu opened for it has closed, or its task drops it.
  const Text& Hold(uint32_t bookId, const std::string& text)
  {
    std::lock_guard<std::mutex> l(m_);
    held_.emplace_back(bookId, text);
    return held_.back().text;
  }

  // Its task is about to call OpenBookMenu with `text`: from now an open of
  // its record may claim it.
  void Issued(const Text& text)
  {
    std::lock_guard<std::mutex> l(m_);
    auto it = Find(text);
    if (it != held_.end()) {
      it->issued = true;
    }
  }

  // Its task did not call OpenBookMenu: nothing will read `text`.
  void Dropped(const Text& text)
  {
    std::lock_guard<std::mutex> l(m_);
    auto it = Find(text);
    if (it != held_.end() && !it->issued) {
      held_.erase(it);
    }
  }

  // The Book Menu opened, showing `bookId`.
  void Opened(uint32_t bookId)
  {
    std::lock_guard<std::mutex> l(m_);
    open_ = held_.end();
    if (bookId == 0) {
      return;
    }
    for (auto it = held_.begin(); it != held_.end(); ++it) {
      if (it->issued && !it->opened && it->bookId == bookId) {
        it->opened = true;
        open_ = it;
        return;
      }
    }
  }

  // The Book Menu closed. The text it was opened for, if it was ours, goes.
  void Closed()
  {
    std::lock_guard<std::mutex> l(m_);
    if (open_ != held_.end()) {
      held_.erase(open_);
    }
    open_ = held_.end();
  }

  size_t Held() const
  {
    std::lock_guard<std::mutex> l(m_);
    return held_.size();
  }

  // The oldest text still held.
  const Text& Front() const
  {
    std::lock_guard<std::mutex> l(m_);
    return held_.front().text;
  }

private:
  struct Entry
  {
    Entry(uint32_t id, const std::string& s)
      : bookId(id)
      , text(s.c_str())
    {
    }
    uint32_t bookId;
    bool issued = false;
    bool opened = false;
    Text text;
  };

  typename std::list<Entry>::iterator Find(const Text& text)
  {
    for (auto it = held_.begin(); it != held_.end(); ++it) {
      if (&it->text == &text) {
        return it;
      }
    }
    return held_.end();
  }

  mutable std::mutex m_;
  std::list<Entry> held_;
  typename std::list<Entry>::iterator open_ = held_.end();
};
