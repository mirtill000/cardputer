#pragma once

#include "Screen.h"

// Shared gate in front of every "active offensive" tool (ARP spoof/MITM
// audit, deauth+handshake capture, evil twin) — one level stronger than
// CredDisclaimerScreen's single "Y" keypress, because these techniques
// affect a THIRD-PARTY device's connectivity/traffic, not just a
// service already discovered on a host the user pointed this thing at.
// Requires typing the word AUTHORIZED in full before Enter does
// anything; accepting sets AppConfig::offensiveEnabled for the rest of
// this session (shared by all three tools, same "ask once per boot"
// pattern as credAuditEnabled) and pushes whatever screen was set as
// the pending target.
class OffensiveDisclaimerScreen : public Screen {
public:
    static OffensiveDisclaimerScreen& instance();

    void setPendingTargetScreen(Screen* s) { _pendingTarget = s; }

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

private:
    Screen* _pendingTarget = nullptr;
    String _typed;
};
