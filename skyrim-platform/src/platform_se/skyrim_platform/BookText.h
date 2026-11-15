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
// Which menu is ours is told by the book record it shows (BookMenu's target
// form). Any book of the game's own opens the same menu, so an open of
// another record, or one whose record could not be read (0), is not ours,
// and its close lets nothing go. Two opens of ours are matched first in,
// first out, the order the game handles its UI queue. A text whose open the
// game never makes stays held; nothing here guesses that it will not come.
//
// Text is the string type the menu is given (RE::BSString in the game,
// std::string in misc/tests/node/test_book_text_native.cpp). A std::list
// keeps every held text at one address however many more are held.
template <class Text>
class BookTextHold
{
public:
  // Holds `text` for an open of `bookId`. The reference stays valid until
  // the Book Menu opened for it has closed.
  const Text& Hold(uint32_t bookId, const std::string& text)
  {
    std::lock_guard<std::mutex> l(m_);
    held_.emplace_back(bookId, text);
    return held_.back().text;
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
      if (!it->opened && it->bookId == bookId) {
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
    bool opened = false;
    Text text;
  };

  mutable std::mutex m_;
  std::list<Entry> held_;
  typename std::list<Entry>::iterator open_ = held_.end();
};
