#include "LuaSoundVorbis.h"
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // SEEK_SET/SEEK_CUR/SEEK_END

// ===========================================================================
// In-memory ov_callbacks (mirrors dr_wav's memory-decode convention in
// LuaSound.cpp, adapted to libvorbis's stdio-style read/seek/close/tell
// callback shape).
// ===========================================================================

struct OggMemStream {
	const unsigned char* data;
	size_t size;
	size_t pos;
};

static size_t OggMemRead(void* ptr, size_t size, size_t nmemb, void* datasource) {
	OggMemStream* s = (OggMemStream*)datasource;
	if (size == 0) return 0;
	size_t remain = s->size - s->pos;
	size_t want = size * nmemb;
	size_t take = want < remain ? want : remain;
	if (take > 0) {
		memcpy(ptr, s->data + s->pos, take);
		s->pos += take;
	}
	return take / size;
}

static int OggMemSeek(void* datasource, ogg_int64_t offset, int whence) {
	OggMemStream* s = (OggMemStream*)datasource;
	long long newpos;
	switch (whence) {
		case SEEK_SET: newpos = offset; break;
		case SEEK_CUR: newpos = (long long)s->pos + offset; break;
		case SEEK_END: newpos = (long long)s->size + offset; break;
		default: return -1;
	}
	if (newpos < 0 || (size_t)newpos > s->size)
		return -1;
	s->pos = (size_t)newpos;
	return 0;
}

static int OggMemClose(void* datasource) {
	(void)datasource;
	return 0;
}

static long OggMemTell(void* datasource) {
	return (long)((OggMemStream*)datasource)->pos;
}

bool IsOggData(const unsigned char* data, size_t len) {
	return len >= 4 && memcmp(data, "OggS", 4) == 0;
}

// ===========================================================================
// Decode
// ===========================================================================

bool DecodeOgg(const unsigned char* data, size_t len, LuaSound* snd, const char** errMsg) {
	OggMemStream stream{ data, len, 0 };
	ov_callbacks cb{ OggMemRead, OggMemSeek, OggMemClose, OggMemTell };

	OggVorbis_File vf;
	if (ov_open_callbacks(&stream, &vf, NULL, 0, cb) < 0) {
		*errMsg = "not a valid Ogg Vorbis stream";
		return false;
	}

	vorbis_info* vi = ov_info(&vf, -1);
	int channels = vi->channels;
	int sampleRate = (int)vi->rate;

	size_t capacity = 65536; // frames
	size_t frameCount = 0;
	float* samples = (float*)kitsune_malloc(capacity * channels * sizeof(float));

	int bitstream = 0;
	for (;;) {
		float** pcm;
		long ret = ov_read_float(&vf, &pcm, 4096, &bitstream);
		if (ret == 0)
			break; // EOF
		if (ret < 0)
			continue; // recoverable stream error (e.g. corrupt page); skip and keep going
		if (frameCount + (size_t)ret > capacity) {
			while (frameCount + (size_t)ret > capacity)
				capacity *= 2;
			samples = (float*)kitsune_realloc(samples, capacity * channels * sizeof(float));
		}
		for (long i = 0; i < ret; i++)
			for (int c = 0; c < channels; c++)
				samples[(frameCount + (size_t)i) * channels + c] = pcm[c][i];
		frameCount += (size_t)ret;
	}
	ov_clear(&vf);

	snd->sampleRate = sampleRate;
	snd->channels = channels;
	snd->frameCount = (int)frameCount;
	snd->samples = samples;
	return true;
}

// ===========================================================================
// Encode -- the canonical libvorbis analysis/bitrate/muxing sequence
// (vorbis_info_init -> vorbis_encode_init_vbr -> vorbis_analysis_init +
// vorbis_block_init -> ogg_stream_init -> 3 header packets -> feed PCM in
// chunks, draining blocks/packets/pages as they become available -> signal
// end-of-stream with a zero-length vorbis_analysis_wrote, drain the rest).
// ===========================================================================

unsigned char* EncodeOgg(const LuaSound* snd, double quality, size_t* outLen) {
	if (quality < 0.0) quality = 0.0;
	if (quality > 1.0) quality = 1.0;

	vorbis_info vi;
	vorbis_info_init(&vi);
	if (vorbis_encode_init_vbr(&vi, snd->channels, snd->sampleRate, (float)quality) != 0) {
		vorbis_info_clear(&vi);
		return NULL;
	}

	vorbis_comment vc;
	vorbis_comment_init(&vc);

	vorbis_dsp_state vd;
	vorbis_analysis_init(&vd, &vi);
	vorbis_block vb;
	vorbis_block_init(&vd, &vb);

	ogg_stream_state os;
	ogg_stream_init(&os, rand());

	size_t capacity = 65536;
	size_t len = 0;
	unsigned char* out = (unsigned char*)kitsune_malloc(capacity);

	auto appendBytes = [&](const unsigned char* p, long n) {
		if (n <= 0) return;
		if (len + (size_t)n > capacity) {
			while (len + (size_t)n > capacity)
				capacity *= 2;
			out = (unsigned char*)kitsune_realloc(out, capacity);
		}
		memcpy(out + len, p, (size_t)n);
		len += (size_t)n;
	};
	auto drainPages = [&](bool flush) {
		ogg_page og;
		for (;;) {
			int got = flush ? ogg_stream_flush(&os, &og) : ogg_stream_pageout(&os, &og);
			if (!got) break;
			appendBytes(og.header, og.header_len);
			appendBytes(og.body, og.body_len);
		}
	};

	ogg_packet header, header_comm, header_code;
	vorbis_analysis_headerout(&vd, &vc, &header, &header_comm, &header_code);
	ogg_stream_packetin(&os, &header);
	ogg_stream_packetin(&os, &header_comm);
	ogg_stream_packetin(&os, &header_code);
	drainPages(true); // header pages are flushed immediately, per convention

	int channels = snd->channels;
	int frameCount = snd->frameCount;
	const int CHUNK = 1024;
	int framesWritten = 0;
	bool eos = false;

	while (!eos) {
		int toWrite = MIN(CHUNK, frameCount - framesWritten);
		if (toWrite > 0) {
			float** buffer = vorbis_analysis_buffer(&vd, toWrite);
			for (int c = 0; c < channels; c++)
				for (int i = 0; i < toWrite; i++)
					buffer[c][i] = snd->samples[(size_t)(framesWritten + i) * channels + c];
			vorbis_analysis_wrote(&vd, toWrite);
			framesWritten += toWrite;
		}
		else {
			vorbis_analysis_wrote(&vd, 0); // signal end-of-stream
		}

		while (vorbis_analysis_blockout(&vd, &vb) == 1) {
			vorbis_analysis(&vb, NULL);
			vorbis_bitrate_addblock(&vb);
			ogg_packet op;
			while (vorbis_bitrate_flushpacket(&vd, &op)) {
				ogg_stream_packetin(&os, &op);
				drainPages(false);
			}
		}

		if (toWrite == 0) {
			drainPages(true);
			eos = true;
		}
	}

	ogg_stream_clear(&os);
	vorbis_block_clear(&vb);
	vorbis_dsp_clear(&vd);
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi);

	*outLen = len;
	return out;
}
