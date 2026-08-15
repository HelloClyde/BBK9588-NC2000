// orignal author: Lee

#pragma once

/*
 * BBK9588 is a freestanding target and does not ship libstdc++.  WORD_MODE
 * can describe at most 255 240-sample frames, so a 64K-sample buffer covers
 * the full command range without allocation.
 */
template <typename T, unsigned int Capacity>
class DspBuffer {
public:
	DspBuffer() : length(0) {}
	void clear() { length = 0; }
	unsigned int size() const { return length; }
	void push_back(T value) {
		if (length < Capacity) data[length++] = value;
	}
	T *data_ptr() { return data; }
	const T *data_ptr() const { return data; }
	T &operator[](unsigned int index) { return data[index]; }
	const T &operator[](unsigned int index) const { return data[index]; }
private:
	T data[Capacity];
	unsigned int length;
};

typedef unsigned char byte;

const int ORDER=10;

typedef struct {
	int D[ORDER+1];
	int ai[ORDER+1];
} fixfilter;

typedef struct {
	int Lindex;
	int Gindex;
	int Grid;
	int Pos[2];
	int Sign[2];
} tagGainShape;

class Dsp {
	struct WordCelpFrame {
		byte data[18];
		unsigned short sample_count;
	};

	byte clpBuf[18];
	int dspCelp[15];
	int dspCelpOff;
public:
	int dspMode;
	Dsp();
	void reset();
	void write(int high,int low);
	void process_pending_word(int max_frames);
	bool has_pending_word_decode() const { return word_job_active; }
	unsigned int buffered_samples() const;
private:
	void dspCelpToCelp();
	void writeSample8000(int val);
	void writePcm(int val);
	void finishWordJob();

	void dspStart();
	void subFrame(int subframe_NUM,int s0,int s1,int fixpos);
	void dsp(byte* celp);
	int SamplePitch(int Pos,int F,int s);
	void DecodePitch(int P,int k);
	void InterpLSP(int k);
	void fixlsptopc();
	void root_rs(int *HXrootInPo, int *HXpcoef);
	int fixpolefilter(int S);
	int rounding(int mul,int P);
private:
	int oldindex[ORDER];
	int newindex[ORDER];
	int lsp[ORDER];
	int r[240];
	int LTP[148] ;
	short Sout[240];
	int recursive[60];
	int Period,Frac,_SubFrameSize;
	int PreP;
	int pcount;
	int EX;
	int PM;
	bool lspflag;
	tagGainShape Pitch,Inno;
	fixfilter decoder;

	int delsp[10];
	int pc[ORDER+1];
    int tr[5];
	int p[5], q[5] , RX[5] , SX[5];
	int id;

	DspBuffer<WordCelpFrame, 256> word_frames;
	unsigned int word_collected_samples;
	unsigned int word_frame_read;
	unsigned int word_sample_cursor;
	unsigned int word_start;
	unsigned int word_end;
	bool word_job_active;
	int c2,c4,c8;
	int data_cnt;

public:
	void (*callback) (unsigned char *, int);

};

void set_dsp_log_level(int level);
