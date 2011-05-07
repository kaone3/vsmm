/*
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

/**
 * @file
 * null audio filter
 */

#include "avfilter.h"

#ifdef _MSC_VER
static const AVFilterPad inputs[] = {
	{
		"default",
		AVMEDIA_TYPE_AUDIO,
		0,
		0,
		NULL,
		NULL,
		avfilter_null_get_audio_buffer,
		NULL,
		NULL,
		avfilter_null_filter_samples,
		NULL,
		NULL,
		NULL,
	},
	{
		NULL,
		AVMEDIA_TYPE_UNKNOWN,
		0,
		0,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
	},
};
static const AVFilterPad outputs[] = {
	{
		"default",
		AVMEDIA_TYPE_AUDIO,
		0,
		0,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
	},
	{
		NULL,
		AVMEDIA_TYPE_UNKNOWN,
		0,
		0,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
	},
};
AVFilter avfilter_af_anull = {
    "anull",
    0,
	NULL,
	NULL,
	NULL,
	inputs,
	outputs,
    NULL_IF_CONFIG_SMALL("Pass the source unchanged to the output."),
};
#else	/* _MSC_VER */
AVFilter avfilter_af_anull = {
    .name      = "anull",
    .description = NULL_IF_CONFIG_SMALL("Pass the source unchanged to the output."),

    .priv_size = 0,

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_AUDIO,
                                    .get_audio_buffer = avfilter_null_get_audio_buffer,
                                    .filter_samples   = avfilter_null_filter_samples },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_AUDIO, },
                                  { .name = NULL}},
};
#endif	/* _MSC_VER */
