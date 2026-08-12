#pragma once

#include <M5GFX.h>
#include "../InputManager.h"
#include "../../core/EventQueue.h"

// Base interface for every screen in the UI stack. UiManager owns a
// stack of Screen* (push/pop/replace) and drives whichever one is on
// top; screens are long-lived singletons (created once, reused across
// navigations) rather than heap-churned per visit.
class Screen {
public:
    virtual ~Screen() = default;

    virtual void onEnter() {}
    virtual void onExit() {}

    virtual void onKey(UiKey key, char ch) { (void)key; (void)ch; }

    // Most screens don't care about live scan results; only the
    // dashboard/detail screens override this.
    virtual void onScanEvent(const ScanNotification& ev) { (void)ev; }

    // Per-frame logic (animations, blinking cursors) with no drawing.
    virtual void update(uint32_t nowMs) { (void)nowMs; }

    virtual void draw(M5Canvas& gfx) = 0;

    // Optional metadata used by UiManager/chrome, both defaulting to
    // nullptr so no existing screen has to change:
    //  - title(): a SHORT name (a few chars) used as the breadcrumb prefix
    //    when this screen is the parent of another (see chrome::drawHeader).
    //  - helpText(): '\n'-separated lines shown by the global '?' help
    //    overlay (see UiManager). nullptr => a generic help panel.
    virtual const char* title() const { return nullptr; }
    virtual const char* helpText() const { return nullptr; }
};
