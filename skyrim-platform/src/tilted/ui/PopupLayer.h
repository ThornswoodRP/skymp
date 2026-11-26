#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Thornswood #6882: the list a page's <select> opens is drawn.
//
// The browser is windowless: Chromium paints the page into a buffer and
// DX11RenderHandler copies it onto the game's frame. A <select>'s list is
// not part of the page's picture. Chromium makes it a widget of its own,
// says where it is (OnPopupShow, OnPopupSize) and paints it separately, as
// OnPaint with PET_POPUP. DX11RenderHandler took only PET_VIEW, so a click on
// a select opened a list nobody could see: at a market stall on main on
// 26 Nov the item and currency lists "did not open" while the number boxes
// beside them worked. Every select in the game's pages (the stall, the drop
// box, the notice board, the journal, the merchant and the ship) was the
// same.
//
// PopupLayer is what the handler keeps of that widget: whether it is up,
// where it is in the page, and whether a picture of that size is the one to
// draw there. Chromium hit-tests the list at the place it gave, so the list
// is drawn exactly there, unmoved; a mouse click on it then reaches it the
// way a click on the page does.
struct PopupLayer
{
  bool shown{ false };
  int x{ 0 };
  int y{ 0 };
  int width{ 0 };
  int height{ 0 };

  // OnPopupShow. Hidden means not drawn; a list shown again is placed again
  // by the OnPopupSize that follows.
  void Show(bool show)
  {
    shown = show;
    if (!show) {
      x = y = width = height = 0;
    }
  }

  // OnPopupSize: where the list is, in the page's own pixels.
  void Size(int aX, int aY, int aWidth, int aHeight)
  {
    x = aX;
    y = aY;
    width = aWidth > 0 ? aWidth : 0;
    height = aHeight > 0 ? aHeight : 0;
  }

  // A PET_POPUP picture is taken when the list is up and the picture is its
  // size. One of another size is from before the list was moved or resized,
  // and the next paint brings the right one.
  bool Takes(int paintWidth, int paintHeight) const
  {
    return shown && width > 0 && height > 0 && paintWidth == width &&
      paintHeight == height;
  }

  // Drawn while it is up and has a size.
  bool ShouldDraw() const { return shown && width > 0 && height > 0; }

  // Copies a picture Chromium painted (width * 4 bytes a row, no gaps) into
  // a mapped texture whose rows are destPitch bytes apart, which can be more
  // than width * 4.
  static void CopyRows(uint8_t* dest, std::size_t destPitch, const void* src,
                       int paintWidth, int paintHeight)
  {
    const auto* from = static_cast<const uint8_t*>(src);
    const std::size_t row = static_cast<std::size_t>(paintWidth) * 4;
    for (int i = 0; i < paintHeight; ++i) {
      std::memcpy(dest + static_cast<std::size_t>(i) * destPitch,
                  from + static_cast<std::size_t>(i) * row, row);
    }
  }
};
