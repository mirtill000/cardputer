#pragma once

#include "Screen.h"

// "DATASTORE SWEEP": runs DataStoreProbe over the alive-host list and
// lists exposed Redis/Memcached/Elasticsearch/MongoDB instances, flagging
// the ones reachable without authentication. See scan/DataStoreProbe.h —
// read-only detection.
class DataStoreScreen : public Screen {
public:
    static DataStoreScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    void drawFindings(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
};
