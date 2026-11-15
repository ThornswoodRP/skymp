// Thornswood #4566. BookTextHold, the words openBookWithText
// (BookTextApi.cpp) hands the game's Book Menu, over a stand-in for the
// menu's open and close events.
//
// The game's BookMenu::OpenBookMenu takes the words as a borrowed
// const BSString&, and nothing in CommonLibSSE says whether the game copies
// them before it returns or reads them later while the menu is up. So the
// words are held until the menu they were opened for has closed, and every
// case below asks that they are still there, at the same address, for as
// long as the game might read them.
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "BookText.h"

constexpr uint32_t kWritten = 0x0a000801;  // the record every written book is
constexpr uint32_t kVanilla = 0x0001b0e8;  // a book of the game's own

int main()
{
  { // Held while the menu is up, and let go when it closes.
    BookTextHold<std::string> h;
    const std::string& words = h.Hold(kWritten, "The Ballad of Hroki");
    assert(h.Held() == 1);
    h.Opened(kWritten);
    assert(h.Held() == 1 && words == "The Ballad of Hroki");
    h.Closed();
    assert(h.Held() == 0);
  }
  { // Opening is not the end: a menu that reads the words after it opens
    // still finds them.
    BookTextHold<std::string> h;
    const std::string* at = &h.Hold(kWritten, "page one");
    h.Opened(kWritten);
    assert(at == &h.Front() && *at == "page one");
  }
  { // A book of the game's own opening and closing while ours is still
    // queued does not let ours go.
    BookTextHold<std::string> h;
    const std::string& words = h.Hold(kWritten, "mine");
    h.Opened(kVanilla);
    h.Closed();
    assert(h.Held() == 1 && words == "mine");
    h.Opened(kWritten);
    h.Closed();
    assert(h.Held() == 0);
  }
  { // Two of ours queued: the first close lets the first go, and the second
    // is untouched where it lies.
    BookTextHold<std::string> h;
    h.Hold(kWritten, "first");
    const std::string* second = &h.Hold(kWritten, "second");
    h.Opened(kWritten);
    h.Closed();
    assert(h.Held() == 1 && *second == "second" && &h.Front() == second);
    h.Opened(kWritten);
    h.Closed();
    assert(h.Held() == 0);
  }
  { // A close with nothing of ours open lets nothing go.
    BookTextHold<std::string> h;
    h.Hold(kWritten, "waiting");
    h.Closed();
    assert(h.Held() == 1);
  }
  { // Holding more never moves what is already held.
    BookTextHold<std::string> h;
    const std::string* at = &h.Hold(kWritten, "kept");
    for (int i = 0; i < 200; ++i) {
      h.Hold(kWritten, std::string(64, 'x'));
    }
    assert(&h.Front() == at && *at == "kept");
  }
  { // A menu whose book could not be read (0) is not ours.
    BookTextHold<std::string> h;
    h.Hold(kWritten, "kept");
    h.Opened(0);
    h.Closed();
    assert(h.Held() == 1);
  }
  std::cout << "book text: 7 cases passed" << std::endl;
  return 0;
}
