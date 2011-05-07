/*
 * FFplay : Simple Media Player based on the FFmpeg libraries
 * Copyright (c) 2003 Fabrice Bellard
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

#include <windows.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale_internal.h"
#include "libswscale/swscale.h"
#include "SDL.h"

/* SDL audio buffer size, in samples. Should be small to have precise
   A/V sync as SDL does not have hardware buffer fullness info. */
#define SDL_AUDIO_BUFFER_SIZE 1024

#define VIDEO_PICTURE_QUEUE_SIZE 2
#define MAX_VIDEOQ_SIZE (15 * 1024 * 1024)
#define MAX_AUDIOQ_SIZE (20 * 16 * 1024)

#define FF_ALLOC_EVENT		(SDL_USEREVENT)
#define FF_REFRESH_EVENT	(SDL_USEREVENT + 1)
#define FF_QUIT_EVENT		(SDL_USEREVENT + 2)

/* no AV sync correction is done if below the AV sync threshold */
#define AV_SYNC_THRESHOLD 0.01
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

typedef struct VideoPicture {
	SDL_Overlay *bmp;
	int width, height;
	int allocated;
	double pts;
} VideoPicture;

typedef enum AV_Sync_Type{
	AV_SYNC_AUDIO_MASTER,
	AV_SYNC_VIDEO_MASTER,
	AV_SYNC_EXTERNAL_MASTER,
}AVSyncType;

typedef struct VideoState {
	AVFormatContext *pFormatCtx;
	int videoStream,audioStream;

	AVStream *audio_st;
	PacketQueue audioq;
	uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int audio_buf_size;
	unsigned int audio_buf_index;
	AVPacket audio_pkt;
	uint8_t *audio_pkt_data;
	int audio_pkt_size;
	double audio_clock;
	double audio_diff_cum, audio_diff_avg_coef;
    double audio_diff_threshold;
	uint16_t audio_diff_avg_count;

	AVStream *video_st;
	PacketQueue videoq;
	VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int pictq_size, pictq_rIndex, pictq_wIndex;
	double video_clock;
	double video_current_pts;
	uint64_t video_current_pts_time;
	double frame_timer;
	double frame_last_delay, frame_last_pts;

	SDL_mutex *pictq_mutex;
	SDL_cond *pictq_cond;
	SDL_Thread *parse_tid;
	SDL_Thread *video_tid;

	int seek_req;
	int seek_flags;
	int64_t seek_pos;

	AVSyncType av_sync_type;
	char filename[1024];
	int quit;
}VideoState;

typedef enum QueueType {
	QType_Audio,
	QType_Video,
}QueueType;

#define DEFAULT_AV_SYNC_TYPE AV_SYNC_VIDEO_MASTER

#if defined(__MINGW32__) || defined (_MSC_VER)
#undef main /* We don't want SDL to override our main() */
#endif

static uint64_t global_video_pkt_pts = AV_NOPTS_VALUE;
static AVPacket flush_pkt;
static SDL_Surface *g_pScreen;

static void audio_callback(void *userdata, Uint8 *stream, int len);

static int our_get_buffer(AVCodecContext *c, AVFrame *pic)
{
	int ret = avcodec_default_get_buffer(c, pic);
	uint64_t *pts = av_malloc(sizeof(uint64_t));

	*pts = global_video_pkt_pts;
	pic->opaque = pts;
	return ret;
}

static void our_release_buffer(AVCodecContext *c, AVFrame *pic)
{
	if(pic)
		av_freep(&pic->opaque);
	avcodec_default_release_buffer(c, pic);
}

static double get_audio_clock(VideoState *pVideoState)
{
	double pts;
	int hw_buf_size, bytes_per_sec, n;

	pts = pVideoState->audio_clock;
	hw_buf_size = pVideoState->audio_buf_size - pVideoState->audio_buf_index;
	bytes_per_sec = 0;
	n = pVideoState->audio_st->codec->channels * 2;
	if(pVideoState->audio_st)
	{
		bytes_per_sec = pVideoState->audio_st->codec->sample_rate * n;
	}
	if(bytes_per_sec)
	{
		pts -= (double)hw_buf_size / bytes_per_sec;
	}
	return pts;
}

static get_video_clock(VideoState *pVideoState)
{
	double delta;

	delta = (av_gettime() - pVideoState->video_current_pts_time) / 1000000.0;
	return pVideoState->video_current_pts + delta;
}

static get_master_clock(VideoState *pVideoState)
{
	if(AV_SYNC_VIDEO_MASTER == pVideoState->av_sync_type)
	{
		return get_video_clock(pVideoState);
	}
	else if(AV_SYNC_AUDIO_MASTER == pVideoState->av_sync_type)
	{
		return get_audio_clock(pVideoState);
	}
	//else
	//{
	//	return get_externanl_clock(pVideoState);
	//}
}

static int synchronize_audio(VideoState *pVideoState, int16_t *samples, int samples_size, double pts)
{
	int n;
	double ref_clock;

	n = pVideoState->audio_st->codec->channels * 2;
	if(AV_SYNC_AUDIO_MASTER != pVideoState->av_sync_type)
	{
		double diff, avg_diff = 0.0;
		int wanted_size, min_size, max_size, nb_samples;

		ref_clock = get_master_clock(pVideoState);
		diff = get_audio_clock(pVideoState) - ref_clock;

		if(AV_NOSYNC_THRESHOLD > diff)
		{
			// accumulate the diffs
			pVideoState->audio_diff_cum = diff + pVideoState->audio_diff_avg_coef * pVideoState->audio_diff_cum;
			if(AUDIO_DIFF_AVG_NB > pVideoState->audio_diff_avg_count)
			{
				pVideoState->audio_diff_avg_count++;
			}
			else
			{
				avg_diff = pVideoState->audio_diff_cum * (1.0 - pVideoState->audio_diff_avg_coef);
			}
		}
		else
		{
			pVideoState->audio_diff_avg_count = 0;
			pVideoState->audio_diff_cum = 0;
		}

		if(fabs(avg_diff) >= pVideoState->audio_diff_threshold)
		{
			wanted_size = samples_size + ((int)(diff * pVideoState->audio_st->codec->sample_rate) * n);
			min_size = samples_size * ((100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100);
			max_size = samples_size * ((100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100);

			if(wanted_size < min_size)
			{
				wanted_size = min_size;
			}
			else if(wanted_size > max_size)
			{
				wanted_size = max_size;
			}

			if(wanted_size < samples_size)
			{
				samples_size = wanted_size;
			}
			else if(wanted_size > samples_size)
			{
				uint8_t *samples_end, *q;
				int nb;

				nb = samples_size - wanted_size;
				samples_end = (uint8_t *)samples + samples_size - n;
				q = samples_end + n;

				while(0 < nb)
				{
					memcpy(q, samples_end, n);
					q += n;
					nb -= n;
				}

				samples_size = wanted_size;
			}
		}
	}

	return samples_size;
}

static double synchronize_video(VideoState *pVideoState, AVFrame *src_frame, double pts)
{
	double frame_delay;

	if(0 != pts)
	{
		pVideoState->video_clock = pts;
	}
	else
	{
		pts = pVideoState->video_clock;
	}

	frame_delay = av_q2d(pVideoState->video_st->codec->time_base);
	frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
	pVideoState->video_clock += frame_delay;
	return pts;
}

void packet_queue_init(PacketQueue *q)
{
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
	AVPacketList *pktl;

	if(0 > av_dup_packet(pkt) && pkt != &flush_pkt)
		return -1;

	pktl = av_malloc(sizeof(AVPacketList));
	if(!pktl)
		return -1;
	pktl->pkt = *pkt;
	pktl->next = NULL;

	SDL_LockMutex(q->mutex);
	if(!q->last_pkt)
		q->first_pkt = pktl;
	else
		q->last_pkt->next = pktl;

	q->last_pkt = pktl;
	q->nb_packets++;
	q->size += pktl->pkt.size;
	SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);
	return 0;
}

static int packet_queue_get(VideoState *pVideoState, QueueType type, AVPacket *pkt, int block)
{
	PacketQueue *q = NULL;
	AVPacketList *pktl;
	int ret;

	switch(type)
	{
	case QType_Audio:
		q = &pVideoState->audioq;
		break;
	case QType_Video:
		q = &pVideoState->videoq;
		break;
	}
	if(!q)
		return 0;

	SDL_LockMutex(q->mutex);
	for(;;)
	{
		if(pVideoState->quit)
		{
			ret = -1;
			break;
		}

		pktl = q->first_pkt;
		if(pktl)
		{
			q->first_pkt = pktl->next;
			if(!q->first_pkt)
				q->last_pkt = NULL;

			q->nb_packets--;
			q->size -= pktl->pkt.size;
			*pkt = pktl->pkt;
			av_free(pktl);
			ret = 1;
			break;
		}
		else if(!block)
		{
			ret = 0;
			break;
		}
		else
		{
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);

	return ret;
}

static void packet_queue_flush(PacketQueue *q)
{
	AVPacketList *pkt, *pkt1;

	SDL_LockMutex(q->mutex);
	for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1)
	{
		pkt1 = pkt->next;
		av_free_packet(&pkt->pkt);
		av_freep(&pkt);
	}
	q->last_pkt = NULL;
	q->first_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	SDL_UnlockMutex(q->mutex);
}

int img_convert(AVPicture *dst, enum PixelFormat dst_pix_fmt, AVPicture *src, enum PixelFormat src_pix_fmt, int src_width, int src_height)
{
	int w, h;
	SwsContext *pSwsCtx;

	w = src_width;
	h = src_height;

	pSwsCtx = sws_getContext(w, h, src_pix_fmt, w, h, dst_pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
	sws_scale(pSwsCtx, src->data, src->linesize, 0, h, dst->data, dst->linesize);

	// Free SwsContext
	sws_freeContext(pSwsCtx);

	return 0;
}

int Init_SDL(VideoState *pVideoState)
{
	SDL_AudioSpec audioSpec,audioSpecObtained;
	Uint32 sdlFlags = 0;

	sdlFlags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
	if(sdlFlags != SDL_WasInit(sdlFlags))
	{
		if(0 > SDL_Init(sdlFlags))
		{
			fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
			return -1;
		}
	}

	// 初始化SDL视频
	if(pVideoState->video_st)
	{
		g_pScreen = SDL_SetVideoMode(pVideoState->video_st->codec->width, pVideoState->video_st->codec->height, 0, 0);
		if(!g_pScreen)
		{
			fprintf(stderr, "SDL: could not set video mode - exiting\n");
			return -1;
		}
	}

	// 初始化SDL音频
	if(pVideoState->audio_st)
	{
		audioSpec.freq = pVideoState->audio_st->codec->sample_rate;
		audioSpec.format = AUDIO_S16SYS;
		audioSpec.channels = pVideoState->audio_st->codec->channels;
		audioSpec.silence = 0;
		audioSpec.samples = SDL_AUDIO_BUFFER_SIZE;
		audioSpec.callback = audio_callback;
		audioSpec.userdata = pVideoState;
		if(0 > SDL_OpenAudio(&audioSpec,&audioSpecObtained))
		{
			fprintf(stderr, "SDL_OpenAudio failed: %s\n", SDL_GetError());
			return -1;
		}

		SDL_PauseAudio(0);
	}

	return 0;
}

int audio_decode_frame(VideoState *pVideoState, uint8_t *audio_buf, int buf_size)
{
	static AVPacket pkt;
	static uint8_t *audio_pkt_data = NULL;
	static int audio_pkt_size = 0;
	int len1, data_size;

	for(;;)
	{
		while(0 < audio_pkt_size)
		{
			int n;

			data_size = buf_size;
			len1 = avcodec_decode_audio2(pVideoState->audio_st->codec, (int16_t *)audio_buf, &data_size, audio_pkt_data, audio_pkt_size);
			if(0 > len1)
			{
				audio_pkt_size = 0;
				break;
			}
			audio_pkt_data += len1;
			audio_pkt_size -= len1;
			if(0 >= data_size)
			{
				continue;
			}

			//pts = pVideoState->audio_clock;
			//*pts_ptr = pts;
			n = pVideoState->audio_st->codec->channels * 2;
			pVideoState->audio_clock += (double)data_size / (double)(pVideoState->audio_st->codec->sample_rate * n);

			return data_size;
		}

		if(pkt.data)
			av_free_packet(&pkt);

		if(pVideoState->quit)
			return -1;

		if(0 > packet_queue_get(pVideoState, QType_Audio, &pkt, 1))
			return -1;

		if(pkt.data == flush_pkt.data)
		{
			avcodec_flush_buffers(pVideoState->audio_st->codec);
			continue;
		}

		if(AV_NOPTS_VALUE != pkt.pts)
		{
			pVideoState->audio_clock = av_q2d(pVideoState->audio_st->time_base) * pkt.pts;
		}

		audio_pkt_data = pkt.data;
		audio_pkt_size = pkt.size;
	}
}

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
	VideoState *pVideoState = (VideoState *)userdata;
	int len1, audio_size;
	double pts = 0.0;
	//static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
	//static unsigned int audio_buf_size = 0;
	//static unsigned int audio_buf_index = 0;

	while(0 < len)
	{
		if(pVideoState->audio_buf_index >= pVideoState->audio_buf_size)
		{
			audio_size = audio_decode_frame(pVideoState, pVideoState->audio_buf, sizeof(pVideoState->audio_buf));
			if(0 > audio_size)
			{
				pVideoState->audio_buf_size = 1024;
				memset(pVideoState->audio_buf, 0, pVideoState->audio_buf_size);
			}
			else
			{
				audio_size = synchronize_audio(pVideoState, (int16_t *)pVideoState->audio_buf, audio_size, pts);
				pVideoState->audio_buf_size = audio_size;
			}
			pVideoState->audio_buf_index = 0;
		}
		len1 = pVideoState->audio_buf_size - pVideoState->audio_buf_index;
		if(len1 > len)
			len1 = len;
		memcpy(stream, (uint8_t *)pVideoState->audio_buf + pVideoState->audio_buf_index, len1);
		len -= len1;
		stream += len1;
		pVideoState->audio_buf_index += len1;
	}
}

static int decode_thread(void *arg)
{
	VideoState *pVideoState = (VideoState*)arg;
	AVPacket packet;

	for(;;)
	{
		if(pVideoState->quit)
			break;

		// Seek stuff goes here
		if(MAX_AUDIOQ_SIZE < pVideoState->audioq.size ||
			MAX_VIDEOQ_SIZE < pVideoState->videoq.size)
		{
			SDL_Delay(10);
			continue;
		}

		// 是否需要seek
		if(pVideoState->seek_req)
		{
			int stream_index = -1;
			int64_t seek_target = pVideoState->seek_pos;

			if(0 <= pVideoState->videoStream)
				stream_index = pVideoState->videoStream;
			else if(0 <= pVideoState->audioStream)
				stream_index = pVideoState->audioStream;

			if(0 <= stream_index)
			{
				seek_target = av_rescale_q(seek_target, AV_TIME_BASE_Q, pVideoState->pFormatCtx->streams[stream_index]->time_base);
			}

			if(0 > av_seek_frame(pVideoState->pFormatCtx, stream_index, seek_target, pVideoState->seek_flags))
			{
				fprintf(stderr, "%s:error while seeking\n", pVideoState->pFormatCtx->filename);
			}
			else
			{
				if(0 <= pVideoState->audioStream)
				{
					packet_queue_flush(&pVideoState->audioq);
					packet_queue_put(&pVideoState->audioq, &flush_pkt);
				}

				if(0 <= pVideoState->videoStream)
				{
					packet_queue_flush(&pVideoState->videoq);
					packet_queue_put(&pVideoState->videoq, &flush_pkt);
				}
			}
			pVideoState->seek_req = 0;
		}

		if(0 > av_read_frame(pVideoState->pFormatCtx, &packet))
		{
			if(0 == url_ferror(pVideoState->pFormatCtx->pb))
			{
				SDL_Delay(100);		/* no error, wait for user input */
				continue;
			}
			else
				break;
		}

		// Is this a packet from the video stream?
		if(packet.stream_index == pVideoState->videoStream)
		{
			packet_queue_put(&pVideoState->videoq, &packet);
		}
		else if(packet.stream_index == pVideoState->audioStream)
		{
			packet_queue_put(&pVideoState->audioq, &packet);
		}
		else
		{
			av_free_packet(&packet);
		}
	}

	while(!pVideoState->quit)
		SDL_Delay(100);

fail:
	if(1)
	{
		SDL_Event sdlEvent;
		sdlEvent.type = FF_QUIT_EVENT;
		sdlEvent.user.data1 = pVideoState;
		SDL_PushEvent(&sdlEvent);
	}

	return 0;
}

static void alloc_picture(void *userdata)
{
	VideoState *pVideoState = (VideoState *)userdata;
	VideoPicture *vp;

	vp = &pVideoState->pictq[pVideoState->pictq_wIndex];
	if(vp->bmp)
	{
		// We already have one make another, bigger/smaller
		SDL_FreeYUVOverlay(vp->bmp);
	}

	// Allocate a place to put our YUV image on that screen
	vp->bmp = SDL_CreateYUVOverlay(pVideoState->video_st->codec->width,
									pVideoState->video_st->codec->height,
									SDL_YV12_OVERLAY,
									g_pScreen);
	vp->width = pVideoState->video_st->codec->width;
	vp->height = pVideoState->video_st->codec->height;

	SDL_LockMutex(pVideoState->pictq_mutex);
	vp->allocated = 1;
	SDL_CondSignal(pVideoState->pictq_cond);
	SDL_UnlockMutex(pVideoState->pictq_mutex);
}

static int queue_picture(VideoState *pVideoState, AVFrame *pFrame, double pts)
{
	VideoPicture *vp;
	int dst_pix_fmt;
	AVPicture pict;

	SDL_LockMutex(pVideoState->pictq_mutex);
	while(VIDEO_PICTURE_QUEUE_SIZE <= pVideoState->pictq_size && !pVideoState->quit)
		SDL_CondWait(pVideoState->pictq_cond, pVideoState->pictq_mutex);
	SDL_UnlockMutex(pVideoState->pictq_mutex);

	if(pVideoState->quit)
		return -1;

	// write index is set to 0 initially
	vp = &pVideoState->pictq[pVideoState->pictq_wIndex];

	if(!vp->bmp ||
		vp->width != pVideoState->video_st->codec->width ||
		vp->height != pVideoState->video_st->codec->height)
	{
		SDL_Event sdlEvent;

		vp->allocated = 0;

		sdlEvent.type = FF_ALLOC_EVENT;
		sdlEvent.user.data1 = pVideoState;
		SDL_PushEvent(&sdlEvent);

		SDL_LockMutex(pVideoState->pictq_mutex);
		while(!vp->allocated && !pVideoState->quit)
			SDL_CondWait(pVideoState->pictq_cond, pVideoState->pictq_mutex);
		SDL_UnlockMutex(pVideoState->pictq_mutex);

		if(pVideoState->quit)
			return -1;
	}

	if(vp->bmp)
	{
		SDL_LockYUVOverlay(vp->bmp);

		dst_pix_fmt = PIX_FMT_YUV420P;

		pict.data[0] = vp->bmp->pixels[0];
		pict.data[1] = vp->bmp->pixels[2];
		pict.data[2] = vp->bmp->pixels[1];

		pict.linesize[0] = vp->bmp->pitches[0];
		pict.linesize[1] = vp->bmp->pitches[2];
		pict.linesize[2] = vp->bmp->pitches[1];

		// Convert the image into YUV format that SDL uses
		img_convert(&pict, dst_pix_fmt, (AVPicture *)pFrame, pVideoState->video_st->codec->pix_fmt,
					pVideoState->video_st->codec->width, pVideoState->video_st->codec->height);

		vp->pts = pts;
		SDL_UnlockYUVOverlay(vp->bmp);

		if(VIDEO_PICTURE_QUEUE_SIZE == ++pVideoState->pictq_wIndex)
			pVideoState->pictq_wIndex = 0;

		SDL_LockMutex(pVideoState->pictq_mutex);
		pVideoState->pictq_size++;
		SDL_UnlockMutex(pVideoState->pictq_mutex);
	}

	return 0;
}

static int video_thread(void *arg)
{
	VideoState *pVideoState = (VideoState *)arg;
	AVPacket pkt, *packet = &pkt;
	AVFrame *pFrame;
	int len, frameFinished;
	double pts;

	pFrame = avcodec_alloc_frame();

	for(;;)
	{
		if(0 > packet_queue_get(pVideoState, QType_Video,packet, 1))
		{
			// means we quit getting packets
			break;
		}

		if(packet->data == flush_pkt.data)
		{
			avcodec_flush_buffers(pVideoState->video_st->codec);
			continue;
		}

		pts = 0;

		// Save global pts to be stored in pFrame in first call
		global_video_pkt_pts = packet->pts;

		// Decode video frame
		len = avcodec_decode_video(pVideoState->video_st->codec, pFrame, &frameFinished, packet->data, packet->size);
		if(AV_NOPTS_VALUE == packet->dts && pFrame->opaque &&
			AV_NOPTS_VALUE != *(uint64_t *)pFrame->opaque)
		{
			pts = *(uint64_t *)pFrame->opaque;
		}
		else if(AV_NOPTS_VALUE != packet->dts)
		{
			pts = packet->dts;
		}
		else
		{
			pts = 0;
		}
		pts *= av_q2d(pVideoState->video_st->time_base);

		// Did we get a video frame?
		if(frameFinished)
		{
			pts = synchronize_video(pVideoState, pFrame, pts);
			if(0 > queue_picture(pVideoState, pFrame, pts))
				break;
		}

		av_free_packet(packet);
	}

	av_free(pFrame);

	return 0;
}

int stream_component_open(VideoState *pVideoState, int stream_index)
{
	AVFormatContext *pFormatCtx = pVideoState->pFormatCtx;
	AVCodecContext *pCodecCtx;
	AVCodec *pCodec;
	SDL_AudioSpec audioSpec,audioSpecObtained;

	if(0 > stream_index || stream_index >= pFormatCtx->nb_streams)
		return -1;

	// Get a pointer to the codec context for the audio\video stream
	pCodecCtx = pFormatCtx->streams[stream_index]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if( !pCodec || (0 > avcodec_open(pCodecCtx, pCodec)) )
	{
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}

	pCodecCtx->get_buffer = our_get_buffer;
	pCodecCtx->release_buffer = our_release_buffer;

	switch(pCodecCtx->codec_type)
	{
	case CODEC_TYPE_AUDIO:
		pVideoState->audioStream = stream_index;
		pVideoState->audio_st = pFormatCtx->streams[stream_index];
		pVideoState->audio_buf_size = 0;
		pVideoState->audio_buf_index = 0;
        pVideoState->audio_diff_threshold = 2.0 * SDL_AUDIO_BUFFER_SIZE / pCodecCtx->sample_rate;
		memset(&pVideoState->audio_pkt, 0, sizeof(pVideoState->audio_pkt));
		packet_queue_init(&pVideoState->audioq);
		break;
	case CODEC_TYPE_VIDEO:
		pVideoState->videoStream = stream_index;
		pVideoState->video_st = pFormatCtx->streams[stream_index];
		pVideoState->video_current_pts_time = av_gettime();
		pVideoState->frame_timer = (double)av_gettime() / 1000000.0;
		pVideoState->frame_last_delay = 40e-3;
		packet_queue_init(&pVideoState->videoq);
		break;
	default:
		break;
	}
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque)
{
	SDL_Event sdlEvent;
	sdlEvent.type = FF_REFRESH_EVENT;
	sdlEvent.user.data1 = opaque;
	SDL_PushEvent(&sdlEvent);
	return 0;
}

static void schedule_refresh(VideoState *pVideoState, int delay)
{
	SDL_AddTimer(delay, sdl_refresh_timer_cb, pVideoState);
}

static void video_display(VideoState *pVideoState)
{
	SDL_Rect rect;
	VideoPicture *vp;
	AVPicture pict;
	float aspect_ratio;
	int w, h, x, y;
	int i;

	vp = &pVideoState->pictq[pVideoState->pictq_rIndex];
	if(vp->bmp)
	{
		if(0 == pVideoState->video_st->codec->sample_aspect_ratio.num)
		{
			aspect_ratio = 0;
		}
		else
		{
			aspect_ratio = av_q2d(pVideoState->video_st->codec->sample_aspect_ratio) *
								pVideoState->video_st->codec->width / pVideoState->video_st->codec->height;
		}

		if(0.0 >= aspect_ratio)
		{
			aspect_ratio = (float)pVideoState->video_st->codec->width / (float)pVideoState->video_st->codec->height;
		}
		h = g_pScreen->h;
		w = (int)(h * aspect_ratio) & (~3);
		if(w > g_pScreen->w)
		{
			w = g_pScreen->w;
			h = (int)(w / aspect_ratio) & (~3);
		}

		x = (g_pScreen->w - w) / 2;
		y = (g_pScreen->h - h) / 2;

		rect.x = x;
		rect.y = y;
		rect.w = w;
		rect.h = h;
		SDL_DisplayYUVOverlay(vp->bmp, &rect);
	}
}

static void video_refresh_timer(void *userdata)
{
	VideoState *pVideoState = (VideoState *)userdata;
	VideoPicture *vp;
	double actual_delay, delay, sync_threshold, ref_clock, diff;

	if(pVideoState->video_st)
	{
		if(0 == pVideoState->pictq_size)
		{
			schedule_refresh(pVideoState, 1);
		}
		else
		{
			vp = &pVideoState->pictq[pVideoState->pictq_rIndex];

			pVideoState->video_current_pts = vp->pts;
			pVideoState->video_current_pts_time = av_gettime();
			delay = vp->pts - pVideoState->frame_last_pts;
			if(0 >= delay || 1.0 <= delay)
			{
				delay = pVideoState->frame_last_delay;
			}
			pVideoState->frame_last_delay = delay;
			pVideoState->frame_last_pts = vp->pts;

			if(AV_SYNC_VIDEO_MASTER != pVideoState->av_sync_type)
			{
				ref_clock = get_audio_clock(pVideoState);
				diff = vp->pts - ref_clock;

				sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
				if(AV_NOSYNC_THRESHOLD > fabs(diff))
				{
					if(diff <= -sync_threshold)
					{
						delay = 0;
					}
					else if(diff >= sync_threshold)
					{
						delay = 2 * delay;
					}
				}
			}

			pVideoState->frame_timer += delay;
			actual_delay = pVideoState->frame_timer - (av_gettime() / 1000000.0);
			if(0.010 > actual_delay)
			{
				actual_delay = 0.010;
			}

			schedule_refresh(pVideoState, (int)(actual_delay * 1000 + 0.5));
			video_display(pVideoState);

			if(VIDEO_PICTURE_QUEUE_SIZE == ++pVideoState->pictq_rIndex)
				pVideoState->pictq_rIndex = 0;

			SDL_LockMutex(pVideoState->pictq_mutex);
			pVideoState->pictq_size--;
			SDL_CondSignal(pVideoState->pictq_cond);
			SDL_UnlockMutex(pVideoState->pictq_mutex);
		}
	}
	else
	{
		schedule_refresh(pVideoState, 100);
	}
}

static void stream_seek(VideoState *pVideoState, int64_t pos, int rel)
{
	if(!pVideoState->seek_req)
	{
		pVideoState->seek_pos = pos;
		pVideoState->seek_flags = rel < 0 ? AVSEEK_FLAG_BACKWARD : 0;
		pVideoState->seek_req = 1;
	}
}

int main(int argc, char **argv)
{
	VideoState *pVideoState;
	SDL_Event sdlEvent;
	int i;
	int ret = -1;

	pVideoState = av_mallocz(sizeof(VideoState));
	if(!pVideoState)
		goto exit;

	pVideoState->av_sync_type = DEFAULT_AV_SYNC_TYPE;

	/*******************************************/
	/*  注册所有文件格式和编解码器的库         */
	/*******************************************/
	av_register_all();
	
	/*******************************************/
	/*  打开文件 - 检测文件头部                */
	/*******************************************/
	// Open video file
	if( 0 != av_open_input_file(&pVideoState->pFormatCtx, argv[1], NULL, 0, NULL) )
		return -1;		// Couldn't open file

	/*******************************************/
	/*  打开文件 - 检测文件中流的信息          */
	/*******************************************/
	// Retrieve stream information
	if( 0 > av_find_stream_info(pVideoState->pFormatCtx) )
		return -1;		// Couldn't find stream information

	// Dump information about file onto standard error
	dump_format(pVideoState->pFormatCtx, 0, argv[1], 0);

	/*******************************************/
	/*  查找音视频流                           */
	/*******************************************/
	for(i = 0; i < pVideoState->pFormatCtx->nb_streams; i++)
	{
		stream_component_open(pVideoState, i);
	}

	/*******************************************/
	/*  初始化SDL                              */
	/*******************************************/
	if(0 > Init_SDL(pVideoState))
		return -1;

	// 创建"清空包"
	av_init_packet(&flush_pkt);
	flush_pkt.data = "FLUSH";

	i = sizeof(pVideoState->filename) > (strlen(argv[1]) + 1) ? (strlen(argv[1]) + 1) : sizeof(pVideoState->filename);
	strncpy(pVideoState->filename, argv[1], i);
	pVideoState->pictq_mutex = SDL_CreateMutex();
	pVideoState->pictq_cond = SDL_CreateCond();

	schedule_refresh(pVideoState, 40);
	pVideoState->parse_tid = SDL_CreateThread(decode_thread, pVideoState);
	pVideoState->video_tid = SDL_CreateThread(video_thread, pVideoState);

	if(!pVideoState->parse_tid)
		goto exit;

	for(;;)
	{
		int incr;

		SDL_WaitEvent(&sdlEvent);
		switch(sdlEvent.type)
		{
		case FF_ALLOC_EVENT:
			alloc_picture(sdlEvent.user.data1);
			break;
		case FF_REFRESH_EVENT:
			video_refresh_timer(sdlEvent.user.data1);
			break;
		case FF_QUIT_EVENT:
			pVideoState->quit = 1;
			exit(0);
			break;
		case SDL_KEYDOWN:
			switch(sdlEvent.key.keysym.sym)
			{
			case SDLK_LEFT:
				incr = -10.0;
				goto do_seek;
			case SDLK_RIGHT:
				incr = 10.0;
				goto do_seek;
			case SDLK_UP:
				incr = 60.0;
				goto do_seek;
			case SDLK_DOWN:
				incr = -60.0;
				goto do_seek;

do_seek:
				if(pVideoState)
				{
					int pos = get_master_clock(pVideoState);
					pos += incr;
					stream_seek(pVideoState, (int64_t)(pos * AV_TIME_BASE), incr);
				}
				break;
			default:
				break;
			}
			break;
		}
	}

	ret = 0;
exit:
	// Close the codec
	if(pVideoState->video_st->codec)
		avcodec_close(pVideoState->video_st->codec);
	if(pVideoState->audio_st->codec)
		avcodec_close(pVideoState->audio_st->codec);

	// Close the video file
	if(pVideoState->pFormatCtx)
		av_close_input_file(pVideoState->pFormatCtx);

	av_free(pVideoState);

	return ret;
}
