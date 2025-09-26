#include "config.h"

#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include "xf86.h"
#include "xf86_OSproc.h"
#include "compiler.h"
#include "xf86Pci.h"
#include "mipointer.h"
#include "mipointrst.h"
#include "micmap.h"

#include "fb.h"
#include "edid.h"
#include "xf86i2c.h"
#include "xf86Crtc.h"
#include "miscstruct.h"
#include "dixstruct.h"
#include "xf86xv.h"
#include "xf86Module.h"

#include "loongson_options.h"
#include "loongson_shadow.h"
#include "driver.h"

Bool LS_ShadowAllocFB(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);
    unsigned int bit2byte = (pScrn->bitsPerPixel + 7) >> 3;

    ms->drmmode.shadow_fb = calloc(1,
            pScrn->displayWidth * pScrn->virtualY * bit2byte);

    if ( ms->drmmode.shadow_fb == NULL )
    {
        return FALSE;
    }

    return TRUE;
}

void LS_ShadowFreeFB(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);

    free(ms->drmmode.shadow_fb);
    ms->drmmode.shadow_fb = NULL;
}


Bool LS_ShadowAllocDoubleFB(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);

    unsigned int bit2byte = (pScrn->bitsPerPixel + 7) >> 3;


    ms->drmmode.shadow_fb2 = calloc(1,
            pScrn->displayWidth * pScrn->virtualY * bit2byte);

    if (NULL == ms->drmmode.shadow_fb2)
    {
        return FALSE;
    }

    return TRUE;
}

void LS_ShadowFreeDoubleFB(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);

    free(ms->drmmode.shadow_fb2);
    ms->drmmode.shadow_fb2 = NULL;
}


static Bool LS_ShadowShouldDouble(ScrnInfoPtr pScrn, modesettingPtr ms)
{
    Bool ret = FALSE, asked;
    int from;
    drmVersionPtr v = drmGetVersion(ms->fd);

    if (!strcmp(v->name, "mgag200") ||
        !strcmp(v->name, "ast")) /* XXX || rn50 */
    {
        ret = TRUE;
    }

    drmFreeVersion(v);

    asked = xf86GetOptValBool(ms->drmmode.Options, OPTION_DOUBLE_SHADOW, &ret);

    if (asked)
        from = X_CONFIG;
    else
        from = X_INFO;

    xf86DrvMsg(pScrn->scrnIndex, from,
               "Double-buffered shadow updates: %s\n",
               ret ? "on" : "off");

    return ret;
}



void LS_TryEnableShadow(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);

    Bool prefer_shadow = TRUE;

    uint64_t value = 0;
    int ret;

    if (ms->drmmode.force_24_32)
    {
        prefer_shadow = TRUE;

        ms->drmmode.shadow_enable = TRUE;
    }
    else
    {
        ret = drmGetCap(ms->fd, DRM_CAP_DUMB_PREFER_SHADOW, &value);
        if (ret == 0)
        {
            prefer_shadow = !!value;
        }

        ms->drmmode.shadow_enable = xf86ReturnOptValBool(
                ms->drmmode.Options, OPTION_SHADOW_FB, prefer_shadow);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "ShadowFB: preferred %s, enabled %s\n",
            prefer_shadow ? "YES" : "NO",
            ms->drmmode.force_24_32 ? "FORCE" :
            ms->drmmode.shadow_enable ? "YES" : "NO");

    ms->drmmode.shadow_enable2 = ms->drmmode.shadow_enable ?
        LS_ShadowShouldDouble(pScrn, ms) : FALSE;
}


void * LS_ShadowWindow(ScreenPtr pScreen, CARD32 row, CARD32 offset,
        int mode, CARD32 *size, void *closure)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(pScrn);
    int stride = (pScrn->displayWidth * ms->drmmode.kbpp) / 8;
    *size = stride;

    return ((uint8_t *) ms->drmmode.front_bo.dumb->ptr + row * stride + offset);
}


static Bool msUpdateIntersect(modesettingPtr ms,
        shadowBufPtr pBuf, BoxPtr box, xRectangle *prect)
{
    int i;
    Bool dirty = FALSE;
    const unsigned int stride = pBuf->pPixmap->devKind;
    const unsigned int cpp = ms->drmmode.cpp;
    const unsigned int width = (box->x2 - box->x1) * cpp;

    unsigned char * old = ms->drmmode.shadow_fb2;
    unsigned char * new = ms->drmmode.shadow_fb;

    const unsigned int num_lines = box->y2 - box->y1;

    unsigned int go_to_start = (box->y1 * stride) + (box->x1 * cpp);

    old += go_to_start;
    new += go_to_start;

    for (i = 0; i < num_lines; ++i)
    {
        // unsigned char *o = old + i * stride;
        // unsigned char *n = new + i * stride;
        if (memcmp(old, new, width) != 0)
        {
            dirty = TRUE;
            memcpy(old, new, width);
        }

        old += stride;
        new += stride;
    }

    if (dirty)
    {
        prect->x = box->x1;
        prect->y = box->y1;
        prect->width = box->x2 - box->x1;
        prect->height = box->y2 - box->y1;
    }

    return dirty;
}


void LS_ShadowUpdatePacked(ScreenPtr pScreen, shadowBufPtr pBuf)
{
/* somewhat arbitrary tile size, in pixels */
#define TILE 16

    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(pScrn);
    Bool use_3224 = ms->drmmode.force_24_32 && (pScrn->bitsPerPixel == 32);

    if (ms->drmmode.shadow_enable2 && ms->drmmode.shadow_fb2)
    {
        do {
            RegionPtr damage = DamageRegion(pBuf->pDamage), tiles;
            BoxPtr extents = RegionExtents(damage);
            xRectangle *prect;
            int nrects;
            int i, j, tx1, tx2, ty1, ty2;

            tx1 = extents->x1 / TILE;
            tx2 = (extents->x2 + TILE - 1) / TILE;
            ty1 = extents->y1 / TILE;
            ty2 = (extents->y2 + TILE - 1) / TILE;

            nrects = (tx2 - tx1) * (ty2 - ty1);
            if (!(prect = calloc(nrects, sizeof(xRectangle))))
                break;

            nrects = 0;
            for (j = ty2 - 1; j >= ty1; j--) {
                for (i = tx2 - 1; i >= tx1; i--) {
                    BoxRec box;

                    box.x1 = max(i * TILE, extents->x1);
                    box.y1 = max(j * TILE, extents->y1);
                    box.x2 = min((i+1) * TILE, extents->x2);
                    box.y2 = min((j+1) * TILE, extents->y2);

                    if (RegionContainsRect(damage, &box) != rgnOUT)
                    {
                        if (msUpdateIntersect(ms, pBuf, &box, prect + nrects))
                        {
                            nrects++;
                        }
                    }
                }
            }

            tiles = RegionFromRects(nrects, prect, CT_NONE);
            RegionIntersect(damage, damage, tiles);
            RegionDestroy(tiles);
            free(prect);
        } while (0);
    }

    if (use_3224)
        ms->shadow.Update32to24(pScreen, pBuf);
    else
        ms->shadow.UpdatePacked(pScreen, pBuf);

#undef TILE
}



Bool LS_ShadowLoadAPI(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);
    void* mod = xf86LoadSubModule(pScrn, "shadow");
    if (NULL == mod)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                "shadow loaded failed.\n");
        return FALSE;
    }

    // suijingfeng: LoaderSymbolFromModule is not get exported
    // This is embarassing.
    ms->shadow.Setup        = LoaderSymbol("shadowSetup");
    ms->shadow.Add          = LoaderSymbol("shadowAdd");
    ms->shadow.Remove       = LoaderSymbol("shadowRemove");
    ms->shadow.Update32to24 = LoaderSymbol("shadowUpdate32to24");
    ms->shadow.UpdatePacked = LoaderSymbol("shadowUpdatePacked");

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "shadow's symbols loaded.\n");

    return TRUE;
}
