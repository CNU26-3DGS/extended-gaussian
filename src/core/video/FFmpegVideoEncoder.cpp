/*
 * Copyright (C) 2020, Inria
 * GRAPHDECO research group, https://team.inria.fr/graphdeco
 * All rights reserved.
 *
 * This software is free for non-commercial, research and evaluation use 
 * under the terms of the LICENSE.md file.
 *
 * For inquiries contact sibr@inria.fr and/or George.Drettakis@inria.fr
 */


#include "FFmpegVideoEncoder.hpp"

#ifndef HEADLESS
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#endif

#define QQ(rat) (rat.num/(double)rat.den)

// Disable ffmpeg deprecation warning.
#pragma warning(disable : 4996)

namespace sibr {

	bool FFVideoEncoder::ffmpegInitDone = false;

	FFVideoEncoder::FFVideoEncoder(
		const std::string & _filepath,
		double _fps,
		const sibr::Vector2i & size,
		bool forceResize
	) : filepath(_filepath), fps(_fps), _forceResize(forceResize)
	{
#ifndef HEADLESS
		/** Init FFMPEG. Recent libavformat/libavcodec versions self-register. */
		if (!ffmpegInitDone) {
			SIBR_LOG << "[FFMPEG] Initializing." << std::endl;
			ffmpegInitDone = true;
		}
		
		sibr::Vector2i sizeFix = size;
		bool hadToFix = false;
		if(sizeFix[0]%2 != 0) {
			sizeFix[0] -= 1;
			hadToFix = true;
		}
		if (sizeFix[1] % 2 != 0) {
			sizeFix[1] -= 1;
			hadToFix = true;
		}
		if(hadToFix) {
			SIBR_WRG << "Non-even video dimensions, resized to " << sizeFix[0] << "x" << sizeFix[1] << "." << std::endl;
			_forceResize = true;
		}
		
		init(sizeFix);
#endif
	}

	bool FFVideoEncoder::isFine() const
	{
		return initWasFine;
	}

	void FFVideoEncoder::close()
	{
#ifndef HEADLESS
		if (headerWritten && pFormatCtx) {
			if (!encode(NULL)) {
				SIBR_WRG << "[FFMPEG] Failed to flush encoder." << std::endl;
			}
			if (av_write_trailer(pFormatCtx) < 0) {
				SIBR_WRG << "[FFMPEG] Can not av_write_trailer " << std::endl;
			}
			headerWritten = false;
		}

		if (pCodecCtx) {
			avcodec_free_context(&pCodecCtx);
		}
		if (frameYUV) {
			av_frame_free(&frameYUV);
		}
		if (pkt) {
			av_packet_free(&pkt);
		}
		if (pFormatCtx) {
			if (pFormatCtx->pb) {
				avio_closep(&pFormatCtx->pb);
			}
			avformat_free_context(pFormatCtx);
			pFormatCtx = NULL;
		}

		fmt = NULL;
		video_st = NULL;
		pCodec = NULL;

		needFree = false;
#endif
	}

	FFVideoEncoder::~FFVideoEncoder()
	{
		if (needFree) {
			close();
		}

	}

	void FFVideoEncoder::init(const sibr::Vector2i & size)
	{
#ifndef HEADLESS
		w = size[0];
		h = size[1];

		auto out_file = filepath.c_str();


		pFormatCtx = avformat_alloc_context();
		if (!pFormatCtx) {
			SIBR_WRG << "[FFMPEG] Could not allocate format context." << std::endl;
			return;
		}

		fmt = av_guess_format(NULL, out_file, NULL);
		if (!fmt) {
			SIBR_WRG << "[FFMPEG] Could not guess output format for " << filepath << std::endl;
			close();
			return;
		}
		pFormatCtx->oformat = const_cast<AVOutputFormat*>(fmt);

		const bool isH264 = pFormatCtx->oformat->video_codec == AV_CODEC_ID_H264;
		if(isH264){
			SIBR_LOG << "[FFMPEG] Found H264 codec." << std::endl;
		} else {
			SIBR_LOG << "[FFMPEG] Found codec with ID " << pFormatCtx->oformat->video_codec << " (not H264)." << std::endl;
		}
		
		if (avio_open(&pFormatCtx->pb, out_file, AVIO_FLAG_READ_WRITE) < 0) {
			SIBR_WRG << "[FFMPEG] Could not open file " << filepath << std::endl;
			close();
			return;
		}

		pCodec = avcodec_find_encoder(pFormatCtx->oformat->video_codec);
		if (!pCodec) {
			SIBR_WRG << "[FFMPEG] Could not find codec." << std::endl;
			close();
			return;
		}

		video_st = avformat_new_stream(pFormatCtx, pCodec);

		if (video_st == NULL) {
			SIBR_WRG << "[FFMPEG] Could not create stream." << std::endl;
			close();
			return;
		}

		pCodecCtx = avcodec_alloc_context3(pCodec);
		if (!pCodecCtx) {
			SIBR_WRG << "[FFMPEG] Could not allocate codec context." << std::endl;
			close();
			return;
		}
		pCodecCtx->codec_id = fmt->video_codec;
		pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
		pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
		pCodecCtx->width = w;
		pCodecCtx->height = h;
		pCodecCtx->gop_size = 10;
		pCodecCtx->time_base.num = 1;
		pCodecCtx->time_base.den = (int)std::round(fps);
		video_st->time_base = pCodecCtx->time_base;

		// Required for the header to be well-formed and compatible with Powerpoint/MediaPlayer/...
		if (pFormatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
			pCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}

		//H.264 specific options.
		AVDictionary *param = 0;
		if (pCodecCtx->codec_id == AV_CODEC_ID_H264) {
			av_dict_set(&param, "preset", "slow", 0);
			av_dict_set(&param, "tune", "zerolatency", 0);
		}

		av_dump_format(pFormatCtx, 0, out_file, 1);

		int res = avcodec_open2(pCodecCtx, pCodec, &param);
		av_dict_free(&param);
		if(res < 0){
			SIBR_WRG << "[FFMPEG] Failed to open encoder, error: " << res << std::endl;
			close();
			return;
		}

		res = avcodec_parameters_from_context(video_st->codecpar, pCodecCtx);
		if (res < 0) {
			SIBR_WRG << "[FFMPEG] Failed to copy encoder parameters, error: " << res << std::endl;
			close();
			return;
		}
		// Write the file header.
		res = avformat_write_header(pFormatCtx, NULL);
		if (res < 0) {
			SIBR_WRG << "[FFMPEG] Failed to write header, error: " << res << std::endl;
			close();
			return;
		}
		headerWritten = true;

		// Prepare the scratch frame.
		frameYUV = av_frame_alloc();
		frameYUV->format = (int)pCodecCtx->pix_fmt;
		frameYUV->width = w;
		frameYUV->height = h;
		frameYUV->linesize[0] = w;
		frameYUV->linesize[1] = w / 2;
		frameYUV->linesize[2] = w / 2;

		yuSize[0] = frameYUV->linesize[0] * h;
		yuSize[1] = frameYUV->linesize[1] * h / 2;

		pkt = av_packet_alloc();

		initWasFine = true;
		needFree = true;
#endif
	}


	bool FFVideoEncoder::operator<<(cv::Mat frame)
	{
#ifndef HEADLESS
		if (!video_st) {
			return false;
		}
		cv::Mat local;
		if (frame.cols != w || frame.rows != h) {
			if(_forceResize) {
				cv::resize(frame, local, cv::Size(w,h));
			} else {
				SIBR_WRG << "[FFMPEG] Frame doesn't have the same dimensions as the video." << std::endl;
				return false;
			}
		} else {
			local = frame;
		}

		cv::cvtColor(local, cvFrameYUV, cv::COLOR_BGR2YUV_I420);
		frameYUV->data[0] = cvFrameYUV.data;
		frameYUV->data[1] = frameYUV->data[0] + yuSize[0];
		frameYUV->data[2] = frameYUV->data[1] + yuSize[1];

		frameYUV->pts = frameCount;
		++frameCount;

		return encode(frameYUV);
#else
		SIBR_ERR << "Not supported in headless" << std::endl;
		return false;
#endif
	}

	bool FFVideoEncoder::operator<<(const sibr::ImageRGB & frame){
		return (*this)<<(frame.toOpenCVBGR());
	}

#ifndef HEADLESS
	bool FFVideoEncoder::encode(AVFrame * frame)
	{
		int ret = avcodec_send_frame(pCodecCtx, frame);
		if (ret < 0) {
			SIBR_WRG << "[FFMPEG] Failed to encode frame." << std::endl;
			return false;
		}

		while (ret >= 0) {
			ret = avcodec_receive_packet(pCodecCtx, pkt);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				return true;
			}
			if (ret < 0) {
				SIBR_WRG << "[FFMPEG] Failed to receive encoded packet." << std::endl;
				return false;
			}

			av_packet_rescale_ts(pkt, pCodecCtx->time_base, video_st->time_base);
			pkt->stream_index = video_st->index;
			ret = av_interleaved_write_frame(pFormatCtx, pkt);
			av_packet_unref(pkt);
			if (ret < 0) {
				SIBR_WRG << "[FFMPEG] Failed to write encoded packet." << std::endl;
				return false;
			}
		}

		return true;
	}
#endif

}
