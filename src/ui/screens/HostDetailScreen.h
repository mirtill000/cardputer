#pragma once

#include "Screen.h"
#include <cstddef>

// Full detail view for a single discovered host. Reads live from
// ScanManager on every draw (rather than caching a snapshot), so port
// scan / credential audit results (added in later phases) show up here
// automatically as soon as those modules populate them for this host.
class HostDetailScreen : public Screen {
public:
    static HostDetailScreen& instance();

    void setHostIndex(size_t idx) { _hostIndex = idx; }

    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "HOST"; }
    const char* helpText() const override {
        return "HOST DETAIL\nMENU>NET>Ent(HOST)\nTAB:port scan  C:cred audit\nV:service audit (per-svc)\nH:http brute (if http)\nM:mitm audit\nS:smb negotiate (if smb)\nDEL:back";
    }

private:
    size_t _hostIndex = 0;
};
