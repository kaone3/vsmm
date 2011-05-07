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
 * copy video filter
 */

#include "avfilter.h"

#ifdef _MSC_VER
static const AVFilterPad inputs[] = {
	{
		"default",
		AVMEDIA_TYPE_VIDEO,
		0,
		~0,
		avfilter_null_start_frame,
		avfilter_null_get_video_buffer,
		NULL,
		avfilter_null_end_frame,
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
static const AVFilterPad outputs[] = {
	{
		"default",
		AVMEDIA_TYPE_VIDEO,
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
AVFilter avfilter_vf_copy = {
    "copy",
	0,
	NULL,
	NULL,
	NULL,
	inputs,
	outputs,
    NULL_IF_CONFIG_SMALL("Copy the input video unchanged to the output."),
};
#else	/* _MSC_VER */
AVFilter avfilter_vf_copy = {
    .name      = "copy",
    .description = NULL_IF_CONFIG_SMALL("Copy the input video unchanged to the output."),

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .get_video_buffer = avfilter_null_get_video_buffer,
                                    .start_frame      = avfilter_null_start_frame,
                                    .end_frame        = avfilter_null_end_frame,
                                    .rej_perms        = ~0 },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
#endif	/* _MSC_VER */
