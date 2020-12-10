#include	<windows.h>
#include	<WinUser.h>
#include	<vfw.h>
#include    <limits.h>
#include    <malloc.h>
#include	"resource.h"

extern "C" {
#include <libavcodec/avcodec.h> 
#include <libavutil/imgutils.h> 
#include <libavutil/mem.h>
#include <libavformat/avformat.h> 
#include <libswscale/swscale.h> 
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "swscale.lib")

#include	"input.h"
#pragma comment(lib, "user32.lib")
#pragma comment(lib,"vfw32.lib")

#include <atlstr.h>
#include <inttypes.h>
#include <string>
#include <map>
#include <vector>
#include <chrono>

#define VIDEO_EXT "*.avi;*.mov;*.wmv;*.mp4;*.webm;*.mkv;*.flv;*.264;*.mpeg;*.ts;*.mts;*.m2ts;"
#define AUDIO_EXT "*.mp3;*.ogg;*.wav;*.aac;*.wma;*.m4a;*.webm;*.opus;"
#define VERSION "0.1"
#define BUILD_NUM "1"


//---------------------------------------------------------------------
//		入力プラグイン構造体定義
//---------------------------------------------------------------------
INPUT_PLUGIN_TABLE input_plugin_table = {
	INPUT_PLUGIN_FLAG_VIDEO|INPUT_PLUGIN_FLAG_AUDIO,	//	フラグ
														//	INPUT_PLUGIN_FLAG_VIDEO	: 画像をサポートする
														//	INPUT_PLUGIN_FLAG_AUDIO	: 音声をサポートする
	"FFmpeg Decoder from Ropimer",						//	プラグインの名前
	"Video File (" VIDEO_EXT ")\0" VIDEO_EXT "\0"		//	入力ファイルフィルタ
	"Audio File (" AUDIO_EXT ")\0" AUDIO_EXT "\0"		//	入力ファイルフィルタ
	"Any File (*.*)\0*.*\0",							//	入力ファイルフィルタ
	"FFmpeg Decoder from Ropimer v" VERSION " Build " BUILD_NUM " by Kusaanko",		//	プラグインの情報
	func_init,											//	DLL開始時に呼ばれる関数へのポインタ (NULLなら呼ばれません)
	func_exit,											//	DLL終了時に呼ばれる関数へのポインタ (NULLなら呼ばれません)
	func_open,											//	入力ファイルをオープンする関数へのポインタ
	func_close,											//	入力ファイルをクローズする関数へのポインタ
	func_info_get,										//	入力ファイルの情報を取得する関数へのポインタ
	func_read_video,									//	画像データを読み込む関数へのポインタ
	func_read_audio,									//	音声データを読み込む関数へのポインタ
	func_is_keyframe,									//	キーフレームか調べる関数へのポインタ (NULLなら全てキーフレーム)
	func_config,										//	入力設定のダイアログを要求された時に呼ばれる関数へのポインタ (NULLなら呼ばれません)
};


//---------------------------------------------------------------------
//		入力プラグイン構造体のポインタを渡す関数
//---------------------------------------------------------------------
EXTERN_C INPUT_PLUGIN_TABLE __declspec(dllexport) * __stdcall GetInputPluginTable( void )
{
	return &input_plugin_table;
}


//---------------------------------------------------------------------
//		ファイルハンドル構造体
//---------------------------------------------------------------------
typedef struct FileHandle{
	bool video = false;
	bool audio = false;
	AVFormatContext* format_context;
	AVStream* video_stream;
	AVCodec* video_codec;
	AVCodecContext* video_codec_context;
	AVStream* audio_stream;
	AVCodec* audio_codec;
	AVCodecContext* audio_codec_context;
	WAVEFORMATEX *audio_format;
	AVFrame* frame;
	AVFrame* audio_frame;
	AVPacket *packet;
	SwsContext* sws_ctx;
	SwrContext* swr;
	double fps;
	int swr_buf_len = 0;
	uint8_t *swr_buf;
	int64_t audio_now_timestamp;
	int64_t audio_next_timestamp;
	int64_t video_now_timestamp;
	int64_t video_next_timestamp;
	int64_t video_now_frame;
	bool need_resample;
	int64_t samples_start_gap;
	int samples_return;
	bool audio_seek = false;
} FILE_HANDLE;

uint8_t *swr_buf = 0;
std::map<std::string, std::string> decoder_redirect{};
char ch[100];
AVRational time_base = { 1, AV_TIME_BASE };

std::vector<std::string> split_str(std::string str, std::string delim) {
	std::vector<std::string> vec;
	int pos = str.find(delim);
	while (pos != std::string::npos) {
		vec.insert(vec.end(), str.substr(0, pos));
		str = str.substr(pos + 1);
		pos = str.find(delim);
	}
	vec.insert(vec.end(), str);
	return vec;
}

void reload_config() {
	int ret = 0;
	decoder_redirect.clear();
	char ch[3000] = "";
	FILE* file;
	ret = fopen_s(&file, "ffmpeg_decoder.ini", "r");
	if (ret == 0 && file) {
		std::string text = "";
		while ((ret = fscanf_s(file, "%s", ch, sizeof(ch))) != EOF) {
			ch[sizeof(ch) - 1] = '\0';
			text += ch;
			text += '\n';
		}
		fclose(file);
		std::vector<std::string> split = split_str(text, "\n");
		for (unsigned int i = 0;i < split.size();i++) {
			std::string split_str = split[i];
			int pos = split_str.find("\r");
			if (pos != std::string::npos) {
				split_str = split_str.replace(pos, 1, "");
			}
			pos = split_str.find("\n");
			if (pos != std::string::npos) {
				split_str = split_str.replace(pos, 1, "");
			}
			pos = split_str.find("=");
			if (pos != std::string::npos) {
				std::string key = split_str.substr(0, pos);
				std::string value = split_str.substr(pos + 1, split_str.size() - (pos + 1));
				decoder_redirect.emplace(key, value);
			}
		}
	}
}

//---------------------------------------------------------------------
//		初期化
//---------------------------------------------------------------------
BOOL func_init( void )
{
	swr_buf = (uint8_t*)malloc(2048 * 4);
	reload_config();
	return TRUE;
}


//---------------------------------------------------------------------
//		終了
//---------------------------------------------------------------------
BOOL func_exit( void )
{
	free(swr_buf);
	return TRUE;
}

void file_handle_free(FILE_HANDLE *fp) {
	if (fp) {
		avformat_close_input(&fp->format_context);
		if (fp->swr) {
			swr_free(&fp->swr);
		}
		if (fp->sws_ctx) sws_freeContext(fp->sws_ctx);
		if (fp->video) {
			av_frame_unref(fp->frame);
			av_frame_free(&fp->frame);
			avcodec_free_context(&fp->video_codec_context);
		}
		if (fp->audio) {
			av_frame_unref(fp->audio_frame);
			av_frame_free(&fp->audio_frame);
			avcodec_free_context(&fp->audio_codec_context);
			GlobalFree(fp->audio_format);
		}
		av_packet_free(&fp->packet);
		GlobalFree(fp);
	}
}

//---------------------------------------------------------------------
//		ファイルクローズ
//---------------------------------------------------------------------
BOOL func_close( INPUT_HANDLE ih )
{
	FILE_HANDLE	*fp = (FILE_HANDLE *)ih;

	file_handle_free(fp);

	return TRUE;
}


//---------------------------------------------------------------------
//		ファイルの情報
//---------------------------------------------------------------------
BOOL func_info_get( INPUT_HANDLE ih,INPUT_INFO *iip )
{
	FILE_HANDLE	*fp = (FILE_HANDLE *)ih;

	iip->flag = 0;
	memset(iip, 0, sizeof(INPUT_INFO));

	if( fp->video ) {
		BITMAPINFOHEADER *header = new BITMAPINFOHEADER();
		header->biWidth = fp->video_codec_context->width;
		header->biHeight = fp->video_codec_context->height;
		header->biBitCount = 24;
		iip->flag |= INPUT_INFO_FLAG_VIDEO | INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS;
		iip->rate = fp->video_stream->avg_frame_rate.num;
		iip->scale = fp->video_stream->avg_frame_rate.den;
		iip->n = (int)(fp->format_context->duration / (double)AV_TIME_BASE * (iip->rate / (double)iip->scale));
		iip->format = header;
		iip->format_size = header->biSize;
		iip->handler = 0;
	}

	if( fp->audio ) {
		iip->flag |= INPUT_INFO_FLAG_AUDIO;
		iip->audio_n = (int)(((double)fp->format_context->duration) / 1000000 * fp->audio_codec_context->sample_rate);
		iip->audio_format = fp->audio_format;
		iip->audio_format_size = sizeof(WAVEFORMATEX) + fp->audio_format->cbSize;
	}

	return fp->video || fp->audio;
}


//---------------------------------------------------------------------
//		画像読み込み
//---------------------------------------------------------------------

bool grab(FILE_HANDLE* fp) {
	av_packet_unref(fp->packet);
	while (av_read_frame(fp->format_context, fp->packet) == 0) {
		if (fp->packet->stream_index == fp->video_stream->index) {
			if (avcodec_send_packet(fp->video_codec_context, fp->packet) != 0) {
				OutputDebugString("avcodec_send_packet failed\n");
				av_packet_unref(fp->packet);
				return false;
			}
			if (avcodec_receive_frame(fp->video_codec_context, fp->frame) == 0) {
				fp->video_now_frame = (int64_t)(((fp->frame->pts) * av_q2d(fp->video_stream->time_base) - (fp->format_context->start_time * av_q2d(time_base))) * av_q2d(fp->video_stream->avg_frame_rate));
				fp->video_now_timestamp = fp->video_next_timestamp;
				fp->video_next_timestamp = fp->video_now_timestamp + 1;
				return true;
			}
		}
		av_packet_unref(fp->packet);
	}
	return false;
}

void seek_only(FILE_HANDLE* fp, int frame) {
	AVRational time_base = { 1, AV_TIME_BASE };
	int64_t time_stamp = (int64_t)((int64_t)frame * 1000000 / ((double)fp->video_stream->avg_frame_rate.num / fp->video_stream->avg_frame_rate.den)) + fp->format_context->start_time;
	avformat_seek_file(fp->format_context, -1, INT64_MIN, time_stamp, INT64_MAX, AVSEEK_FLAG_BACKWARD);
	avcodec_flush_buffers(fp->video_codec_context);
	grab(fp);
}

void seek(FILE_HANDLE* fp, int frame) {
	seek_only(fp, frame);
	// 移動先が目的地より進んでいることがあるためその場合は戻る
	int f = frame - (fp->video_now_frame - frame) - 3;
	while (fp->video_now_frame > frame) {
		if (f < 0) f = 0;
		seek_only(fp, f);
		if (f == 0) break;
		f -= 30;
	}
	while (fp->video_now_frame < frame && grab(fp)) {}
}

int func_read_video( INPUT_HANDLE ih,int frame,void *buf )
{
	FILE_HANDLE* fp = (FILE_HANDLE*)ih;
	if (!fp->video_stream) return 0;
	uint64_t skip = frame - fp->video_now_frame;
	if (skip > 10 || skip < 0) {
		seek(fp, frame);
		skip = 0;
	}
	for (int i = 0;i < skip;i++) {
		if (!grab(fp)) {
			return 0;
		}
	}
	int width = fp->frame->width;
	int height = fp->frame->height;
	int output_linesize = width * 3;//width * BGR(3)
	int output_size = output_linesize * height;

	uint8_t* dst_data[4] = { (uint8_t*)buf + output_linesize * (height - 1), NULL, NULL, NULL };
	int      dst_linesize[4] = { -(output_linesize), 0, 0, 0 };
	output_size = sws_scale(fp->sws_ctx, (const uint8_t* const*)fp->frame->data, fp->frame->linesize, 0, fp->frame->height, dst_data, dst_linesize);

	int output_rowsize = fp->frame->width * 3;
	return fp->frame->width * fp->frame->height * 3;
}


//---------------------------------------------------------------------
//		音声読み込み
//---------------------------------------------------------------------

bool grab_audio(FILE_HANDLE* fp) {
	av_frame_unref(fp->audio_frame);
	//複数フレームが含まれる場合があるので残っていればデコード
	if (avcodec_receive_frame(fp->audio_codec_context, fp->audio_frame) == 0) {
		if (fp->audio_seek) {
			fp->audio_now_timestamp = (int64_t)(((fp->audio_frame->pts) * av_q2d(fp->audio_stream->time_base) - (fp->format_context->start_time * av_q2d(time_base))) * fp->audio_codec_context->sample_rate);
			fp->audio_seek = false;
			fp->audio_now_timestamp -= fp->audio_now_timestamp % fp->audio_frame->nb_samples;
		}
		else {
			fp->audio_now_timestamp = fp->audio_next_timestamp;
		}
		fp->audio_next_timestamp = fp->audio_now_timestamp + fp->audio_frame->nb_samples;
		fp->need_resample = true;
		//メモリ解放
		av_packet_unref(fp->packet);
		return true;
	}
	while (av_read_frame(fp->format_context, fp->packet) == 0) {
		if (fp->packet->stream_index == fp->audio_stream->index) {
			if (avcodec_send_packet(fp->audio_codec_context, fp->packet) != 0) {
				printf("avcodec_send_packet failed");
				av_packet_unref(fp->packet);
				return false;
			}
			if (avcodec_receive_frame(fp->audio_codec_context, fp->audio_frame) == 0) {
				if (fp->audio_seek) {
					fp->audio_now_timestamp = (int64_t)(((fp->audio_frame->pts) * av_q2d(fp->audio_stream->time_base) - (fp->format_context->start_time * av_q2d(time_base))) * fp->audio_codec_context->sample_rate);
					fp->audio_seek = false;
					fp->audio_now_timestamp -= fp->audio_now_timestamp % fp->audio_frame->nb_samples;
				}
				else {
					fp->audio_now_timestamp = fp->audio_next_timestamp;
				}
				fp->audio_next_timestamp = fp->audio_now_timestamp + fp->audio_frame->nb_samples;
				fp->need_resample = true;
				av_packet_unref(fp->packet);
				return true;
			}
		}
		av_packet_unref(fp->packet);
	}
	return false;
}

void seek_audio(FILE_HANDLE* fp, int64_t sample_pos) {
	AVRational tb = {1, fp->audio_codec_context->sample_rate};
	int64_t timestamp = sample_pos * (int64_t)1000000 / fp->audio_codec_context->sample_rate + fp->format_context->start_time;
	avformat_seek_file(fp->format_context, -1, INT64_MIN, timestamp, INT64_MAX, AVSEEK_FLAG_BACKWARD);
	avcodec_flush_buffers(fp->audio_codec_context);
	fp->audio_seek = true;
	while (grab_audio(fp) && fp->audio_next_timestamp < sample_pos) {}
	fp->need_resample = true;
}

int func_read_audio(INPUT_HANDLE ih, int start, int length, void* buf)
{
	FILE_HANDLE* fp = (FILE_HANDLE*)ih;
	if (!(start >= fp->audio_now_timestamp && start < fp->audio_next_timestamp)) {
		grab_audio(fp);
		if (!(start >= fp->audio_now_timestamp && start < fp->audio_next_timestamp)) {
			seek_audio(fp, (int64_t)start);
		}
	}
	int decoded = 0;
	bool need_grab = false;
	while (!need_grab || grab_audio(fp)) {
		if (!fp->swr) {
			fp->swr = swr_alloc();
			if (!fp->swr) {
				OutputDebugString("swr_alloc error.\n");
				break;
			}
			av_opt_set_int(fp->swr, "in_channel_layout", av_get_default_channel_layout(fp->audio_frame->channels), 0);
			av_opt_set_int(fp->swr, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
			av_opt_set_int(fp->swr, "in_sample_rate", fp->audio_frame->sample_rate, 0);
			av_opt_set_int(fp->swr, "out_sample_rate", fp->audio_codec_context->sample_rate, 0);
			av_opt_set_sample_fmt(fp->swr, "in_sample_fmt", (AVSampleFormat)fp->audio_frame->format, 0);
			av_opt_set_sample_fmt(fp->swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
			if (swr_init(fp->swr) < 0) {
				OutputDebugString("swr_init error.\n");
				break;
			}
		}
		fp->samples_return = swr_convert(fp->swr, &swr_buf, fp->audio_frame->nb_samples, (const uint8_t**)fp->audio_frame->data, fp->audio_frame->nb_samples);
		if (fp->samples_return < 0) {
			OutputDebugString("swr_convert error.\n");
			return 0;
		}
		int skip = 0;
		if ((int) fp->audio_now_timestamp < start) {
			skip = start - (int)fp->audio_now_timestamp;
		}
		int len = fp->samples_return - skip;
		if (decoded + len > length) {
			len = length - decoded;
		}
		memcpy(((uint8_t*)buf) + (decoded * 4), swr_buf + (skip * 4), len * 4);
		decoded += len;
		fp->need_resample = false;
		if (decoded >= length) {
			return decoded;
		}
		need_grab = skip + len >= fp->audio_frame->nb_samples;
	}
	return 0;
}


//---------------------------------------------------------------------
//		キーフレーム情報
//---------------------------------------------------------------------
BOOL func_is_keyframe( INPUT_HANDLE ih,int frame )
{
	FILE_HANDLE	*fp = (FILE_HANDLE *)ih;

	if (fp->video) {
		return fp->frame->key_frame;
	}
	if(fp->audio) {
		return fp->audio_frame->key_frame;
	}
}


//---------------------------------------------------------------------
//		設定ダイアログ
//---------------------------------------------------------------------
BOOL CALLBACK DialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	UINT wmId;
	int ret = 0;
	int pos = 0;
	char ch[3000];
	FILE* file;
	switch (msg)
	{
	case WM_CLOSE:
		GetDlgItemText(hWnd, IDC_EDIT1,
			(LPTSTR)ch, (int)sizeof(ch));
		ret = fopen_s(&file, "ffmpeg_decoder.ini", "w");
		if (ret == 0 && file) {
			fprintf(file, "%s", ch);
			fclose(file);
			reload_config();
		}
		EndDialog(hWnd, IDD_DIALOG1);
		return TRUE;
	case WM_INITDIALOG:
		ret = fopen_s(&file, "ffmpeg_decoder.ini", "r");
		if (ret == 0 && file) {
			std::string data = "";
			while ((ret = fscanf_s(file, "%s", ch, sizeof(ch))) != EOF) {
				ch[sizeof(ch) - 1] = '\0';
				data += ch;
				data += "\r\n";
			}
			fclose(file);
			SetDlgItemText(hWnd, IDC_EDIT1, (LPCTSTR)data.c_str());
		}
		return TRUE;
	case WM_COMMAND:
		wmId = LOWORD(wParam);
		switch (wmId)
		{
		case IDC_BUTTON1:
			GetDlgItemText(hWnd, IDC_EDIT1,
				(LPTSTR)ch, (int)sizeof(ch));
			std::string data = ch;
			pos = data.find("\r");
			while (pos != std::string::npos) {
				data.replace(pos, 1, "");
				pos = data.find("\r", pos);
			}
			FILE* file;
			ret = fopen_s(&file, "ffmpeg_decoder.ini", "w");
			if (ret == 0 && file) {
				fprintf(file, "%s", data.c_str());
				fclose(file);
				reload_config();
			}
			EndDialog(hWnd, IDD_DIALOG1);
			break;
		}
		return TRUE;
	}
	return FALSE;
}
BOOL func_config( HWND hwnd,HINSTANCE dll_hinst )
{
	DialogBox(dll_hinst, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DialogProc);

	//	DLLを開放されても設定が残るように保存しておいてください。

	return TRUE;
}




//---------------------------------------------------------------------
//		ファイルオープン
//---------------------------------------------------------------------
INPUT_HANDLE func_open(LPSTR file)
{
	FILE_HANDLE* fp;

	fp = (FILE_HANDLE*)GlobalAlloc(GMEM_FIXED, sizeof(FILE_HANDLE));
	if (!fp) return NULL;
	ZeroMemory(fp, sizeof(FILE_HANDLE));

	fp->format_context = NULL;
	if (avformat_open_input(&fp->format_context, file, NULL, NULL) != 0) {
		OutputDebugString("avformat_open_input failed\n");
		goto reset;
	}

	if (avformat_find_stream_info(fp->format_context, NULL) < 0) {
		OutputDebugString("avformat_find_stream_info failed\n");
		goto reset;
	}

	fp->video_stream = NULL;
	fp->audio_stream = NULL;
	for (int i = 0; i < (int)fp->format_context->nb_streams; ++i) {
		if (!fp->video && fp->format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			fp->video_stream = fp->format_context->streams[i];
			fp->video = true;
		}
		if (!fp->audio && fp->format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			fp->audio_stream = fp->format_context->streams[i];
			fp->audio = true;
		}
	}
	if (!fp->video_stream && !fp->audio_stream) {
		OutputDebugString("No video stream and audio stream ...\n");
		goto reset;
	}

	if (fp->video && fp->video_stream) {
		fp->video_codec = avcodec_find_decoder(fp->video_stream->codecpar->codec_id);
		if (!fp->video_codec) {
			OutputDebugString("No supported decoder ...\n");
			goto audio;
		}
		std::string codec_str = fp->video_codec->name;
		if (decoder_redirect.count(codec_str) != 0) {
			fp->video_codec = avcodec_find_decoder_by_name(decoder_redirect[codec_str].c_str());
			if (fp->video_codec == NULL) {
				fp->video_codec = avcodec_find_decoder(fp->video_stream->codecpar->codec_id);
			}
		}
		if (!fp->video_codec) {
			OutputDebugString("No supported decoder ...\n");
			goto audio;
		}

		fp->video_codec_context = avcodec_alloc_context3(fp->video_codec);
		if (!fp->video_codec_context) {
			OutputDebugString("avcodec_alloc_context3 failed\n");
			goto audio;
		}
		fp->video_codec_context->thread_count = 0;
		if (avcodec_parameters_to_context(fp->video_codec_context, fp->video_stream->codecpar) < 0) {
			OutputDebugString("avcodec_parameters_to_context failed\n");
			goto audio;
		}

		if (avcodec_open2(fp->video_codec_context, fp->video_codec, NULL) != 0) {
			OutputDebugString("avcodec_open2 failed\n");
			goto audio;
		}
		fp->sws_ctx = sws_getContext(
			fp->video_codec_context->width, fp->video_codec_context->height
			, fp->video_codec_context->pix_fmt
			, fp->video_codec_context->width, fp->video_codec_context->height
			, AV_PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, NULL);

		if (!fp->sws_ctx) {
			OutputDebugString("Can not use sws");
			goto audio;
		}
		fp->fps = fp->video_stream->r_frame_rate.num / fp->video_stream->r_frame_rate.den;
		fp->frame = av_frame_alloc();
		fp->video_now_timestamp = 0;
		fp->video_next_timestamp = fp->audio_now_timestamp + 1;
	}
	goto audio_2;
audio:
	fp->video = false;
audio_2:
	if (fp->audio && fp->audio_stream) {
		fp->audio_codec = avcodec_find_decoder(fp->audio_stream->codecpar->codec_id);
		std::string codec_str = fp->audio_codec->name;
		if (decoder_redirect.count(codec_str) != 0) {
			fp->audio_codec = avcodec_find_decoder_by_name(decoder_redirect[codec_str].c_str());
			if (fp->audio_codec == NULL) {
				fp->audio_codec = avcodec_find_decoder(fp->audio_stream->codecpar->codec_id);
			}
		}
		if (!fp->audio_codec) {
			OutputDebugString("No supported decoder ...\n");
			goto reset;
		}
		fp->audio_codec_context = avcodec_alloc_context3(fp->audio_codec);
		if (!fp->audio_codec_context) {
			OutputDebugString("avcodec_alloc_context3 failed\n");
			goto reset;
		}
		fp->audio_codec_context->thread_count = 0;
		if (avcodec_parameters_to_context(fp->audio_codec_context, fp->audio_stream->codecpar) < 0) {
			OutputDebugString("avcodec_parameters_to_context failed\n");
			goto reset;
		}

		if (avcodec_open2(fp->audio_codec_context, fp->audio_codec, NULL) != 0) {
			OutputDebugString("avcodec_open2 failed\n");
			goto reset;
		}
		fp->audio_format = new WAVEFORMATEX();
		fp->audio_format->wFormatTag = WAVE_FORMAT_PCM;
		fp->audio_format->nChannels = 2;
		fp->audio_format->nSamplesPerSec = fp->audio_codec_context->sample_rate;
		fp->audio_format->wBitsPerSample = 16;
		fp->audio_format->nBlockAlign = fp->audio_format->nChannels * (fp->audio_format->wBitsPerSample / 8);
		fp->audio_format->nAvgBytesPerSec = fp->audio_format->nSamplesPerSec * fp->audio_format->nBlockAlign;
		fp->audio_format->cbSize = 0;
		fp->audio_frame = av_frame_alloc();
	}
	if (!fp->video && !fp->audio) {
		goto reset;
	}
	fp->packet = av_packet_alloc();
	av_init_packet(fp->packet);
	return fp;
reset:
	file_handle_free(fp);
	return NULL;
}