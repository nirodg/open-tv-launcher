/* ui/media.c — FFmpeg/libav decode for the Video app player.
 * Decodes forward from the last timestamp; a backward or >2 s forward jump
 * seeks to the previous keyframe first. Frames convert straight to BGRA
 * (little-endian XRGB8888, the surface layout) so no per-pixel repacking. */
#include "media.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

double media_probe_duration(const char *path)
{
	AVFormatContext *ctx = NULL;
	if (avformat_open_input(&ctx, path, NULL, NULL) != 0)
		return 0;
	double d = 0;
	if (avformat_find_stream_info(ctx, NULL) >= 0 && ctx->duration > 0)
		d = ctx->duration / (double)AV_TIME_BASE;
	avformat_close_input(&ctx);
	return d;
}

struct Media {
	AVFormatContext *ic;
	const AVCodec *codec;
	AVCodecContext *cdc;
	AVFrame *frame;
	AVFrame *rgb;
	AVPacket *pkt;
	struct SwsContext *sws;
	int vstream;
	double last_ts;   /* timestamp of the returned frame */
	bool have_frame;
	bool eof;
	uint32_t *pack;   /* fallback buffer when linesize ≠ w*4 */
	long pack_n;
};

Media *media_open(const char *path)
{
	Media *m = calloc(1, sizeof *m);
	if (!m)
		return NULL;
	if (avformat_open_input(&m->ic, path, NULL, NULL) != 0)
		goto fail;
	if (avformat_find_stream_info(m->ic, NULL) < 0)
		goto fail;
	m->vstream = av_find_best_stream(m->ic, AVMEDIA_TYPE_VIDEO, -1, -1,
					 &m->codec, 0);
	if (m->vstream < 0 || !m->codec)
		goto fail;
	m->cdc = avcodec_alloc_context3(m->codec);
	if (!m->cdc)
		goto fail;
	if (avcodec_parameters_to_context(m->cdc,
					  m->ic->streams[m->vstream]->codecpar) < 0)
		goto fail;
	/* frame pts is in the stream's timebase */
	m->cdc->pkt_timebase = m->ic->streams[m->vstream]->time_base;
	if (avcodec_open2(m->cdc, m->codec, NULL) < 0)
		goto fail;
	m->frame = av_frame_alloc();
	m->rgb = av_frame_alloc();
	m->pkt = av_packet_alloc();
	if (!m->frame || !m->rgb || !m->pkt)
		goto fail;
	m->last_ts = -1;
	return m;
fail:
	media_close(m);
	return NULL;
}

void media_close(Media *m)
{
	if (!m)
		return;
	if (m->sws)
		sws_freeContext(m->sws);
	if (m->pkt)
		av_packet_free(&m->pkt);
	if (m->rgb)
		av_frame_free(&m->rgb);
	if (m->frame)
		av_frame_free(&m->frame);
	if (m->cdc)
		avcodec_free_context(&m->cdc);
	if (m->ic)
		avformat_close_input(&m->ic);
	free(m->pack);
	free(m);
}

int media_w(const Media *m) { return m && m->rgb ? m->rgb->width : 0; }
int media_h(const Media *m) { return m && m->rgb ? m->rgb->height : 0; }

/* convert the decoded frame into BGRA (the surface's XRGB8888 layout) */
static int to_rgb(Media *m)
{
	int w = m->frame->width, h = m->frame->height;
	if (!m->rgb->data[0] || m->rgb->width != w || m->rgb->height != h) {
		av_frame_unref(m->rgb);
		m->rgb->format = AV_PIX_FMT_BGRA;
		m->rgb->width = w;
		m->rgb->height = h;
		if (av_frame_get_buffer(m->rgb, 0) < 0)
			return -1;
	}
	m->sws = sws_getCachedContext(m->sws, w, h, m->frame->format,
				      w, h, AV_PIX_FMT_BGRA,
				      SWS_BILINEAR, NULL, NULL, NULL);
	if (!m->sws)
		return -1;
	sws_scale(m->sws, (const uint8_t *const *)m->frame->data,
		  m->frame->linesize, 0, h, m->rgb->data, m->rgb->linesize);
	return 0;
}

/* decode packets until a frame with pts ≥ ts is ready (or EOF) */
static bool decode_to(Media *m, double ts)
{
	while (!m->eof) {
		int rc = avcodec_receive_frame(m->cdc, m->frame);
		if (rc == 0) {
			double pts = m->frame->pts *
				av_q2d(m->cdc->pkt_timebase);
			if (pts >= ts - 0.02) {
				if (to_rgb(m) == 0) {
					m->last_ts = pts;
					m->have_frame = true;
					return true;
				}
			}
			av_frame_unref(m->frame);
			continue;
		}
		if (rc != AVERROR(EAGAIN))
			return m->have_frame; /* error or fully drained */
		/* feed the decoder */
		while (true) {
			int pr = av_read_frame(m->ic, m->pkt);
			if (pr < 0) {
				avcodec_send_packet(m->cdc, NULL); /* flush */
				m->eof = true;
				break;
			}
			if (m->pkt->stream_index == m->vstream) {
				avcodec_send_packet(m->cdc, m->pkt);
				av_packet_unref(m->pkt);
				break;
			}
			av_packet_unref(m->pkt);
		}
	}
	return m->have_frame;
}

const uint32_t *media_frame(Media *m, double ts)
{
	if (!m)
		return NULL;
	if (m->last_ts >= 0 && (ts < m->last_ts || ts > m->last_ts + 2.0)) {
		/* seek to the keyframe before the target */
		avcodec_flush_buffers(m->cdc);
		int64_t target = AV_TIME_BASE * (ts - 0.25);
		if (target < 0)
			target = 0;
		if (av_seek_frame(m->ic, m->vstream, target,
				  AVSEEK_FLAG_BACKWARD) < 0)
			return NULL;
		m->eof = false;
		m->have_frame = false;
		m->last_ts = -1;
	}
	if (!decode_to(m, ts))
		return NULL;
	int w = m->rgb->width;
	if (m->rgb->linesize[0] == w * 4)
		return (const uint32_t *)m->rgb->data[0];
	/* repack only when the buffer has padding */
	long need = (long)w * m->rgb->height;
	if (m->pack_n < need) {
		free(m->pack);
		m->pack = malloc(need * 4);
		m->pack_n = need;
		if (!m->pack)
			return NULL;
	}
	for (int y = 0; y < m->rgb->height; y++)
		memcpy(m->pack + (long)y * w,
		       m->rgb->data[0] + (long)y * m->rgb->linesize[0],
		       (size_t)w * 4);
	return m->pack;
}
