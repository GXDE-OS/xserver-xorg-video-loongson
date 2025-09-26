/*
 * Copyright © 2020 Loongson Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Sui Jingfeng <suijingfeng@loongson.cn>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdint.h>
#include <xf86drmMode.h>
#include <xf86str.h>
#include <xf86drm.h>

#include "driver.h"
#include "drmmode_display.h"

#include "loongson_options.h"
#include "loongson_cursor.h"

// the suffix K stand for Kernel
void LS_GetCursorDimK(ScrnInfoPtr pScrn)
{
    modesettingPtr ms = modesettingPTR(pScrn);
    int ret = -1;
    uint64_t value = 0;

    // cusor related
    if (xf86ReturnOptValBool(ms->drmmode.Options, OPTION_SW_CURSOR, FALSE))
    {
        ms->drmmode.sw_cursor = TRUE;
    }

    ms->cursor_width = 64;
    ms->cursor_height = 64;

    ret = drmGetCap(ms->fd, DRM_CAP_CURSOR_WIDTH, &value);
    if (!ret)
    {
        ms->cursor_width = value;
    }

    ret = drmGetCap(ms->fd, DRM_CAP_CURSOR_HEIGHT, &value);
    if (!ret)
    {
        ms->cursor_height = value;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            " %s Cursor: width x height = %dx%d\n",
            ms->drmmode.sw_cursor ? "Software" : "Hardware",
            ms->cursor_width, ms->cursor_height );
}
