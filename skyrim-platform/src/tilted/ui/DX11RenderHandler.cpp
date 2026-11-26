#include "DInputHook.hpp"
#include "TextToDraw.h"
#include <DX11RenderHandler.h>
#include <LoadingScreen.h>
#include <DirectXColors.h>
#include <DirectXTK/DDSTextureLoader.h>
#include <DirectXTK/SimpleMath.h>
#include <DirectXTK/WICTextureLoader.h>
#include <OverlayClient.h>
#include <algorithm>
#include <cmrc/cmrc.hpp>
#include <codecvt>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <iterator>
#include <spdlog/spdlog.h>
#include <string>

CMRC_DECLARE(skyrim_plugin_resources);

namespace CEFUtils {
DX11RenderHandler::DX11RenderHandler(Renderer* apRenderer) noexcept
  : m_pRenderer(apRenderer)
{
  // So we need to lock this until we have the window dimension as a background
  // CEF thread will attempt to get it before we have it
  m_createLock.lock();
  isCreateLock = true;
}

DX11RenderHandler::~DX11RenderHandler() = default;

void DX11RenderHandler::Render(
  const ObtainTextsToDrawFunction& obtainTextsToDraw)
{
  // We need contexts first
  if (!m_pImmediateContext || !m_pContext) {
    Create();

    if (!m_pImmediateContext || !m_pContext)
      return;
  }

  // First of all we flush our deferred context in case we have updated the
  // texture
  {
    std::unique_lock<std::mutex> _(m_textureLock);

    Microsoft::WRL::ComPtr<ID3D11CommandList> pCommandList;
    const auto result = m_pContext->FinishCommandList(FALSE, &pCommandList);

    if (result == S_OK && pCommandList) {
      m_pImmediateContext->ExecuteCommandList(pCommandList.Get(), TRUE);
    }
  }
  GetRenderTargetSize();

  // Thornswood #3130: while the Loading Menu is open nothing of Skyrim's own
  // loading screen reaches the screen; the frame is the page's dark under
  // the page (LoadingScreen.h). The target cleared is the one the page is
  // drawn to and sized from (GetRenderTargetSize), the frame being presented.
  if (LoadingScreen::ShouldCover(Loading())) {
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> pRenderTargetView;
    m_pImmediateContext->OMGetRenderTargets(
      1, pRenderTargetView.ReleaseAndGetAddressOf(), nullptr);
    if (pRenderTargetView) {
      m_pImmediateContext->ClearRenderTargetView(pRenderTargetView.Get(),
                                                 LoadingScreen::kCover);
    }
  }

  m_pSpriteBatch->Begin(DirectX::SpriteSortMode_Deferred,
                        m_pStates->NonPremultiplied());

  if (LoadingScreen::ShouldDraw(Visible(), Loading())) {
    std::unique_lock<std::mutex> _(m_textureLock);

    if (m_pTextureView) {
      m_pSpriteBatch->Draw(m_pTextureView.Get(),
                           DirectX::SimpleMath::Vector2(0.f, 0.f), nullptr,
                           DirectX::Colors::White, 0.f);
    }
  }

  // obtainTextsToDraw is expected to do nothing if IsVisible is set to false
  // NB: this code is active even if browser backend is nirnlab (for now)
  obtainTextsToDraw([&](const TextToDraw& textToDraw) {
    static_assert(
      std::is_same_v<std::decay_t<decltype(textToDraw.string.c_str()[0])>,
                     wchar_t>);

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;

    auto& font = m_pFonts[conv.to_bytes(textToDraw.fontName)];

    if (!font)
      return;

    auto origin = DirectX::SimpleMath::Vector2(
                    font->MeasureString(textToDraw.string.c_str())) /
      2;

    DirectX::XMVECTORF32 color = { static_cast<float>(textToDraw.color[0]),
                                   static_cast<float>(textToDraw.color[1]),
                                   static_cast<float>(textToDraw.color[2]),
                                   static_cast<float>(textToDraw.color[3]) };

    font->DrawString(m_pSpriteBatch.get(), textToDraw.string.c_str(),
                     DirectX::XMFLOAT2(textToDraw.x, textToDraw.y), color,
                     textToDraw.rotation, origin, textToDraw.size,
                     textToDraw.effects, textToDraw.layerDepth);
  });

  bool& focusFlag = CEFUtils::DInputHook::ChromeFocus();

  if (Visible() && focusFlag) {
    if (m_pCursorTexture && m_cursorX >= 0 && m_cursorY >= 0) {
      m_pSpriteBatch->Draw(
        m_pCursorTexture.Get(),
        DirectX::SimpleMath::Vector2(m_cursorX - 24, m_cursorY - 25), nullptr,
        DirectX::Colors::White, 0.f, DirectX::SimpleMath::Vector2(0, 0),
        m_width / 1920.f);
    }
  }

  m_pSpriteBatch->End();
}

void DX11RenderHandler::Reset()
{
  Create();
}

void DX11RenderHandler::Create()
{
  const auto hr = m_pRenderer->GetSwapChain()->GetDevice(
    IID_ID3D11Device,
    reinterpret_cast<void**>(m_pDevice.ReleaseAndGetAddressOf()));

  if (FAILED(hr))
    return;

  m_pDevice->GetImmediateContext(m_pImmediateContext.ReleaseAndGetAddressOf());

  if (!m_pImmediateContext)
    return;

  GetRenderTargetSize();

  if (FAILED(m_pDevice->CreateDeferredContext(
        0, m_pContext.ReleaseAndGetAddressOf())))
    return;

  m_pSpriteBatch =
    std::make_unique<DirectX::SpriteBatch>(m_pImmediateContext.Get());

  m_pStates = std::make_unique<DirectX::CommonStates>(m_pDevice.Get());

  if (FAILED(DirectX::CreateWICTextureFromFile(
        m_pDevice.Get(), m_pParent->GetCursorPathPNG().c_str(), nullptr,
        m_pCursorTexture.ReleaseAndGetAddressOf()))) {
    DirectX::CreateDDSTextureFromFile(
      m_pDevice.Get(), m_pParent->GetCursorPathDDS().c_str(), nullptr,
      m_pCursorTexture.ReleaseAndGetAddressOf());
  }

  cmrc::file file;
  try {
    file = cmrc::skyrim_plugin_resources::get_filesystem().open(
      "assets/cursor.png");
  } catch (std::exception& e) {
    auto dir =
      cmrc::skyrim_plugin_resources::get_filesystem().iterate_directory("");
    std::stringstream ss;
    ss << e.what() << std::endl << std::endl;
    ss << "Root directory contents is: " << std::endl;
    for (auto entry : dir)
      ss << entry.filename() << std::endl;
    throw std::runtime_error(ss.str());
  }

  DirectX::CreateWICTextureFromMemory(
    m_pDevice.Get(), reinterpret_cast<const uint8_t*>(file.begin()),
    file.size(), nullptr, m_pCursorTexture.ReleaseAndGetAddressOf());

  std::unique_lock<std::mutex> _(m_textureLock);

  if (!m_pTexture)
    CreateRenderTexture();

  for (const auto& entry :
       std::filesystem::directory_iterator("Data/Platform/Fonts/")) {
    std::filesystem::path path = entry.path();

    if (path.extension().string() != ".spritefont")
      continue;

    spdlog::info("Font has been added - " + entry.path().stem().string());

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

    auto widestrFontPath =
      converter.from_bytes(static_cast<std::string>(path.string()));

    const wchar_t* fontPath = widestrFontPath.c_str();

    m_pFonts[entry.path().stem().string()] =
      std::make_unique<DirectX::SpriteFont>(m_pDevice.Get(), fontPath);
  }
}

void DX11RenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser,
                                    CefRect& rect)
{
  std::scoped_lock _(m_createLock);

  rect = CefRect(0, 0, m_width, m_height);
}

/*
  Thornswood #6882. A popup, which is how Chromium shows the list of a
  select: the market stall's item and price lists, the drop box's pack, the
  board's destinations, the journal's friends and every other one.

  Drawn offscreen, the page comes to OnPaint as PET_VIEW and an open popup
  comes apart from it, as PET_POPUP, at the place OnPopupSize gave. Only
  PET_VIEW used to be drawn, so a select took the click and opened its list,
  and the list was never put on the screen: on Main the stall's item and
  currency lists "did not open" while its price and count boxes worked.

  So while a popup is open the page's last picture is kept, and every paint
  of either puts the page in the texture with the popup over it, cut to the
  page. CEF already sends the mouse and the keys over the popup's place to
  the popup. Opening and closing one asks CEF for the page again
  (Invalidate), so the picture under the popup is the page as it is, and the
  page as it is goes back when the list closes.
*/
void DX11RenderHandler::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show)
{
  {
    std::unique_lock<std::mutex> _(m_textureLock);
    m_popupShown = show;
    m_popup.clear();
    m_view.clear();
    if (!show)
      m_popupRect = CefRect();
  }
  // Outside the lock: OnPaint takes it, and nothing here needs to hold it
  // while CEF is asked for the page.
  if (browser)
    browser->GetHost()->Invalidate(PET_VIEW);
}

void DX11RenderHandler::OnPopupSize(CefRefPtr<CefBrowser> browser,
                                    const CefRect& rect)
{
  std::unique_lock<std::mutex> _(m_textureLock);
  m_popupRect = rect;
  m_popup.clear();
}

void DX11RenderHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                PaintElementType type,
                                const RectList& dirtyRects, const void* buffer,
                                int width, int height)
{
  const auto bytes = static_cast<const uint8_t*>(buffer);

  if (type == PET_POPUP) {
    bool askForPage = false;
    {
      std::unique_lock<std::mutex> _(m_textureLock);
      if (!m_popupShown || width != m_popupRect.width ||
          height != m_popupRect.height || width <= 0 || height <= 0)
        return;
      m_popup.assign(bytes, bytes + static_cast<size_t>(width) * height * 4);
      if (m_view.size() == static_cast<size_t>(m_width) * m_height * 4)
        Upload(m_view.data());
      else
        askForPage = true; // the page is drawn with it when it comes
    }
    if (askForPage && browser)
      browser->GetHost()->Invalidate(PET_VIEW);
    return;
  }

  std::unique_lock<std::mutex> _(m_textureLock);
  if (type == PET_VIEW && m_width == width && m_height == height) {
    if (m_popupShown)
      m_view.assign(bytes, bytes + static_cast<size_t>(width) * height * 4);
    Upload(bytes);
  }
}

// The page's picture into the texture, and an open popup over it. Called with
// m_textureLock held; view is m_width by m_height, four bytes a pixel.
void DX11RenderHandler::Upload(const uint8_t* view)
{
  if (!m_pTexture) {
    CreateRenderTexture();
  }

  // Under MO2 for some reason, OnPaint called before context initialization
  if (!m_pContext || !m_pTexture) {
    return;
  }

  D3D11_MAPPED_SUBRESOURCE mappedResource;
  const auto result = m_pContext->Map(
    m_pTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

  if (FAILED(result)) {
    // We got no mapping, let's drop the context and reset the texture so
    // that we attempt to create a new one during the next frame
    m_pContext.Reset();
    m_pTexture.Reset();
    m_width = m_height = 0;
    return;
  }

  const auto pDest = static_cast<uint8_t*>(mappedResource.pData);
  const size_t row = static_cast<size_t>(m_width) * 4;
  const size_t pitch = mappedResource.RowPitch;
  for (uint32_t y = 0; y < m_height; ++y)
    std::memcpy(pDest + y * pitch, view + y * row, row);

  const CefRect& r = m_popupRect;
  if (m_popupShown &&
      m_popup.size() == static_cast<size_t>(r.width) * r.height * 4) {
    const int x0 = (std::max)(r.x, 0);
    const int y0 = (std::max)(r.y, 0);
    const int x1 = (std::min)(r.x + r.width, static_cast<int>(m_width));
    const int y1 = (std::min)(r.y + r.height, static_cast<int>(m_height));
    for (int y = y0; y < y1 && x0 < x1; ++y) {
      std::memcpy(pDest + y * pitch + static_cast<size_t>(x0) * 4,
                  m_popup.data() +
                    (static_cast<size_t>(y - r.y) * r.width + (x0 - r.x)) * 4,
                  static_cast<size_t>(x1 - x0) * 4);
    }
  }

  m_pContext->Unmap(m_pTexture.Get(), 0);

  // Thornswood #3129: when the page first has a picture to draw, so the
  // log says how long the game's own menu picture was the only one up.
  static bool firstPictureTold = false;
  if (!firstPictureTold) {
    firstPictureTold = true;
    spdlog::info("browser (tilted): the page drew its first picture "
                 "({}x{}), shown: {}",
                 m_width, m_height, Visible() ? "yes" : "no");
  }
}

void DX11RenderHandler::GetRenderTargetSize()
{
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> pRenderTargetView;

  m_pImmediateContext->OMGetRenderTargets(
    1, pRenderTargetView.ReleaseAndGetAddressOf(), nullptr);
  if (pRenderTargetView) {
    Microsoft::WRL::ComPtr<ID3D11Resource> pSrcResource;
    pRenderTargetView->GetResource(pSrcResource.ReleaseAndGetAddressOf());

    if (pSrcResource) {
      Microsoft::WRL::ComPtr<ID3D11Texture2D> pSrcBuffer;
      pSrcResource.As(&pSrcBuffer);

      D3D11_TEXTURE2D_DESC desc;
      pSrcBuffer->GetDesc(&desc);

      if ((m_width != desc.Width || m_height != desc.Height) && m_pParent) {
        m_width = desc.Width;
        m_height = desc.Height;

        if (isCreateLock) {
          // We now know the size of the viewport, we can let CEF get it
          m_createLock.unlock();
          isCreateLock = false;
        }

        {
          std::unique_lock<std::mutex> _(m_textureLock);

          m_pTexture.Reset();
          m_pTextureView.Reset();
        }

        if (m_pParent->GetBrowser())
          m_pParent->GetBrowser()->GetHost()->WasResized();
      }
    }
  }
}

void DX11RenderHandler::CreateRenderTexture()
{
  D3D11_TEXTURE2D_DESC textDesc;
  textDesc.Width = m_width;
  textDesc.Height = m_height;
  textDesc.MipLevels = textDesc.ArraySize = 1;
  textDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  textDesc.SampleDesc.Count = 1;
  textDesc.SampleDesc.Quality = 0;
  textDesc.Usage = D3D11_USAGE_DYNAMIC;
  textDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  textDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  textDesc.MiscFlags = 0;

  if (FAILED(m_pDevice->CreateTexture2D(&textDesc, nullptr,
                                        m_pTexture.ReleaseAndGetAddressOf())))
    return;

  D3D11_SHADER_RESOURCE_VIEW_DESC sharedResourceViewDesc = {};
  sharedResourceViewDesc.Format = textDesc.Format;
  sharedResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  sharedResourceViewDesc.Texture2D.MipLevels = 1;

  if (FAILED(m_pDevice->CreateShaderResourceView(
        m_pTexture.Get(), &sharedResourceViewDesc,
        m_pTextureView.ReleaseAndGetAddressOf())))
    return;
}
}
