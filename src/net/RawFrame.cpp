#include "RawFrame.h"

extern "C" {
#include "lwip/netif.h"
#include "lwip/pbuf.h"
}

bool RawFrame::send(const uint8_t* data, size_t len) {
    struct netif* lwipNetif = netif_default;
    if (!lwipNetif || !lwipNetif->linkoutput) return false;

    struct pbuf* p = pbuf_alloc(PBUF_RAW, (uint16_t)len, PBUF_RAM);
    if (!p) return false;
    pbuf_take(p, data, (uint16_t)len);

    err_t err = lwipNetif->linkoutput(lwipNetif, p);
    pbuf_free(p);
    return err == ERR_OK;
}
