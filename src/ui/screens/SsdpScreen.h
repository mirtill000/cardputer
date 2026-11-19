#pragma once

#include "ListScreen.h"

// "UPNP DISCOVERY": drives SsdpDiscovery's one-shot M-SEARCH sweep and
// lists whatever answers (smart TVs, NAS, routers, IoT gear).
//
// First screen migrated onto the shared ListScreen base (selection,
// windowing, scroll markers, Up/Down/Back and the 'I' detail overlay all
// come from there); it only describes its rows. Template for migrating
// the other ~30 list screens one at a time.
class SsdpScreen : public ListScreen {
public:
    static SsdpScreen& instance();

    void onEnter() override;
    void onScanEvent(const ScanNotification& ev) override;

    const char* title() const override { return "UPNP"; }
    const char* helpText() const override {
        return "UPNP DISCOVERY\nMENU>NET>D>Ent(UPNP)\nOne M-SEARCH multicast -\nlists whatever answers (TVs,\nNAS, routers, IoT).\nENTER: sweep   I: full detail\nArrows: move   DEL: back";
    }

protected:
    size_t itemCount() const override;
    void drawItem(M5Canvas& gfx, size_t index, int16_t y, int16_t rowH, bool selected) override;
    const char* listHeader() const override { return "UPNP DISCOVERY"; }
    const char* listFooter() const override;

    int16_t listTop() const override { return 32; }
    int16_t rowHeight() const override { return 20; }  // two text lines per device
    size_t maxRows() const override { return 4; }
    void drawAboveList(M5Canvas& gfx) override;
    String detailText(size_t index) const override;
    const char* detailTitle() const override { return _detailTitle.c_str(); }
    void onListKey(UiKey key, char ch) override;

private:
    mutable String _detailTitle;  // the selected device's IP, set by detailText()
};
