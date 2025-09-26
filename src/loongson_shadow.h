#ifndef LOONGSON_SHADOW_H_
#define LOONGSON_SHADOW_H_

#include "shadow.h"

Bool LS_ShadowLoadAPI(ScrnInfoPtr pScrn);

Bool LS_ShadowAllocFB(ScrnInfoPtr pScrn);
void LS_ShadowFreeFB(ScrnInfoPtr pScrn);


Bool LS_ShadowAllocDoubleFB(ScrnInfoPtr pScrn);
void LS_ShadowFreeDoubleFB(ScrnInfoPtr pScrn);
// Bool LS_ShadowShouldDouble(ScrnInfoPtr pScrn, modesettingPtr ms);


void LS_TryEnableShadow(ScrnInfoPtr pScrn);


void * LS_ShadowWindow(ScreenPtr pScreen, CARD32 row, CARD32 offset,
        int mode, CARD32 *size, void *closure);

void LS_ShadowUpdatePacked(ScreenPtr pScreen, shadowBufPtr pBuf);

#endif
