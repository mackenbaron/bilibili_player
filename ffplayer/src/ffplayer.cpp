
#include <QtCore>
#include <QMediaService>

#include "ffmpeg.hpp"
#include "ffplayer.hpp"
#include "ffplayer_p.hpp"
#include "qio2avio.hpp"



FFPlayerPrivate::FFPlayerPrivate(FFPlayer* q)
	: q_ptr(q)
{
	avcodec_register_all();
	av_register_all();

	avformat_network_init();

}

FFPlayerPrivate::~FFPlayerPrivate()
{
	delete adecoder;
	delete vdecoder;
	delete demuxer;
	delete avsync;

	avformat_network_deinit();
}

FFPlayer::FFPlayer()
 : d_ptr(new FFPlayerPrivate(this))
{
	demux_thread.start();
}

FFPlayer::~FFPlayer()
{
	d_ptr->vdecoder->stop();
	d_ptr->demuxer->stop();
	d_ptr->avsync->stop();
	demux_thread.quit();

	demux_thread.wait();


    delete d_ptr;
}

static int stream_index(enum AVMediaType type, AVFormatContext *ctx)
{
	unsigned int i;

	for (i = 0; (unsigned int) i < ctx->nb_streams; i++)
		if (ctx->streams[i]->codec->codec_type == type)
			return i;
	return -1;
}

void FFPlayer::play(std::string url)
{
	AVFormatContext* avformat_ctx = nullptr;//avformat_alloc_context();

	avformat_open_input(&avformat_ctx, url.c_str(), NULL, NULL);

	d_func()->avformat_ctx.reset(avformat_ctx, avformat_free_context);

	avformat_find_stream_info(avformat_ctx, NULL);

	av_dump_format(avformat_ctx, 0, NULL, 0);

	// 获取 video index 和 audio index

	int video_index, audio_index;

	video_index = stream_index(AVMEDIA_TYPE_VIDEO, avformat_ctx);
	audio_index = stream_index(AVMEDIA_TYPE_AUDIO, avformat_ctx);

	// 开启线程，正式进入播放

	// 创建 demux 对象.

	d_func()->demuxer = new QDemuxer(this);
 	d_func()->vdecoder = new QVDecoder(this, video_index);
 	d_func()->adecoder = new QADecoder(this, audio_index);
	d_func()->avsync = new QAudioVideoSync(this);

	d_func()->demuxer->moveToThread(&demux_thread);
	d_func()->vdecoder->moveToThread(&demux_thread);
	d_func()->adecoder->moveToThread(&demux_thread);

	connect(d_func()->demuxer, SIGNAL(frame_readed(AVPacket*)), d_func()->vdecoder, SLOT(put_one_frame(AVPacket*)));
	connect(d_func()->demuxer, SIGNAL(frame_readed(AVPacket*)), d_func()->adecoder, SLOT(put_one_frame(AVPacket*)));

	connect(d_func()->vdecoder, SIGNAL(videoframe_decoded(const QVideoFrame&)), d_func()->avsync, SLOT(sync_frame(const QVideoFrame&)), Qt::DirectConnection);
	connect(d_func()->adecoder, SIGNAL(audioframe_decoded(const QAudioBuffer&)), d_func()->avsync, SLOT(sync_audio(const QAudioBuffer&)), Qt::DirectConnection);

	connect(d_func()->avsync, SIGNAL(render_frame(const QVideoFrame&)), this, SLOT(render_frame(const QVideoFrame&)), Qt::DirectConnection);
	d_func()->demuxer->start();
}

void FFPlayer::render_frame(const QVideoFrame&f)
{
	if (m_vout)
		m_vout->present(f);
}

void FFPlayer::play()
{
	// 从 play list 里获取url

	m_playlist->setCurrentIndex(0);

	auto url = m_playlist->currentMedia().canonicalUrl().toString().toStdString();

	play(url);

	// 打开 url

	// TODO
	// 缓冲到一定的数据量

	// 开始播放

	// 播放过程中监控剩余时间，剩余时间剩下 30s 的时候开始缓冲下一个
}

void FFPlayer::start_decode(QIODevice* input)
{
	auto avio =  QIO2AVIO(input);

	auto avformat_ctx = avformat_alloc_context();

	AVInputFormat* fmt = nullptr;

	av_probe_input_buffer(avio.avio_context(), &fmt, "", 0, 0, 0);

	avformat_open_input(&avformat_ctx, NULL, fmt, NULL);

	avformat_find_stream_info(avformat_ctx, NULL);

	av_dump_format(avformat_ctx, 0, NULL, 0);
}