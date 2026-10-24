#include "TextWrap.h"
#include <cstring>

// Greedy word-wrap, hardened against adversarial input (SSIDs, banners,
// URLs, base64 blobs can all arrive here from the network):
//
//  - A1: a single "word" longer than the line buffer used to overflow
//    line[] via strcat() because the flush guard only fired when the
//    line was already non-empty. Now long words are hard-split to fit
//    the line buffer, and every append is bounds-checked, so no write
//    can ever exceed the buffers regardless of input length.
//  - A2: when the source text is longer than the local copy buffer it is
//    truncated with a trailing "…" marker instead of silently vanishing.
//  - A6: uses strtok_r (reentrant) rather than strtok's hidden global
//    state, so the function is safe even if ever called off the UI task.
void drawWrapped(M5Canvas& gfx, const char* text, int16_t x, int16_t y, int16_t lineH, uint8_t maxChars) {
    if (!text) return;

    char buf[320];
    size_t srcLen = strlen(text);
    if (srcLen < sizeof(buf)) {
        memcpy(buf, text, srcLen + 1);
    } else {
        // A2: keep room for a 3-byte "…" (UTF-8) + NUL so the user can
        // see the text was cut rather than assuming it just ended.
        size_t keep = sizeof(buf) - 1 - 3;
        memcpy(buf, text, keep);
        buf[keep] = '\0';
        strncat(buf, "\xE2\x80\xA6", sizeof(buf) - strlen(buf) - 1);
    }

    // The per-line buffer never needs to hold more than maxChars glyphs,
    // and maxChars is a uint8_t so it fits comfortably in 256+1.
    char line[257];
    line[0] = '\0';
    size_t lineLen = 0;
    int16_t cy = y;

    auto flush = [&]() {
        if (lineLen == 0) return;
        gfx.setCursor(x, cy);
        gfx.print(line);
        cy += lineH;
        line[0] = '\0';
        lineLen = 0;
    };

    // Clamp the effective wrap width to what the line buffer can hold, so
    // appends below can never be asked to exceed it even if a caller
    // passes a maxChars at the top of the uint8_t range.
    size_t wrapAt = maxChars;
    if (wrapAt > sizeof(line) - 1) wrapAt = sizeof(line) - 1;
    if (wrapAt == 0) wrapAt = 1;

    char* save = nullptr;
    char* word = strtok_r(buf, " ", &save);
    while (word) {
        size_t wordLen = strlen(word);

        // A1: a word that can't fit on a line by itself is hard-split
        // across as many lines as it takes, filling each line up to the
        // wrap width. Printable, and most importantly bounded.
        while (wordLen > wrapAt) {
            flush();  // start the oversized word on a fresh line
            memcpy(line, word, wrapAt);
            line[wrapAt] = '\0';
            lineLen = wrapAt;
            flush();
            word += wrapAt;
            wordLen -= wrapAt;
        }

        size_t needed = lineLen + (lineLen ? 1 : 0) + wordLen;
        if (needed > wrapAt && lineLen > 0) flush();

        if (lineLen) {
            line[lineLen++] = ' ';
        }
        memcpy(line + lineLen, word, wordLen);
        lineLen += wordLen;
        line[lineLen] = '\0';

        word = strtok_r(nullptr, " ", &save);
    }
    flush();
}
