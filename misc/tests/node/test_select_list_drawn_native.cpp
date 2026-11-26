// Thornswood #6882. PopupLayer.h, what DX11RenderHandler keeps of the list a
// page's <select> opens. Chromium paints that list on its own (PET_POPUP),
// and before this the handler took only the page's picture, so the list was
// open but never drawn. Prints "ok" when every assert holds.
#include "PopupLayer.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
  PopupLayer p;

  // Nothing is drawn before Chromium opens a list.
  assert(!p.ShouldDraw());
  assert(!p.Takes(10, 10));

  // The order Chromium uses: shown, then placed, then painted.
  p.Show(true);
  assert(!p.ShouldDraw());
  p.Size(140, 300, 260, 180);
  assert(p.ShouldDraw());
  assert(p.x == 140 && p.y == 300 && p.width == 260 && p.height == 180);
  assert(p.Takes(260, 180));
  // A picture of another size is from before a move or a resize.
  assert(!p.Takes(260, 200));
  assert(!p.Takes(0, 0));

  // Placed before it is shown is kept when it is shown.
  PopupLayer q;
  q.Size(5, 6, 7, 8);
  assert(!q.ShouldDraw());
  q.Show(true);
  assert(q.ShouldDraw() && q.Takes(7, 8));

  // Where Chromium put it is where it is drawn, even partly off the page:
  // that is where it takes the mouse.
  p.Size(-20, 1000, 260, 180);
  assert(p.x == -20 && p.y == 1000 && p.Takes(260, 180));

  // Closed, it is not drawn and takes no picture, and a new one starts empty.
  p.Show(false);
  assert(!p.ShouldDraw() && !p.Takes(260, 180));
  p.Show(true);
  assert(!p.ShouldDraw());

  // A negative size from Chromium is no size.
  p.Size(0, 0, -1, 40);
  assert(!p.ShouldDraw());

  // The rows land at the texture's pitch, which can be wider than the
  // picture: 3 pixels wide, 2 high, into rows 16 bytes apart.
  const int w = 3, h = 2;
  std::vector<uint8_t> src(w * h * 4);
  for (size_t i = 0; i < src.size(); ++i)
    src[i] = static_cast<uint8_t>(i + 1);
  std::vector<uint8_t> dest(16 * h, 0xEE);
  PopupLayer::CopyRows(dest.data(), 16, src.data(), w, h);
  for (int r = 0; r < h; ++r) {
    for (int c = 0; c < w * 4; ++c)
      assert(dest[r * 16 + c] == src[r * w * 4 + c]);
    for (int c = w * 4; c < 16; ++c)
      assert(dest[r * 16 + c] == 0xEE);
  }

  std::cout << "ok\n";
  return 0;
}
