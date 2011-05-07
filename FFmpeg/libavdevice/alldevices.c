/*
 * Register all the grabbing devices.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "libavformat/avformat.h"
#include "avdevice.h"

#define REGISTER_OUTDEV(X,x) { \
          extern AVOutputFormat x##_muxer; \
          if(CONFIG_##X##_OUTDEV)  av_register_output_format(&x##_muxer); }
#define REGISTER_INDEV(X,x) { \
          extern AVInputFormat x##_demuxer; \
          if(CONFIG_##X##_INDEV)   av_register_input_format(&x##_demuxer); }
#define REGISTER_INOUTDEV(X,x)  REGISTER_OUTDEV(X,x); REGISTER_INDEV(X,x)

void avdevice_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

    /* devices */
#if (CONFIG_ALSA_INDEV || CONFIG_ALSA_OUTDEV)
    REGISTER_INOUTDEV (ALSA, alsa);
#endif
#if CONFIG_BKTR_INDEV
    REGISTER_INDEV    (BKTR, bktr);
#endif
#if CONFIG_DV1394_INDEV
    REGISTER_INDEV    (DV1394, dv1394);
#endif
#if CONFIG_JACK_INDEV
    REGISTER_INDEV    (JACK, jack);
#endif
#if (CONFIG_OSS_INDEV || CONFIG_OSS_OUTDEV)
    REGISTER_INOUTDEV (OSS, oss);
#endif
#if CONFIG_V4L2_INDEV
    REGISTER_INDEV    (V4L2, v4l2);
#endif
#if CONFIG_V4L_INDEV
    REGISTER_INDEV    (V4L, v4l);
#endif
    REGISTER_INDEV    (VFWCAP, vfwcap);
#if CONFIG_X11_GRAB_DEVICE_INDEV
    REGISTER_INDEV    (X11_GRAB_DEVICE, x11_grab_device);
#endif

    /* external libraries */
#if CONFIG_LIBDC1394
    REGISTER_INDEV    (LIBDC1394, libdc1394);
#endif
}
