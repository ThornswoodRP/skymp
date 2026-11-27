
// TODO: send event instead of direct dependency on FormView class
import { FormView } from "../../view/formView";
import { QueryKeyCodeBindings } from "../events/queryKeyCodeBindings";

import { ClientListener, CombinedController, Sp } from "./clientListener";
import { BrowserMessageEvent, DxScanCode, Menu, MenuCloseEvent, MenuOpenEvent } from "skyrimPlatform";

const unfocusEventString = `window.dispatchEvent(new CustomEvent('skymp5-client:browserUnfocused', {}))`;
const focusEventString = `window.dispatchEvent(new CustomEvent('skymp5-client:browserFocused', {}))`;

export class BrowserService extends ClientListener {
  constructor(private sp: Sp, private controller: CombinedController) {
    super();

    // THORNSWOOD PATCH (#3129): with gameData.chooseAtMenu the page at the main
    // menu is the character choice, and SkyrimPlatform shows it from the start
    // ([Browser] ShowAtStart), long before this plugin runs. Hiding it here put
    // the game's own menu picture back on screen until thornswood-front.js
    // showed the page again.
    const settingsGameData = this.sp.settings["skymp5-client"]?.["gameData"] as any;
    if (!settingsGameData?.chooseAtMenu) {
      this.sp.browser.setVisible(false);
    }

    this.controller.emitter.on("queryKeyCodeBindings", (e) => this.onQueryKeyCodeBindings(e));
    this.controller.once("update", () => this.onceUpdate());
    this.controller.on("browserMessage", (e) => this.onBrowserMessage(e));
    this.controller.on("menuOpen", (e) => this.onMenuOpen(e));
    this.controller.on("menuClose", (e) => this.onMenuClose(e));
  }

  // TODO: keycodes should be configurable
  private onQueryKeyCodeBindings(e: QueryKeyCodeBindings) {
    if (e.isDown([DxScanCode.F1])) {
      FormView.isDisplayingNicknames = !FormView.isDisplayingNicknames;
    }
    // THORNSWOOD PATCH (#4616): while the character choice is held at the
    // main menu the page is the only thing on screen to choose with, so
    // neither F2, F6 nor Escape hides it or takes its mouse away.
    if (this.choiceHeld()) {
      return;
    }
    if (e.isDown([DxScanCode.F2])) {
      this.sp.browser.setVisible(!this.sp.browser.isVisible());
    }
    if (this.badMenusOpen.size === 0 && e.isDown([DxScanCode.F6])) {
      const newState = !this.sp.browser.isFocused();
      this.sp.browser.setFocused(newState);
      if (newState) {
        this.sp.browser.executeJavaScript(focusEventString);
      } else {
        this.sp.browser.executeJavaScript(unfocusEventString);
      }
    }
    if (this.badMenusOpen.size === 0 && e.isDown([DxScanCode.Enter])) {
      this.sp.browser.setFocused(true);
      this.sp.browser.executeJavaScript(focusEventString);
    }
    if (e.isDown([DxScanCode.Escape])) {
      if (this.sp.browser.isFocused()) {
        this.sp.browser.setFocused(false);
        this.sp.browser.executeJavaScript(unfocusEventString);
      }
    }
  }

  private onceUpdate() {
    this.sp.browser.setVisible(true);
  }

  private onBrowserMessage(e: BrowserMessageEvent) {
    const onFrontLoadedEventKey = "front-loaded";

    if (e.arguments[0] === onFrontLoadedEventKey) {
      this.controller.emitter.emit("browserWindowLoaded", {});
    }
  }

  private onMenuOpen(e: MenuOpenEvent) {
    // THORNSWOOD PATCH (#4616): the main menu is where the character choice
    // is, so while thornswood-front.js holds the choice it is not a menu the
    // page has to get out of the way of. Hiding the page for it left nothing
    // to choose with, and nothing showed it again, because the main menu
    // stays open until the pick loads.
    if (e.name === Menu.Main && this.choiceHeld()) {
      return;
    }
    if (this.isBadMenu(e.name)) {
      this.sp.browser.setVisible(false);
      this.badMenusOpen.add(e.name);
    } else if (e.name === Menu.HUD) {
      this.sp.browser.setVisible(true);
    }
  }

  private onMenuClose(e: MenuCloseEvent) {
    if (this.badMenusOpen.delete(e.name)) {
      if (this.badMenusOpen.size === 0) {
        this.sp.browser.setVisible(true);
        // THORNSWOOD PATCH (#4616): hiding the page took its focus with it
        // (NirnLab drops the real focus on a hide), and the choice is chosen
        // with the mouse, so it gets the mouse back with the page.
        if (this.choiceHeld()) {
          this.sp.browser.setFocused(true);
          this.sp.browser.executeJavaScript(focusEventString);
        }
      }
    }

    // THORNSWOOD PATCH (#4616): the HUD closes on the way back to the main
    // menu, where the character choice is coming back up.
    if (e.name === Menu.HUD && !this.choiceHeld()) {
      this.sp.browser.setVisible(false);
    }
  }

  // THORNSWOOD PATCH (#4616): thornswood-front.js says on globalThis whether
  // it holds the character choice at the main menu: true from the moment the
  // choice is due (the start, or a session ended back to the menu) until a
  // pick goes in or the world is up. SkyrimPlatform's plugins share one
  // globalThis, the way __thornswoodLoadRefused and __thornswoodInWorld go.
  private choiceHeld(): boolean {
    return (globalThis as any).__thornswoodChoiceHeld === true;
  }

  private isBadMenu(menu: string) {
    return this.badMenus.includes(menu as Menu);
  }

  private badMenusOpen = new Set<string>();

  private readonly badMenus: Menu[] = [
    Menu.Barter,
    Menu.Book,
    Menu.Container,
    Menu.Crafting,
    Menu.Gift,
    Menu.Inventory,
    Menu.Journal,
    Menu.Lockpicking,
    // THORNSWOOD #3130. Not Menu.Loading: Thornswood's page covers a load
    // with its own loading screen, and SkyrimPlatform draws the page while
    // the Loading Menu is open (LoadingScreen.h). This hid the browser for
    // it from the first update after the load, and showed it again on the
    // close, which came up to 90 ms later on dev (22 Oct 2026): the page
    // blinked out as the load ended.
    Menu.Map,
    Menu.RaceSex,
    Menu.Stats,
    Menu.Tween,
    Menu.Console,
    Menu.Main,
  ];
}
