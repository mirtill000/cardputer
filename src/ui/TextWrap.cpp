#include "TextWrap.h"
#include <cstring>

void drawWrapped(M5Canvas& gfx, const char* text, int16_t x, int16_t y, int16_t lineH, uint8_t maxChars) {
    char buf[320];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* word = strtok(buf, " ");
    char line[64] = "";
    int16_t cy = y;

    while (word) {
        size_t lineLen = strlen(line);
        size_t wordLen = strlen(word);
        size_t needed = lineLen + (lineLen ? 1 : 0) + wordLen;

        if (needed > maxChars && lineLen > 0) {
            gfx.setCursor(x, cy);
            gfx.print(line);
            cy += lineH;
            line[0] = '\0';
            lineLen = 0;
        }
        if (lineLen) strcat(line, " ");
        strcat(line, word);
        word = strtok(nullptr, " ");
    }
    if (line[0]) {
        gfx.setCursor(x, cy);
        gfx.print(line);
    }
}
