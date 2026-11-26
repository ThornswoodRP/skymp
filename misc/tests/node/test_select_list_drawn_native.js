'use strict';
// Thornswood #6882. The list a page's <select> opens is drawn. The game's
// browser is windowless: Chromium paints a select's list apart from the page
// (OnPaint with PET_POPUP, placed by OnPopupShow and OnPopupSize), and
// DX11RenderHandler drew only the page, so at a market stall on main on
// 26 Nov the item and currency lists opened unseen. Builds
// test_select_list_drawn_native.cpp against the real PopupLayer.h, and reads
// DX11RenderHandler, which builds only on Windows, for where it is used.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), cp = require('node:child_process');
const root = path.join(__dirname, '../../..');
const headers = path.join(root, 'skyrim-platform/src/tilted/ui');
if (!fs.existsSync(path.join(headers, 'PopupLayer.h'))) throw Error('PopupLayer.h missing');

let failed = 0;
function check(what, ok) {
  console.log((ok ? 'ok   ' : 'FAIL ') + what);
  if (!ok) failed++;
}

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-native-select-'));
try {
  const fixture = path.join(__dirname, 'test_select_list_drawn_native.cpp');
  const exe = path.join(work, process.platform === 'win32' ? 'select.exe' : 'select');
  const {command: compiler, env} = require('./cxx').compiler();
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/I' + headers, '/Fe:' + exe, '/Fo:' + path.join(work, 'select.obj'), fixture]
    : ['-std=c++17', '-Wall', '-Wextra', '-I' + headers, fixture, '-o', exe];
  const build = cp.spawnSync(compiler, args, {encoding: 'utf8', cwd: work, env});
  process.stdout.write(build.stdout || ''); process.stderr.write(build.stderr || '');
  if (build.error) throw build.error;
  check('the fixture builds', build.status === 0);
  if (build.status === 0) {
    const run = cp.spawnSync(exe, [], {encoding: 'utf8'});
    process.stderr.write(run.stderr || '');
    check('PopupLayer is shown, placed, takes only a picture its size, and copies rows at the pitch',
      run.status === 0 && String(run.stdout).trim() === 'ok');
  }
} finally {
  const parent = fs.realpathSync(os.tmpdir());
  const target = fs.realpathSync(work);
  if (path.dirname(target).toLowerCase() !== parent.toLowerCase() || !path.basename(target).startsWith('thornswood-native-select-')) throw Error('Unsafe test cleanup target');
  fs.rmSync(target, {recursive: true, force: true});
}

const read = (p) => fs.readFileSync(path.join(root, p), 'utf8');
const head = read('skyrim-platform/src/tilted/ui/DX11RenderHandler.h');
const render = read('skyrim-platform/src/tilted/ui/DX11RenderHandler.cpp');

check('the handler overrides OnPopupShow and OnPopupSize, and keeps a PopupLayer',
  /void OnPopupShow\(CefRefPtr<CefBrowser> browser, bool show\) override;/.test(head) &&
  /void OnPopupSize\(CefRefPtr<CefBrowser> browser,\s*const CefRect& rect\) override;/.test(head) &&
  /PopupLayer m_popup;/.test(head));
check('OnPopupShow and OnPopupSize set it under the texture lock',
  /void DX11RenderHandler::OnPopupShow\([^)]*\)\s*\{\s*std::unique_lock<std::mutex> _\(m_textureLock\);\s*m_popup\.Show\(show\);\s*\}/.test(render) &&
  /void DX11RenderHandler::OnPopupSize\([^)]*\)\s*\{\s*std::unique_lock<std::mutex> _\(m_textureLock\);\s*m_popup\.Size\(rect\.x, rect\.y, rect\.width, rect\.height\);\s*\}/.test(render));
{
  const paint = render.indexOf('void DX11RenderHandler::OnPaint(');
  const popup = render.indexOf('if (type == PET_POPUP) {', paint);
  const view = render.indexOf('if (type == PET_VIEW', paint);
  check('OnPaint hands a PET_POPUP picture to PaintPopup before the page is taken',
    paint > 0 && popup > paint && view > popup &&
    /if \(type == PET_POPUP\) \{\s*PaintPopup\(buffer, width, height\);\s*return;\s*\}/.test(render.slice(popup, view)));
}
check('PaintPopup takes only a picture m_popup.Takes, and copies it with CopyRows at the RowPitch',
  /void DX11RenderHandler::PaintPopup\([\s\S]*?std::unique_lock<std::mutex> _\(m_textureLock\);[\s\S]*?m_popup\.Takes\(width, height\)[\s\S]*?PopupLayer::CopyRows\(static_cast<uint8_t\*>\(mapped\.pData\),\s*mapped\.RowPitch, buffer, width, height\);/.test(render));
{
  const page = render.indexOf('m_pSpriteBatch->Draw(m_pTextureView.Get(),');
  const list = render.indexOf('m_pPopupTextureView.Get(),');
  const end = render.indexOf('m_pSpriteBatch->End();');
  check('Render draws the list over the page, at m_popup.x and m_popup.y, before End',
    page > 0 && list > page && end > list &&
    /if \(m_popup\.ShouldDraw\(\) && m_pPopupTextureView\) \{/.test(render.slice(page, list)) &&
    /static_cast<float>\(m_popup\.x\),\s*static_cast<float>\(m_popup\.y\)/.test(render.slice(list, end)));
}

if (failed) { console.log(failed + ' failed'); process.exitCode = 1; }
else console.log('all passed');
