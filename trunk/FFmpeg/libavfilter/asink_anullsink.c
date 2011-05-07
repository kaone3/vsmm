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

#include "avfilter.h"

static void null_filter_samples(AVFilterLink *link, AVFilterBufferRef *samplesref) { }

#ifdef _MSC_VER
static const AVFilterPad inputs[] = {
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
		null_filter_samples,
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
AVFilter avfilter_asink_anullsink = {
    "anullsink",
    0,
	NULL,
	NULL,
	NULL,
	inputs,
	outputs,
    NULL_IF_CONFIG_SMALL("Do absolutely nothing with the input audio."),
};
#else	/* _MSC_VER */
AVFilter avfilter_asink_anullsink = {
    .name        = "anullsink",
    .description = NULL_IF_CONFIG_SMALL("Do absolutely nothing with the input audio."),

    .priv_size = 0,

    .inputs    = (AVFilterPad[]) {
        {
            .name            = "default",
            .type            = AVMEDIA_TYPE_AUDIO,
            .filter_samples  = null_filter_samples,
        },
        { .name = NULL},
    },
    .outputs   = (AVFilterPad[]) {{ .name = NULL }},
};
#endif	/* _MSC_VER */
