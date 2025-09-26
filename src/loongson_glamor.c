#include "config.h"

#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include <xf86.h>
#include <xf86_OSproc.h>

#include "xf86Crtc.h"


#include "driver.h"
#include "loongson_options.h"

#include "loongson_glamor.h"



#ifdef GLAMOR_HAS_GBM

static Bool load_glamor(ScrnInfoPtr pScrn)
{
    void *mod = xf86LoadSubModule(pScrn, GLAMOR_EGL_MODULE_NAME);
    modesettingPtr ms = modesettingPTR(pScrn);

    if (!mod)
        return FALSE;

    ms->glamor.back_pixmap_from_fd = LoaderSymbol(mod, "glamor_back_pixmap_from_fd");
    ms->glamor.block_handler = LoaderSymbol(mod, "glamor_block_handler");
    ms->glamor.clear_pixmap = LoaderSymbol(mod, "glamor_clear_pixmap");
    ms->glamor.egl_create_textured_pixmap = LoaderSymbol(mod, "glamor_egl_create_textured_pixmap");
    ms->glamor.egl_create_textured_pixmap_from_gbm_bo = LoaderSymbol(mod, "glamor_egl_create_textured_pixmap_from_gbm_bo");
    ms->glamor.egl_exchange_buffers = LoaderSymbol(mod, "glamor_egl_exchange_buffers");
    ms->glamor.egl_get_gbm_device = LoaderSymbol(mod, "glamor_egl_get_gbm_device");
    ms->glamor.egl_init = LoaderSymbol(mod, "glamor_egl_init");
    ms->glamor.finish = LoaderSymbol(mod, "glamor_finish");
    ms->glamor.gbm_bo_from_pixmap = LoaderSymbol(mod, "glamor_gbm_bo_from_pixmap");
    ms->glamor.init = LoaderSymbol(mod, "glamor_init");
    ms->glamor.name_from_pixmap = LoaderSymbol(mod, "glamor_name_from_pixmap");
    ms->glamor.set_drawable_modifiers_func = LoaderSymbol(mod, "glamor_set_drawable_modifiers_func");
    ms->glamor.shareable_fd_from_pixmap = LoaderSymbol(mod, "glamor_shareable_fd_from_pixmap");
    ms->glamor.supports_pixmap_import_export = LoaderSymbol(mod, "glamor_supports_pixmap_import_export");
    ms->glamor.xv_init = LoaderSymbol(mod, "glamor_xv_init");
    ms->glamor.egl_get_driver_name = LoaderSymbol(mod, "glamor_egl_get_driver_name");

    return TRUE;
}

#endif


Bool try_enable_glamor(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);
    const char *accel_method_str = xf86GetOptValString(ms->drmmode.Options,
                                                       OPTION_ACCEL_METHOD);
    Bool do_glamor = (!accel_method_str ||
                      strcmp(accel_method_str, "glamor") == 0);

    ms->drmmode.glamor = FALSE;

#ifdef GLAMOR_HAS_GBM
    if (ms->drmmode.force_24_32)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                       "Cannot use glamor with 24bpp packed fb\n");
        return FALSE;
    }

    if (!do_glamor)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "glamor disabled\n");
        return FALSE;
    }

    if (load_glamor(pScrn))
    {
        if (ms->glamor.egl_init(pScrn, ms->fd))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "glamor initialized\n");
            ms->drmmode.glamor = TRUE;
            return TRUE;
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "glamor initialization failed\n");
        }
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to load glamor module.\n");
        return FALSE;
    }
#else
    if (do_glamor)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "No glamor support in the X Server\n");

        return FALSE;
    }
#endif

    return ms->drmmode.glamor;
}

