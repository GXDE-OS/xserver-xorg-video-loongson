/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright 2011 Dave Airlie
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 * Original Author: Alan Hourihane <alanh@tungstengraphics.com>
 * Rewrite: Dave Airlie <airlied@redhat.com>
 *
 */
#include "config.h"

#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include <xf86.h>
#include <xf86_OSproc.h>
#include "compiler.h"
#include <xf86Pci.h>
#include "mipointer.h"
#include "mipointrst.h"
#include "micmap.h"

#include "fb.h"
#include "edid.h"
#include "xf86i2c.h"
#include "xf86Crtc.h"
#include "miscstruct.h"
#include "dixstruct.h"

#include <X11/extensions/randr.h>

// #include "xf86xv.h"
// #include <X11/extensions/Xv.h>

#ifdef XSERVER_PLATFORM_BUS
#include "xf86platformBus.h"
#endif
#ifdef XSERVER_LIBPCIACCESS
#include <pciaccess.h>
#endif
#include "driver.h"

#include "fake_exa.h"

#include "loongson_options.h"
#include "loongson_debug.h"
#include "loongson_helpers.h"
#include "loongson_cursor.h"
#include "loongson_shadow.h"
#include "loongson_entity.h"

#include "loongson_glamor.h"

static Bool PreInit(ScrnInfoPtr pScrn, int flags);
static Bool ScreenInit(ScreenPtr pScreen, int argc, char **argv);

static void FreeScreen(ScrnInfoPtr pScrn);
static Bool CloseScreen(ScreenPtr pScreen);

// The server takes control of the console.
static Bool EnterVT(ScrnInfoPtr pScrn);
// The server releases control of the console.
static void LeaveVT(ScrnInfoPtr pScrn);

static ModeStatus ValidMode(ScrnInfoPtr pScrn, DisplayModePtr mode,
                            Bool verbose, int flags);
static Bool SwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode);
static void AdjustFrame(ScrnInfoPtr pScrn, int x, int y);


//
// A driver and any module it uses may allocate per-screen
// private storage in either the ScreenRec (DIX level) or
// ScrnInfoRec (XFree86 common layer level).
//
// ScreenRec storage persists only for a single server generation,
// and ScrnInfoRec storage persists across generations for the
// life time of the server.
//
// The ScreenRec devPrivates data must be reallocated/initialised
// at the start of each new generation.
//
// This is normally done from the ScreenInit() function,
// and Init functions for other modules that it calls.
// Data allocated in this way should be freed by the driver’s
// CloseScreen() functions, and Close functions for other modules
// that it calls.
//
// A new devPrivates entry is allocated by calling the
// AllocateScreenPrivateIndex() function.


void LS_SetupScrnHooks(ScrnInfoPtr scrn, Bool (* pFnProbe)(DriverPtr, int))
{
    scrn->driverVersion = 1;
    scrn->driverName = "loongson";
    scrn->name = "loongson";

    scrn->Probe = pFnProbe;
    scrn->PreInit = PreInit;
    scrn->ScreenInit = ScreenInit;
    scrn->SwitchMode = SwitchMode;
    scrn->AdjustFrame = AdjustFrame;
    scrn->EnterVT = EnterVT;
    scrn->LeaveVT = LeaveVT;
    scrn->FreeScreen = FreeScreen;
    scrn->ValidMode = ValidMode;
}



static Bool LS_AllocDriverPrivate(ScrnInfoPtr pScrn)
{
    //
    // Per-screen driver specific data that cannot be accommodated
    // with the static ScrnInfoRec fields is held in a driver-defined
    // data structure, a pointer to which is assigned to the
    // ScrnInfoRec’s driverPrivate field.
    //
    // Driver specific information should be stored in a structure
    // hooked into the ScrnInfoRec’s driverPrivate field.
    //
    // Any other modules which require persistent data (ie data that
    // persists across server generations) should be initialised in
    // this function, and they should allocate a "privates" index
    // to hook their data into. The "privates" data is persistent.
    //
    if (NULL == pScrn->driverPrivate)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "GetRec: Allocate for driver private.\n");
        //suijingfeng: void *calloc(size_t nmemb, size_t size);
        pScrn->driverPrivate = xnfcalloc(1, sizeof(modesettingRec));
        if (NULL == pScrn->driverPrivate)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "GetRec: Failed allocate for driver private.\n");
            return FALSE;
        }
    }

    return TRUE;
}


static int dispatch_dirty_region(ScrnInfoPtr scrn,
                      PixmapPtr pixmap, DamagePtr damage, int fb_id)
{
    modesettingPtr ms = modesettingPTR(scrn);
    RegionPtr dirty = DamageRegion(damage);
    unsigned int num_cliprects = REGION_NUM_RECTS(dirty);
    int ret = 0;

    if (num_cliprects)
    {
        drmModeClip *clip = xallocarray(num_cliprects, sizeof(drmModeClip));
        BoxPtr rect = REGION_RECTS(dirty);
        unsigned int i;

        if (!clip)
            return -ENOMEM;

        /* XXX no need for copy? */
        for (i = 0; i < num_cliprects; i++, rect++)
        {
            clip[i].x1 = rect->x1;
            clip[i].y1 = rect->y1;
            clip[i].x2 = rect->x2;
            clip[i].y2 = rect->y2;
        }

        /* TODO query connector property to see if this is needed */
        ret = drmModeDirtyFB(ms->fd, fb_id, clip, num_cliprects);

        /* if we're swamping it with work, try one at a time */
        if (ret == -EINVAL)
        {
            for (i = 0; i < num_cliprects; i++)
            {
                if ((ret = drmModeDirtyFB(ms->fd, fb_id, &clip[i], 1)) < 0)
                    break;
            }
        }

        free(clip);
        DamageEmpty(damage);
    }
    return ret;
}


/* OUTPUT SLAVE SUPPORT */
static void dispatch_dirty(ScreenPtr pScreen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(scrn);
    PixmapPtr pixmap = pScreen->GetScreenPixmap(pScreen);
    int fb_id = ms->drmmode.fb_id;
    int ret;

    ret = dispatch_dirty_region(scrn, pixmap, ms->damage, fb_id);
    if (ret == -EINVAL || ret == -ENOSYS)
    {
        ms->dirty_enabled = FALSE;
        DamageUnregister(ms->damage);
        DamageDestroy(ms->damage);
        ms->damage = NULL;
        xf86DrvMsg(scrn->scrnIndex, X_INFO,
                   "Disabling kernel dirty updates, not required.\n");
        return;
    }
}

/* OUTPUT SLAVE SUPPORT */
static void dispatch_dirty_pixmap(ScrnInfoPtr scrn, xf86CrtcPtr crtc, PixmapPtr ppix)
{
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr ppriv = msGetPixmapPriv(&ms->drmmode, ppix);
    DamagePtr damage = ppriv->slave_damage;
    int fb_id = ppriv->fb_id;

    dispatch_dirty_region(scrn, ppix, damage, fb_id);
}


/* OUTPUT SLAVE SUPPORT */
static void dispatch_slave_dirty(ScreenPtr pScreen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int c;

    for (c = 0; c < xf86_config->num_crtc; c++) {
        xf86CrtcPtr crtc = xf86_config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        if (!drmmode_crtc)
            continue;

        if (drmmode_crtc->prime_pixmap)
            dispatch_dirty_pixmap(scrn, crtc, drmmode_crtc->prime_pixmap);
        if (drmmode_crtc->prime_pixmap_back)
            dispatch_dirty_pixmap(scrn, crtc, drmmode_crtc->prime_pixmap_back);
    }
}


static void redisplay_dirty(ScreenPtr screen, PixmapDirtyUpdatePtr dirty, int *timeout)
{
    RegionRec pixregion;

    PixmapRegionInit(&pixregion, dirty->slave_dst);
    DamageRegionAppend(&dirty->slave_dst->drawable, &pixregion);
    PixmapSyncDirtyHelper(dirty);

    if (!screen->isGPU)
    {
#ifdef GLAMOR_HAS_GBM
        modesettingPtr ms = modesettingPTR(xf86ScreenToScrn(screen));
        /*
         * When copying from the master framebuffer to the shared pixmap,
         * we must ensure the copy is complete before the slave starts a
         * copy to its own framebuffer (some slaves scanout directly from
         * the shared pixmap, but not all).
         */
        if (ms->drmmode.glamor)
            ms->glamor.finish(screen);
#endif
        /* Ensure the slave processes the damage immediately */
        if (timeout)
            *timeout = 0;
    }

    DamageRegionProcessPending(&dirty->slave_dst->drawable);
    RegionUninit(&pixregion);
}


static void ms_dirty_update(ScreenPtr screen, int *timeout)
{
    modesettingPtr ms = modesettingPTR(xf86ScreenToScrn(screen));

    RegionPtr region;
    PixmapDirtyUpdatePtr ent;

    if (xorg_list_is_empty(&screen->pixmap_dirty_list))
        return;

    xorg_list_for_each_entry(ent, &screen->pixmap_dirty_list, ent)
    {
        region = DamageRegion(ent->damage);
        if (RegionNotEmpty(region)) {
            if (!screen->isGPU) {
                   msPixmapPrivPtr ppriv =
                    msGetPixmapPriv(&ms->drmmode, ent->slave_dst->master_pixmap);

                if (ppriv->notify_on_damage) {
                    ppriv->notify_on_damage = FALSE;

                    ent->slave_dst->drawable.pScreen->
                        SharedPixmapNotifyDamage(ent->slave_dst);
                }

                /* Requested manual updating */
                if (ppriv->defer_dirty_update)
                    continue;
            }

            redisplay_dirty(screen, ent, timeout);
            DamageEmpty(ent->damage);
        }
    }
}


static PixmapDirtyUpdatePtr ms_dirty_get_ent(ScreenPtr pScreen, PixmapPtr slave_dst)
{
    PixmapDirtyUpdatePtr ent;

    if (xorg_list_is_empty(&pScreen->pixmap_dirty_list))
        return NULL;

    xorg_list_for_each_entry(ent, &pScreen->pixmap_dirty_list, ent) {
        if (ent->slave_dst == slave_dst)
            return ent;
    }

    return NULL;
}


static void msBlockHandler(ScreenPtr pScreen, void *timeout)
{
    modesettingPtr ms = modesettingPTR(xf86ScreenToScrn(pScreen));

    pScreen->BlockHandler = ms->BlockHandler;
    pScreen->BlockHandler(pScreen, timeout);
    ms->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = msBlockHandler;

    if (pScreen->isGPU && !ms->drmmode.reverse_prime_offload_mode)
    {
        dispatch_slave_dirty(pScreen);
    }
    else if (ms->dirty_enabled)
    {
        dispatch_dirty(pScreen);
    }

    ms_dirty_update(pScreen, timeout);
}


static void msBlockHandler_oneshot(ScreenPtr pScreen, void *pTimeout)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(pScrn);

    msBlockHandler(pScreen, pTimeout);

    drmmode_set_desired_modes(pScrn, &ms->drmmode, TRUE);
}


static void FreeRec(ScrnInfoPtr pScrn)
{
    modesettingPtr ms;

    if (!pScrn)
        return;

    ms = modesettingPTR(pScrn);
    if (!ms)
        return;

    if (ms->fd > 0)
    {
        int ret;
        if ( 0 == LS_EntityDecreaseFdReference(pScrn) )
        {
            if (ms->pEnt->location.type == BUS_PCI)
            {
                ret = drmClose(ms->fd);
            }
            else
            {
#ifdef XF86_PDEV_SERVER_FD
                if (!(ms->pEnt->location.type == BUS_PLATFORM &&
                      (ms->pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD)))
                {
                    ret = close(ms->fd);
                }
#else
                {
                    ret = close(ms->fd);
                }
#endif
            }

            (void) ret;
        }
    }

    pScrn->driverPrivate = NULL;
    free(ms->drmmode.Options);
    free(ms);
}



static Bool ms_get_drm_master_fd(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);
    EntityInfoPtr pEnt = ms->pEnt;
    int cached_fd = LS_EntityGetCachedFd(pScrn);

    if ( cached_fd != 0)
    {
        ms->fd = cached_fd;
        LS_EntityIncreaseFdReference(pScrn);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "Reusing fd %d for second head.\n", cached_fd);

        return TRUE;
    }

    ms->fd_passed = FALSE;
    if ((ms->fd = LS_GetPassedFD()) >= 0)
    {
        ms->fd_passed = TRUE;
        return TRUE;
    }

#ifdef XSERVER_PLATFORM_BUS
    if (pEnt->location.type == BUS_PLATFORM)
    {
#ifdef XF86_PDEV_SERVER_FD
        if (pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD) {
            ms->fd =
                xf86_platform_device_odev_attributes(pEnt->location.id.plat)->fd;
            // suijingfeng : server manage fd is not working on our platform
            // now. we don't know what's the reason and how to enable that.
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "Get the fd from server managed fd.\n");
        }
        else
#endif
        {
            char *path = xf86_platform_device_odev_attributes(
                        pEnt->location.id.plat)->path;
            if (NULL != path) {
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "path = %s, got from PLATFORM\n", path);
            }

            ms->fd = LS_OpenHW(path);
        }
    }
    else
#endif
#ifdef XSERVER_LIBPCIACCESS
    if (pEnt->location.type == BUS_PCI)
    {
        char *BusID = NULL;
        struct pci_device *PciInfo;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "BUS: PCI\n");

        PciInfo = xf86GetPciInfoForEntity(pEnt->index);
        if (PciInfo)
        {
            if ((BusID = LS_DRICreatePCIBusID(PciInfo)) != NULL)
            {
                ms->fd = drmOpen(NULL, BusID);

                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        " BusID = %s, got from pci bus\n", BusID);

                free(BusID);
            }
        }
    }
    else
#endif
    {
        const char *devicename;
        devicename = xf86FindOptionValue(pEnt->device->options, "kmsdev");

        if ( devicename )
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "kmsdev=%s, got from conf\n", devicename);
        }

        ms->fd = LS_OpenHW(devicename);
    }

    if (ms->fd < 0)
        return FALSE;

    LS_EntityInitFd(pScrn, ms->fd);

    return TRUE;
}


/* This is called by PreInit to set up the default visual */
static Bool InitDefaultVisual(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);

    int defaultdepth, defaultbpp;
    int bppflags;
    drmmode_get_default_bpp(pScrn, &ms->drmmode, &defaultdepth, &defaultbpp);

    //
    // By default, a 24bpp screen will use 32bpp images, this avoids
    // problems with many applications which just can't handle packed pixels.
    // If you want real 24bit images, include a 24bpp format in the pixmap
    // formats
    //

    if ((defaultdepth == 24) && (defaultbpp == 24))
    {
        ms->drmmode.force_24_32 = TRUE;
        ms->drmmode.kbpp = 24;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Using 24bpp hw front buffer with 32bpp shadow\n");
        defaultbpp = 32;
    }
    else
    {
        ms->drmmode.kbpp = 0;
    }

    bppflags = PreferConvert24to32 | SupportConvert24to32 | Support32bppFb;

    if (!xf86SetDepthBpp(pScrn, defaultdepth, defaultdepth, defaultbpp, bppflags))
        return FALSE;


    switch (pScrn->depth)
    {
        case 15:
        case 16:
        case 24:
        case 30:
        break;
        default:
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Given depth (%d) is not supported by the driver\n",
                   pScrn->depth);
        return FALSE;
    }

    xf86PrintDepthBpp(pScrn);
    if (!ms->drmmode.kbpp)
    {
        ms->drmmode.kbpp = pScrn->bitsPerPixel;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "drmmode.kbpp = %d\n", ms->drmmode.kbpp);
    }


    {
        rgb defaultWeight = { 0, 0, 0 };
        if (!xf86SetWeight(pScrn, defaultWeight, defaultWeight))
            return FALSE;
    }

    if (!xf86SetDefaultVisual(pScrn, -1))
    {
        return FALSE;
    }

    return TRUE;
}



static Bool PreInit(ScrnInfoPtr pScrn, int flags)
{
    modesettingPtr ms;

    uint64_t value = 0;
    int ret;
    int connector_count;
    Bool is_prime_supported = 0;

    if (pScrn->numEntities != 1)
    {
        // suijingfeng: print this to see when could this happen
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "PreInit: pScrn->numEntities = %d \n", pScrn->numEntities );
        return FALSE;
    }

    if (flags & PROBE_DETECT)
    {
        // suijingfeng:
        // support the \"-configure\" or \"-probe\" command line arguments.
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "PreInit: PROBE DETECT only.\n");
        return FALSE;
    }


    if ( FALSE == LS_AllocDriverPrivate(pScrn) )
    {
        return FALSE;
    }

    ms = modesettingPTR(pScrn);
    ms->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);
    ms->drmmode.is_secondary = FALSE;
    pScrn->displayWidth = 640;  /* default it */

    if (xf86IsEntityShared(pScrn->entityList[0]))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "PreInit: Entity is shared.\n");
        if (xf86IsPrimInitDone(pScrn->entityList[0]))
        {
            ms->drmmode.is_secondary = TRUE;
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "PreInit: Primary init is done.\n");
        }
        else
        {
            xf86SetPrimInitDone(pScrn->entityList[0]);

            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "PreInit: Primary init is NOT done, set it. \n");
        }
    }

    pScrn->monitor = pScrn->confScreen->monitor;
    pScrn->progClock = TRUE;
    pScrn->rgbBits = 8;

    if (!ms_get_drm_master_fd(pScrn))
    {
        return FALSE;
    }

    ms->drmmode.fd = ms->fd;

    if (!LS_CheckOutputs(ms->fd, &connector_count))
    {
        return FALSE;
    }

    InitDefaultVisual(pScrn);

    /* Process the options */

    LS_ProcessOptions(pScrn, &ms->drmmode.Options);

    LS_GetCursorDimK(pScrn);

    LS_PrepareDebug(pScrn);


    ret = drmGetCap(ms->fd, DRM_CAP_PRIME, &value);
    is_prime_supported = ((ret == 0) && (value != 0));

    // first try glamor, then try EXA
    // if both failed, using the shadowfb
    if ( try_enable_glamor(pScrn) == FALSE )
    {
        // if prime is not supported by the kms, fallback to shadow.
        if (is_prime_supported)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "DRM PRIME is supported, trying fake exa + dri3.\n");

            try_enable_exa(pScrn);
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                "DRM PRIME is NOT supported, will fallback to shadow.\n");
            ms->drmmode.exa_enabled = FALSE;
        }
    }


    if ( (FALSE == ms->drmmode.glamor) && (FALSE == ms->drmmode.exa_enabled) )
    {
        LS_TryEnableShadow(pScrn);
    }


    // Modules may be loaded at any point in this function,
    // and all modules that the driver will need must be loaded
    // before the end of this function.
    //
    // Load the required sub modules
    if (!xf86LoadSubModule(pScrn, "fb"))
    {
        return FALSE;
    }

    ms->drmmode.pageflip =
        xf86ReturnOptValBool(ms->drmmode.Options, OPTION_PAGEFLIP, TRUE);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
        "PageFlip enabled ? %s.\n", ms->drmmode.pageflip ? "YES" : "NO" );

    pScrn->capabilities = 0;
    if (is_prime_supported)
    {
        if (connector_count && (value & DRM_PRIME_CAP_IMPORT))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                 "DRM PRIME IMPORT support.\n");
            pScrn->capabilities |= RR_Capability_SinkOutput;

            if (ms->drmmode.glamor) {
                pScrn->capabilities |= RR_Capability_SinkOffload;
            }
        }
#ifdef GLAMOR_HAS_GBM_LINEAR
        if (value & DRM_PRIME_CAP_EXPORT && ms->drmmode.glamor)
        {
            pScrn->capabilities |= RR_Capability_SourceOutput |
                                   RR_Capability_SourceOffload;
        }
#endif
    }

    if (xf86ReturnOptValBool(ms->drmmode.Options, OPTION_ATOMIC, FALSE))
    {
        ret = drmSetClientCap(ms->fd, DRM_CLIENT_CAP_ATOMIC, 1);
        ms->atomic_modeset = (ret == 0);
    }
    else
    {
        ms->atomic_modeset = FALSE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
        "Atomic modeset enabled ? %s.\n",
            ms->atomic_modeset ? "YES" : "NO" );

    ms->kms_has_modifiers = FALSE;
    ret = drmGetCap(ms->fd, DRM_CAP_ADDFB2_MODIFIERS, &value);
    if (ret == 0 && value != 0)
    {
        ms->kms_has_modifiers = TRUE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, ms->kms_has_modifiers ?
        "KMS has modifier support.\n" : "KMS does't have modifier support\n");

    if (drmmode_pre_init(pScrn, &ms->drmmode, pScrn->bitsPerPixel / 8) == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "KMS setup failed\n");
        goto fail;
    }

    /*
     * If the driver can do gamma correction, it should call xf86SetGamma() here.
     */
    {
        Gamma zeros = { 0.0, 0.0, 0.0 };

        if (!xf86SetGamma(pScrn, zeros))
        {
            return FALSE;
        }
    }

    if (!(pScrn->is_gpu && connector_count == 0) && (pScrn->modes == NULL))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No modes.\n");
        return FALSE;
    }

    pScrn->currentMode = pScrn->modes;

    /* Set display resolution */
    xf86SetDpi(pScrn, 0, 0);


    if (ms->drmmode.shadow_enable)
    {
        LS_ShadowLoadAPI(pScrn);
    }

    //
    // It is expected that if the ChipPreInit() function returns TRUE,
    // then the only reasons that subsequent stages in the driver might
    // fail are lack or resources (like xalloc failures).
    //
    // All other possible reasons for failure should be determined
    // by the ChipPreInit() function.
    //
    return TRUE;

 fail:
    //
    // PreInit() returns FALSE when the configuration is unusable
    // in some way (unsupported depth, no valid modes, not enough
    // video memory, etc), and TRUE if it is usable.
    //
    return FALSE;
}



static Bool
msEnableSharedPixmapFlipping(RRCrtcPtr crtc, PixmapPtr front, PixmapPtr back)
{
    ScreenPtr screen = crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    EntityInfoPtr pEnt = ms->pEnt;
    xf86CrtcPtr xf86Crtc = crtc->devPrivate;

    if (!xf86Crtc)
        return FALSE;

    /* Not supported if we can't flip */
    if (!ms->drmmode.pageflip)
        return FALSE;

    /* Not currently supported with reverse PRIME */
    if (ms->drmmode.reverse_prime_offload_mode)
        return FALSE;

#ifdef XSERVER_PLATFORM_BUS
    if (pEnt->location.type == BUS_PLATFORM)
    {
        char *syspath =
            xf86_platform_device_odev_attributes(pEnt->location.id.plat)->syspath;

        /* Not supported for devices using USB transport due to misbehaved
         * vblank events */
        if (syspath && strstr(syspath, "usb"))
            return FALSE;

        /* EVDI uses USB transport but is platform device, not usb.
         * Blacklist it explicitly */
        if (syspath && strstr(syspath, "evdi"))
        {
            return FALSE;
        }
    }
#endif

    return drmmode_EnableSharedPixmapFlipping(xf86Crtc, &ms->drmmode, front, back);
}


static void msDisableSharedPixmapFlipping(RRCrtcPtr crtc)
{
    ScreenPtr screen = crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    xf86CrtcPtr xf86Crtc = crtc->devPrivate;

    if (xf86Crtc)
    {
        drmmode_DisableSharedPixmapFlipping(xf86Crtc, &ms->drmmode);
    }
}


static Bool
msStartFlippingPixmapTracking(RRCrtcPtr crtc, DrawablePtr src,
                              PixmapPtr slave_dst1, PixmapPtr slave_dst2,
                              int x, int y, int dst_x, int dst_y,
                              Rotation rotation)
{
    ScreenPtr pScreen = src->pScreen;
    modesettingPtr ms = modesettingPTR(xf86ScreenToScrn(pScreen));

    msPixmapPrivPtr ppriv1 = msGetPixmapPriv(&ms->drmmode, slave_dst1->master_pixmap),
                    ppriv2 = msGetPixmapPriv(&ms->drmmode, slave_dst2->master_pixmap);

    if (!PixmapStartDirtyTracking(src, slave_dst1, x, y,
                                  dst_x, dst_y, rotation))
    {
        return FALSE;
    }

    if (!PixmapStartDirtyTracking(src, slave_dst2, x, y,
                                  dst_x, dst_y, rotation))
    {
        PixmapStopDirtyTracking(src, slave_dst1);
        return FALSE;
    }

    ppriv1->slave_src = src;
    ppriv2->slave_src = src;

    ppriv1->dirty = ms_dirty_get_ent(pScreen, slave_dst1);
    ppriv2->dirty = ms_dirty_get_ent(pScreen, slave_dst2);

    ppriv1->defer_dirty_update = TRUE;
    ppriv2->defer_dirty_update = TRUE;

    return TRUE;
}


static Bool msPresentSharedPixmap(PixmapPtr slave_dst)
{
    ScreenPtr pScreen = slave_dst->master_pixmap->drawable.pScreen;
    modesettingPtr ms = modesettingPTR(xf86ScreenToScrn(pScreen));

    msPixmapPrivPtr ppriv = msGetPixmapPriv(&ms->drmmode, slave_dst->master_pixmap);

    RegionPtr region = DamageRegion(ppriv->dirty->damage);

    if (RegionNotEmpty(region))
    {
        redisplay_dirty(ppriv->slave_src->pScreen, ppriv->dirty, NULL);
        DamageEmpty(ppriv->dirty->damage);

        return TRUE;
    }

    return FALSE;
}

static Bool
msStopFlippingPixmapTracking(DrawablePtr src,
                             PixmapPtr slave_dst1, PixmapPtr slave_dst2)
{
    ScreenPtr pScreen = src->pScreen;
    modesettingPtr ms = modesettingPTR(xf86ScreenToScrn(pScreen));

    msPixmapPrivPtr ppriv1 = msGetPixmapPriv(&ms->drmmode, slave_dst1->master_pixmap);
    msPixmapPrivPtr ppriv2 = msGetPixmapPriv(&ms->drmmode, slave_dst2->master_pixmap);

    Bool ret = TRUE;

    ret &= PixmapStopDirtyTracking(src, slave_dst1);
    ret &= PixmapStopDirtyTracking(src, slave_dst2);

    if (ret)
    {
        ppriv1->slave_src = NULL;
        ppriv2->slave_src = NULL;

        ppriv1->dirty = NULL;
        ppriv2->dirty = NULL;

        ppriv1->defer_dirty_update = FALSE;
        ppriv2->defer_dirty_update = FALSE;
    }

    return ret;
}


static Bool CreateScreenResources(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(pScrn);
    PixmapPtr rootPixmap;
    Bool ret;
    void *pixels = NULL;
    int err;

    pScreen->CreateScreenResources = ms->createScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = CreateScreenResources;

    if (!drmmode_set_desired_modes(pScrn, &ms->drmmode, pScrn->is_gpu))
    {
        return FALSE;
    }

#ifdef GLAMOR_HAS_GBM
    if (!drmmode_glamor_handle_new_screen_pixmap(&ms->drmmode))
    {
        return FALSE;
    }
#endif

    drmmode_uevent_init(pScrn, &ms->drmmode);


    if (ms->drmmode.sw_cursor == FALSE)
    {
        drmmode_map_cursor_bos(pScrn, &ms->drmmode);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "hardware cursor enabled, mapping it.\n");
    }

    if (ms->drmmode.gbm == NULL)
    {
        pixels = drmmode_map_front_bo(&ms->drmmode);
        if (pixels == NULL)
        {
            return FALSE;
        }
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "ms->drmmode.gbm is NULL, mapping it.\n");
    }

    rootPixmap = pScreen->GetScreenPixmap(pScreen);

    if (ms->drmmode.shadow_enable)
    {
        pixels = ms->drmmode.shadow_fb;
    }

    if (ms->drmmode.shadow_enable2)
    {
        ms->drmmode.shadow_enable2 = LS_ShadowAllocDoubleFB(pScrn);
    }


    // Recall the comment of of miCreateScreenResources()
    // create a pixmap with no data, then redirect it to point to the screen".
    // The routine that created the empty pixmap was (*pScreen->CreatePixmap)
    // actually fbCreatePixmap() and the routine that (*pScreen->ModifyPixmapHeader),
    // which is actually miModifyPixmapHeader() sets the
    // address of the pixmap to the screen memory address
    // is (*pScreen->ModifyPixmapHeader), which is actually miModifyPixmapHeader().
    //
    //
    // The address is passed as the last argument of (*pScreen->ModifyPixmapHeader)
    // and as seen in miCreateScreenResources() this is pScrInitParms->pbits.
    // This was set to pbits by miScreenDevPrivateInit() and pbits replaces
    // the FBStart fbScreenInit(), which is the screen memory address.
    //
    // As we read in section 3.2.2.11: "Mga->FbStart is equal to pMga->FbBase
    // since YDstOrg (the offset in bytes from video start to usable memory)
    // is usually zero (see comment in MGAPreInit())".
    //
    // Additionally, if an aperture used to access video memory is
    // unmapped and remapped in this fashion, ChipEnterVT() will
    // also need to notify the framebuffer layers of the aperture's
    // new location in virtual memory.  This is done with a call
    // to the screen's ModifyPixmapHeader() function
    //
    // where the rootPixmap field in a ScrnInfoRec points to the
    // pixmap used by the screen's SaveRestoreImage() function to
    // hold the screen's contents while switched out.
    //
    if (!pScreen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, -1, pixels))
    {
        FatalError("Couldn't adjust screen pixmap\n");
    }

    if (ms->drmmode.shadow_enable)
    {
        if (!ms->shadow.Add(pScreen, rootPixmap, LS_ShadowUpdatePacked, LS_ShadowWindow, 0, 0))
        {
            return FALSE;
        }
    }

    err = drmModeDirtyFB(ms->fd, ms->drmmode.fb_id, NULL, 0);

    if ((err != -EINVAL) && (err != -ENOSYS))
    {
        ms->damage = DamageCreate(NULL, NULL, DamageReportNone, TRUE,
                                  pScreen, rootPixmap);

        if (ms->damage)
        {
            DamageRegister(&rootPixmap->drawable, ms->damage);
            ms->dirty_enabled = TRUE;
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Damage tracking initialized\n");
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to create screen damage record\n");
            return FALSE;
        }
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "[drm] dirty fb failed: %d\n", err);
    }

    if (dixPrivateKeyRegistered(rrPrivKey))
    {
        rrScrPrivPtr pScrPriv = rrGetScrPriv(pScreen);

        pScrPriv->rrEnableSharedPixmapFlipping = msEnableSharedPixmapFlipping;
        pScrPriv->rrDisableSharedPixmapFlipping = msDisableSharedPixmapFlipping;

        pScrPriv->rrStartFlippingPixmapTracking = msStartFlippingPixmapTracking;
    }

    return ret;
}


static Bool msSharePixmapBacking(PixmapPtr ppix, ScreenPtr slave, void **handle)
{
    modesettingPtr ms =
        modesettingPTR(xf86ScreenToScrn(ppix->drawable.pScreen));
    int ret = -1;
    CARD16 stride;
    CARD32 size;

#ifdef GLAMOR_HAS_GBM
    ret = ms->glamor.shareable_fd_from_pixmap(ppix->drawable.pScreen, ppix,
                                              &stride, &size);
    if (ret == -1)
    {
        return FALSE;
    }

    *handle = (void *)(long)(ret);
    return TRUE;
#endif

    if (ms->drmmode.exa_enabled)
    {
        ret = ms_exa_shareable_fd_from_pixmap(ppix->drawable.pScreen,
                ppix, &stride, &size);
        if (ret == -1)
        {
            return FALSE;
        }

        *handle = (void *)(long)(ret);
        return TRUE;
    }

    return FALSE;
}


/* OUTPUT SLAVE SUPPORT */
static Bool msSetSharedPixmapBacking(PixmapPtr ppix, void *fd_handle)
{
    ScreenPtr screen = ppix->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    Bool ret = FALSE;
    int ihandle = (int) (long) fd_handle;

    if (ihandle == -1)
        if (!ms->drmmode.reverse_prime_offload_mode)
           return drmmode_SetSlaveBO(ppix, &ms->drmmode, ihandle, 0, 0);

    if (ms->drmmode.reverse_prime_offload_mode)
    {
        // suijingfeng:
        // we can use either glamor or exa,
        // Not both at the same time.
#ifdef GLAMOR_HAS_GBM
        if (ms->drmmode.glamor)
        {
            ret = ms->glamor.back_pixmap_from_fd(ppix, ihandle,
                                             ppix->drawable.width,
                                             ppix->drawable.height,
                                             ppix->devKind,
                                             ppix->drawable.depth,
                                             ppix->drawable.bitsPerPixel);
            return ret;
        }
#endif

        if (ms->drmmode.exa_enabled)
        {
            ret = ms_exa_back_pixmap_from_fd(ppix, ihandle,
                    ppix->drawable.width, ppix->drawable.height,
                    ppix->devKind, ppix->drawable.depth,
                    ppix->drawable.bitsPerPixel);

            return ret;
        }

    }
    else
    {
        int size = ppix->devKind * ppix->drawable.height;
        ret = drmmode_SetSlaveBO(ppix, &ms->drmmode, ihandle, ppix->devKind, size);
    }

    return ret;
}


static Bool msRequestSharedPixmapNotifyDamage(PixmapPtr ppix)
{
    ScreenPtr screen = ppix->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    msPixmapPrivPtr ppriv = msGetPixmapPriv(&ms->drmmode, ppix->master_pixmap);

    ppriv->notify_on_damage = TRUE;

    return TRUE;
}


static Bool msSharedPixmapNotifyDamage(PixmapPtr ppix)
{
    Bool ret = FALSE;
    int c;

    ScreenPtr screen = ppix->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);

    msPixmapPrivPtr ppriv = msGetPixmapPriv(&ms->drmmode, ppix);

    if (!ppriv->wait_for_damage)
        return ret;
    ppriv->wait_for_damage = FALSE;

    for (c = 0; c < xf86_config->num_crtc; c++)
    {
        xf86CrtcPtr crtc = xf86_config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        if (!drmmode_crtc)
            continue;
        if (!(drmmode_crtc->prime_pixmap && drmmode_crtc->prime_pixmap_back))
            continue;

        // Received damage on master screen pixmap, schedule present on vblank
        ret |= drmmode_SharedPixmapPresentOnVBlank(ppix, crtc, &ms->drmmode);
    }

    return ret;
}


static Bool SetMaster(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);
    int ret;

#ifdef XF86_PDEV_SERVER_FD
    if (ms->pEnt->location.type == BUS_PLATFORM &&
        (ms->pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD))
        return TRUE;
#endif

    if (ms->fd_passed)
        return TRUE;

    ret = drmSetMaster(ms->fd);
    if (ret)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "drmSetMaster failed: %s\n",
                   strerror(errno));

    return ret == 0;
}

/* When the root window is created, initialize the screen contents from
 * console if -background none was specified on the command line
 */
static Bool CreateWindow_oneshot(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(pScrn);
    Bool ret;

    pScreen->CreateWindow = ms->CreateWindow;
    ret = pScreen->CreateWindow(pWin);

    if (ret)
        drmmode_copy_fb(pScrn, &ms->drmmode);
    return ret;
}


//
// When ScreenInit() phase is done the common level will determine
// which shared resources are requested by more than one driver and
// set the access functions accordingly.
//
// This is done following these rules:
//
// The sharable resources registered by each entity are compared.
// If a resource is registered by more than one entity the entity
// will be marked to need to share this resources type (IO or MEM).
//
// A resource marked “disabled” during OPERATING state will be
// ignored entirely.
//
// A resource marked “unused” will only conflicts with an overlapping
// resource of an other entity if the second is actually in use during
// OPERATING state.
//
// If an “unused” resource was found to conflict however the entity
// does not use any other resource of this type the entire resource
// type will be disabled for that entity.
//
//
// The driver has the choice among different ways to control access
// to certain resources:
//
// 1. It can rely on the generic access functions. This is probably the
// most common case. Here the driver only needs to register any resource
// it is going to use.
//
// 2. It can replace the generic access functions by driver specific ones.
// This will mostly be used in cases where no generic access functions are
// available. In this case the driver has to make sure these resources are
// disabled when entering the PreInit() stage. Since the replacement
// functions are registered in PreInit() the driver will have to enable
// these resources itself if it needs to access them during this state.
// The driver can specify if the replacement functions can control memory
// and/or I/O resources separately.

// The driver can enable resources itself when it needs them.
// Each driver function enabling them needs to disable them
// before it will return. This should be used if a resource
// which can be controlled in a device dependent way is only
// required during SETUP state.
// This way it can be marked “unused” during OPERATING state.


static Bool ScreenInit(ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(pScrn);
    VisualPtr visual;
    pScrn->pScreen = pScreen;

    if (!SetMaster(pScrn))
    {
        return FALSE;
    }

#ifdef GLAMOR_HAS_GBM
    if (ms->drmmode.glamor)
    {
        ms->drmmode.gbm = ms->glamor.egl_get_gbm_device(pScreen);
    }
#endif

    /* HW dependent - FIXME */
    pScrn->displayWidth = pScrn->virtualX;

    if (!drmmode_create_initial_bos(pScrn, &ms->drmmode))
    {
        return FALSE;
    }

    if (ms->drmmode.shadow_enable)
    {
        ms->drmmode.shadow_enable = LS_ShadowAllocFB(pScrn);
    }

    /* Reset the visual list. */
    miClearVisualTypes();

    if (!miSetVisualTypes(pScrn->depth,
                          miGetDefaultVisualMask(pScrn->depth),
                          pScrn->rgbBits, pScrn->defaultVisual))
    {
        return FALSE;
    }

    if (!miSetPixmapDepths())
    {
        return FALSE;
    }

    /* OUTPUT SLAVE SUPPORT */
    if (!dixRegisterScreenSpecificPrivateKey
        (pScreen, &ms->drmmode.pixmapPrivateKeyRec, PRIVATE_PIXMAP,
         sizeof(msPixmapPrivRec)))
    {
        return FALSE;
    }

    pScrn->memPhysBase = 0;
    pScrn->fbOffset = 0;

    //
    // The DDX layer's ScreenInit() function usually calls another layer's
    // ScreenInit() function (e.g., miScreenInit() or fbScreenInit()) to
    // initialize the fallbacks that the DDX driver does not specifically
    // handle.
    //
    // fbScreenInit() is used to tell the fb layer where the video card
    // framebuffer is.
    //
    if (!fbScreenInit(pScreen, NULL,
                      pScrn->virtualX, pScrn->virtualY,
                      pScrn->xDpi, pScrn->yDpi,
                      pScrn->displayWidth, pScrn->bitsPerPixel))
    {
        return FALSE;
    }

    if (pScrn->bitsPerPixel > 8)
    {
        /* Fixup RGB ordering */
        visual = pScreen->visuals + pScreen->numVisuals;
        while (--visual >= pScreen->visuals)
        {
            if ((visual->class | DynamicClass) == DirectColor)
            {
                visual->offsetRed = pScrn->offset.red;
                visual->offsetGreen = pScrn->offset.green;
                visual->offsetBlue = pScrn->offset.blue;
                visual->redMask = pScrn->mask.red;
                visual->greenMask = pScrn->mask.green;
                visual->blueMask = pScrn->mask.blue;
            }
        }
    }

    fbPictureInit(pScreen, NULL, 0);

    if (drmmode_init(pScrn, &ms->drmmode) == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to initialize glamor at ScreenInit() time.\n");
        return FALSE;
    }

    if (ms->drmmode.shadow_enable)
    {
        if (ms->shadow.Setup(pScreen) == FALSE)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "shadow fb init failed\n");

            return FALSE;
        }
    }

    /*
     * With the introduction of pixmap privates, the "screen pixmap" can no
     * longer be created in miScreenInit, since all the modules that could
     * possibly ask for pixmap private space have not been initialized at
     * that time.  pScreen->CreateScreenResources is called after all
     * possible private-requesting modules have been inited; we create the
     * screen pixmap here.
     */
    ms->createScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = CreateScreenResources;


    /* Set the initial black & white colormap indices: */
    xf86SetBlackWhitePixels(pScreen);
    /* Initialize backing store: */
    xf86SetBackingStore(pScreen);
    /* Enable cursor position updates by mouse signal handler: */
    xf86SetSilkenMouse(pScreen);

    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    /* If pageflip is enabled hook the screen's cursor-sprite (swcursor) funcs.
     * So that we can disabe page-flipping on fallback to a swcursor. */
    if (ms->drmmode.pageflip)
    {
        miPointerScreenPtr PointPriv =
            dixLookupPrivate(&pScreen->devPrivates, miPointerScreenKey);

        if (!dixRegisterScreenPrivateKey(&ms->drmmode.spritePrivateKeyRec,
                            pScreen, PRIVATE_DEVICE, sizeof(msSpritePrivRec)))
        {
            return FALSE;
        }

        ms->SpriteFuncs = PointPriv->spriteFuncs;
        PointPriv->spriteFuncs = &drmmode_sprite_funcs;
    }

    /* Need to extend HWcursor support to handle mask interleave */
    if (!ms->drmmode.sw_cursor)
    {
        xf86_cursors_init(pScreen, ms->cursor_width, ms->cursor_height,
                          HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_64 |
                          HARDWARE_CURSOR_UPDATE_UNHIDDEN |
                          HARDWARE_CURSOR_ARGB);
    }
    /* Must force it before EnterVT, so we are in control of VT and
     * later memory should be bound when allocating, e.g rotate_mem */
    pScrn->vtSema = TRUE;


    if ( ms->drmmode.exa_enabled == TRUE )
    {
        if (!LS_InitExaLayer(pScreen))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "internal error: initExaLayer failed in ScreenInit()\n");
        }
    }


    if ((serverGeneration == 1) && bgNoneRoot && ms->drmmode.glamor)
    {
        ms->CreateWindow = pScreen->CreateWindow;
        pScreen->CreateWindow = CreateWindow_oneshot;
    }

    //
    // After calling another layer's ScreenInit() function, any screen-specific
    // functions either wrap or replace the other layer's function pointers.
    // If a function is to be wrapped, each of the old function pointers from
    // the other layer are stored in a screen private area. Common functions
    // to wrap are CloseScreen() and SaveScreen().
    //
    pScreen->SaveScreen = xf86SaveScreen;
    ms->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = CloseScreen;

    ms->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = msBlockHandler_oneshot;

    pScreen->SharePixmapBacking = msSharePixmapBacking;
    /* OUTPUT SLAVE SUPPORT */
    pScreen->SetSharedPixmapBacking = msSetSharedPixmapBacking;
    pScreen->StartPixmapTracking = PixmapStartDirtyTracking;
    pScreen->StopPixmapTracking = PixmapStopDirtyTracking;

    pScreen->SharedPixmapNotifyDamage = msSharedPixmapNotifyDamage;
    pScreen->RequestSharedPixmapNotifyDamage =
        msRequestSharedPixmapNotifyDamage;

    pScreen->PresentSharedPixmap = msPresentSharedPixmap;
    pScreen->StopFlippingPixmapTracking = msStopFlippingPixmapTracking;

    if (!xf86CrtcScreenInit(pScreen))
    {
        return FALSE;
    }

    if (!drmmode_setup_colormap(pScreen, pScrn))
    {
        return FALSE;
    }

    if (ms->atomic_modeset)
    {
        xf86DPMSInit(pScreen, drmmode_set_dpms, 0);
    }
    else
    {
        xf86DPMSInit(pScreen, xf86DPMSSet, 0);
    }

#ifdef GLAMOR_HAS_GBM
    if (ms->drmmode.glamor)
    {
        XF86VideoAdaptorPtr glamor_adaptor;

        glamor_adaptor = ms->glamor.xv_init(pScreen, 16);
        if (glamor_adaptor != NULL)
            xf86XVScreenInit(pScreen, &glamor_adaptor, 1);
        else
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to initialize XV support.\n");
    }
#endif

    if (serverGeneration == 1)
    {
        xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);
    }


    if (!ms_vblank_screen_init(pScreen))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to initialize vblank support.\n");
        return FALSE;
    }

#ifdef GLAMOR_HAS_GBM
    if (ms->drmmode.glamor)
    {
        if (!(ms->drmmode.dri2_enable = ms_dri2_screen_init(pScreen)))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to initialize the DRI2 extension.\n");
        }

        if (!(ms->drmmode.present_enable = ms_present_screen_init(pScreen)))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to initialize the Present extension.\n");
        }
        /* enable reverse prime if we are a GPU screen, and accelerated, and not
         * i915. i915 is happy scanning out from sysmem. */
        if (pScreen->isGPU)
        {
            drmVersionPtr version;

            /* enable if we are an accelerated GPU screen */
            ms->drmmode.reverse_prime_offload_mode = TRUE;

            /* disable if we detect i915 */
            if ((version = drmGetVersion(ms->drmmode.fd)))
            {
                if (!strncmp("i915", version->name, version->name_len))
                {
                    ms->drmmode.reverse_prime_offload_mode = FALSE;
                }
                drmFreeVersion(version);
            }
        }
    }
    else
#endif
    {
        if (ms->drmmode.exa_enabled)
        {
            ms->drmmode.dri2_enable = FALSE;
            #if 0
            // TODO : add exa + dri2 support
            ms->drmmode.dri2_enable = ms_dri2_screen_init(pScreen);
            if ( ms->drmmode.dri2_enable == FALSE)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                        "Failed to initialize the DRI2 extension.\n");
            }
            #endif

            ms->drmmode.present_enable = ms_present_screen_init(pScreen);
            if (ms->drmmode.present_enable == FALSE)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Failed to initialize the Present extension.\n");
            }

            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Present extension enabled.\n");

            // Enable reverse prime if we are a GPU screen, and accelerated.
            if (pScreen->isGPU)
            {
                /* enable if we are an accelerated GPU screen */
                ms->drmmode.reverse_prime_offload_mode = TRUE;
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "Reverse prime Enable.\n");
            }
        }
    }

#ifdef DRI3
    if (ms->drmmode.exa_enabled)
    {
        if (!ms_exa_dri3_init(pScreen))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to initialize the DRI3 extension.\n");
        }
    }
#endif

    pScrn->vtSema = TRUE;

    return TRUE;
}


static void AdjustFrame(ScrnInfoPtr pScrn, int x, int y)
{
    modesettingPtr ms = modesettingPTR(pScrn);

    drmmode_adjust_frame(pScrn, &ms->drmmode, x, y);
}


static void FreeScreen(ScrnInfoPtr pScrn)
{
    FreeRec(pScrn);
}


static void LeaveVT(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);

    xf86_hide_cursors(pScrn);

    pScrn->vtSema = FALSE;

#ifdef XF86_PDEV_SERVER_FD
    if (ms->pEnt->location.type == BUS_PLATFORM &&
        (ms->pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD))
    {
        return;
    }
#endif

    if (!ms->fd_passed)
    {
        drmDropMaster(ms->fd);
    }
}

/*
 * This gets called when gaining control of the VT, and from ScreenInit().
 */
static Bool EnterVT(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);

    pScrn->vtSema = TRUE;

    SetMaster(pScrn);

    if (!drmmode_set_desired_modes(pScrn, &ms->drmmode, TRUE))
    {
        return FALSE;
    }

    return TRUE;
}


static Bool SwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    return xf86SetSingleMode(pScrn, mode, RR_Rotate_0);
}


static Bool CloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(pScrn);

    LS_EntityClearAssignedCrtc(pScrn);

#ifdef GLAMOR_HAS_GBM
    if (ms->drmmode.dri2_enable)
    {
        ms_dri2_close_screen(pScreen);
    }
#endif

    ms_vblank_close_screen(pScreen);

    if (ms->damage)
    {
        DamageUnregister(ms->damage);
        DamageDestroy(ms->damage);
        ms->damage = NULL;
    }


    if (ms->drmmode.exa_enabled)
    {
        LS_DestroyExaLayer(pScreen);
    }


    if (ms->drmmode.shadow_enable)
    {
        ms->shadow.Remove(pScreen, pScreen->GetScreenPixmap(pScreen));

        LS_ShadowFreeFB(pScrn);

        LS_ShadowFreeDoubleFB(pScrn);
    }

    drmmode_uevent_fini(pScrn, &ms->drmmode);

    drmmode_free_bos(pScrn, &ms->drmmode);

    if (ms->drmmode.pageflip)
    {
        miPointerScreenPtr PointPriv =
            dixLookupPrivate(&pScreen->devPrivates, miPointerScreenKey);

        if (PointPriv->spriteFuncs == &drmmode_sprite_funcs)
            PointPriv->spriteFuncs = ms->SpriteFuncs;
    }

    if (pScrn->vtSema)
    {
        LeaveVT(pScrn);
    }

    pScreen->CreateScreenResources = ms->createScreenResources;
    pScreen->BlockHandler = ms->BlockHandler;

    pScrn->vtSema = FALSE;
    pScreen->CloseScreen = ms->CloseScreen;
    return (*pScreen->CloseScreen) (pScreen);
}

static ModeStatus ValidMode(ScrnInfoPtr arg,
        DisplayModePtr mode, Bool verbose, int flags)
{
    return MODE_OK;
}
