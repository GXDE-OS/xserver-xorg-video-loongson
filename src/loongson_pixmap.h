#ifndef LOONGSON_PIXMAP_H_
#define LOONGSON_PIXMAP_H_


#define CREATE_PIXMAP_USAGE_SCANOUT 0x80000000

struct ms_exa_pixmap_priv {
    struct dumb_bo *bo;
    int fd;
    int pitch;
    Bool owned;
    struct LoongsonBuf buf;
    int usage_hint;
};


Bool LS_IsDumbPixmap( int usage_hint );

void * LS_CreateExaPixmap(ScreenPtr pScreen,
        int width, int height, int depth,
        int usage_hint, int bitsPerPixel,
        int *new_fb_pitch);

void LS_DestroyExaPixmap(ScreenPtr pScreen, void *driverPriv);


void * LS_CreateDumbPixmap(ScreenPtr pScreen,
        int width, int height, int depth,
        int usage_hint, int bitsPerPixel,
        int *new_fb_pitch );

void LS_DestroyDumbPixmap(ScreenPtr pScreen, void *driverPriv);

/*

Bool LS_ModifyDumbPixmapHeader( PixmapPtr pPixmap,
        int width, int height, int depth, int bitsPerPixel,
        int devKind, pointer pPixData );

Bool LS_ModifyExaPixmapHeader( PixmapPtr pPixmap,
        int width, int height, int depth,
        int bitsPerPixel, int devKind, pointer pPixData );

*/

#endif
