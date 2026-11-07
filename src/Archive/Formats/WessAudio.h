#pragma once

// -----------------------------------------------------------------------------
// Doom 64 WESS sample audio: N64 VADPCM <-> 16 bit PCM <-> WAV
//
// A WDD sample is a series of 9 byte frames. Each frame makes 16 samples:
//   byte 0       high nibble: scale (shift), low nibble: predictor to use
//   bytes 1..8   16 signed 4 bit residuals
// The predictors come from the sample's codebook (one per sample, in the PREDICTORS
// section of the WMD): order (u32), npredictors (u32), then 128 big-endian s16.
// Each predictor is 2 vectors of 8 entries. Samples play at 11025 Hz by default.
// -----------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace slade::wess
{
constexpr int default_sample_rate = 11025;

struct Book
{
	uint32_t order       = 2;
	uint32_t npredictors = 0;
	int16_t  preds[128]  = {};

	// Reads the 264 byte book from the WMD
	static bool fromBytes(const uint8_t* p, size_t size, Book& out)
	{
		if (size < 264)
			return false;
		out.order       = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
		out.npredictors = (uint32_t(p[4]) << 24) | (uint32_t(p[5]) << 16) | (uint32_t(p[6]) << 8) | p[7];
		if (out.npredictors < 1 || out.npredictors > 8 || out.order != 2)
			return false;
		for (int i = 0; i < 128; i++)
			out.preds[i] = static_cast<int16_t>((p[8 + i * 2] << 8) | p[9 + i * 2]);
		return true;
	}
};

namespace detail
{
	constexpr int itable[16] = { 0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1 };

	inline int16_t clamp16(int64_t v)
	{
		return static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
	}

	// Decodes 8 samples from the 4 residual bytes at [in] (same maths as the N64 decoder).
	// [last] holds the previous 8 output samples, replaced with the new ones.
	inline void decode8(const uint8_t* in, int16_t* out, int scale, const int16_t* pred1, int16_t last[8])
	{
		const int16_t* pred2 = pred1 + 8;
		int16_t        tmp[8];
		for (int i = 0; i < 8; i++)
		{
			const int nib = (i & 1) ? (in[i / 2] & 0xF) : ((in[i / 2] >> 4) & 0xF);
			tmp[i]        = static_cast<int16_t>(itable[nib] * (1 << scale));
		}

		for (int i = 0; i < 8; i++)
		{
			int64_t total = int64_t(pred1[i]) * last[6] + int64_t(pred2[i]) * last[7];
			for (int x = i - 1; x > -1; x--)
				total += int64_t(tmp[(i - 1) - x]) * pred2[x];

			out[i] = clamp16(((int64_t(tmp[i]) << 11) + total) >> 11);
		}
		std::memcpy(last, out, sizeof(int16_t) * 8);
	}
} // namespace detail

// Decodes [len] bytes of VADPCM (a partial last frame is ignored)
inline std::vector<int16_t> decodeVadpcm(const uint8_t* in, size_t len, const Book& book)
{
	std::vector<int16_t> out;
	out.reserve(len / 9 * 16);

	int16_t last[8] = {};
	for (size_t f = 0; f + 9 <= len; f += 9)
	{
		const int      scale = (in[f] >> 4) & 0xF;
		const unsigned pred  = std::min<unsigned>(in[f] & 0xF, book.npredictors - 1);
		int16_t        half[8];

		detail::decode8(in + f + 1, half, scale, &book.preds[pred * 16], last);
		out.insert(out.end(), half, half + 8);
		detail::decode8(in + f + 5, half, scale, &book.preds[pred * 16], last);
		out.insert(out.end(), half, half + 8);
	}
	return out;
}

// Encodes [pcm] as VADPCM with [book]. For every 16 sample frame the predictor and scale
// that reproduce the audio most accurately are chosen.
inline std::vector<uint8_t> encodeVadpcm(const std::vector<int16_t>& pcm, const Book& book)
{
	std::vector<uint8_t> out;
	const size_t         nframes = (pcm.size() + 15) / 16;
	out.reserve(nframes * 9);

	int16_t last[8] = {};

	for (size_t f = 0; f < nframes; f++)
	{
		int target[16] = {};
		for (int i = 0; i < 16; i++)
			if (f * 16 + i < pcm.size())
				target[i] = pcm[f * 16 + i];

		double  best_err = -1;
		uint8_t best_frame[9] = {};
		int16_t best_last[8]  = {};

		for (unsigned p = 0; p < book.npredictors; p++)
		{
			for (int scale = 0; scale <= 12; scale++)
			{
				uint8_t frame[9] = {};
				frame[0]         = static_cast<uint8_t>((scale << 4) | p);
				int16_t lst[8];
				std::memcpy(lst, last, sizeof(lst));
				double err = 0;

				for (int half = 0; half < 2; half++)
				{
					const int16_t* pred1 = &book.preds[p * 16];
					const int16_t* pred2 = pred1 + 8;
					int16_t        tmp[8];
					int16_t        outs[8];

					for (int i = 0; i < 8; i++)
					{
						int64_t total = int64_t(pred1[i]) * lst[6] + int64_t(pred2[i]) * lst[7];
						for (int x = i - 1; x > -1; x--)
							total += int64_t(tmp[(i - 1) - x]) * pred2[x];

						// Residual needed to land on the target, quantised to 4 bits
						const double r = double(target[half * 8 + i]) - double(total >> 11);
						int          q = static_cast<int>(std::lround(r / double(1 << scale)));
						q              = std::clamp(q, -8, 7);

						tmp[i]  = static_cast<int16_t>(q * (1 << scale));
						outs[i] = detail::clamp16(((int64_t(tmp[i]) << 11) + total) >> 11);

						const double e = double(target[half * 8 + i]) - double(outs[i]);
						err += e * e;

						const int nib = q & 0xF;
						uint8_t&  b   = frame[1 + half * 4 + i / 2];
						b             = static_cast<uint8_t>((i & 1) ? (b | nib) : (nib << 4));
					}
					std::memcpy(lst, outs, sizeof(lst));
				}

				if (best_err < 0 || err < best_err)
				{
					best_err = err;
					std::memcpy(best_frame, frame, 9);
					std::memcpy(best_last, lst, sizeof(lst));
				}
			}
		}

		out.insert(out.end(), best_frame, best_frame + 9);
		std::memcpy(last, best_last, sizeof(last));
	}
	return out;
}

// -----------------------------------------------------------------------------
// Codebook design (port of the N64 SDK 'tabledesign' tool)
//
// Analyses the audio frame by frame (16 samples), models each frame as a linear predictor,
// then clusters those predictors (split + refine) into [1 << bits] codebook entries.
// -----------------------------------------------------------------------------
namespace detail
{
	using Mat = std::vector<std::vector<double>>;

	inline int durbin(const double* in, int n, double* k_out, double* a_out, double* err_out)
	{
		int    ret = 0;
		double div = in[0];
		a_out[0]   = 1.0;
		for (int i = 1; i <= n; i++)
		{
			double sum = 0.0;
			for (int j = 1; j <= i - 1; j++)
				sum += a_out[j] * in[i - j];

			a_out[i] = (div > 0.0 ? -(in[i] + sum) / div : 0.0);
			k_out[i] = a_out[i];
			if (std::fabs(k_out[i]) > 1.0)
				ret++;

			for (int j = 1; j < i; j++)
				a_out[j] += a_out[i - j] * a_out[i];

			div *= 1.0 - a_out[i] * a_out[i];
		}
		*err_out = div;
		return ret;
	}

	inline void afromk(const double* in, double* out, int n)
	{
		out[0] = 1.0;
		for (int i = 1; i <= n; i++)
		{
			out[i] = in[i];
			for (int j = 1; j <= i - 1; j++)
				out[j] += out[i - j] * out[i];
		}
	}

	inline int kfroma(double* in, double* out, int n)
	{
		int                 ret = 0;
		std::vector<double> next(n + 1);

		out[n] = in[n];
		for (int i = n - 1; i >= 1; i--)
		{
			for (int j = 0; j <= i; j++)
			{
				const double temp = out[i + 1];
				const double div  = 1.0 - (temp * temp);
				if (div == 0.0)
					return 1;
				next[j] = (in[j] - in[i + 1 - j] * temp) / div;
			}
			for (int j = 0; j <= i; j++)
				in[j] = next[j];

			out[i] = next[i];
			if (std::fabs(out[i]) > 1.0)
				ret++;
		}
		return ret;
	}

	inline void rfroma(const double* in, int n, double* out)
	{
		Mat mat(n + 1);
		mat[n].assign(n + 1, 0.0);
		mat[n][0] = 1.0;
		for (int i = 1; i <= n; i++)
			mat[n][i] = -in[i];

		for (int i = n; i >= 1; i--)
		{
			mat[i - 1].assign(std::max(i, 1), 0.0);
			const double div = 1.0 - mat[i][i] * mat[i][i];
			for (int j = 1; j <= i - 1; j++)
				mat[i - 1][j] = (mat[i][i - j] * mat[i][i] + mat[i][j]) / div;
		}

		out[0] = 1.0;
		for (int i = 1; i <= n; i++)
		{
			out[i] = 0.0;
			for (int j = 1; j <= i; j++)
				out[i] += mat[i][j] * out[i - j];
		}
	}

	inline double modelDist(const double* a, const double* b, int n)
	{
		std::vector<double> r(n + 1), ac(n + 1);
		rfroma(b, n, r.data());
		for (int i = 0; i <= n; i++)
		{
			ac[i] = 0.0;
			for (int j = 0; j <= n - i; j++)
				ac[i] += a[j] * a[i + j];
		}
		double ret = ac[0] * r[0];
		for (int i = 1; i <= n; i++)
			ret += 2 * r[i] * ac[i];
		return ret;
	}

	// [in] points at the current frame, valid for indices [-n, m)
	inline void acmat(const int16_t* in, int n, int m, Mat& out)
	{
		for (int i = 1; i <= n; i++)
			for (int j = 1; j <= n; j++)
			{
				out[i][j] = 0.0;
				for (int k = 0; k < m; k++)
					out[i][j] += double(in[k - i]) * in[k - j];
			}
	}

	inline void acvect(const int16_t* in, int n, int m, double* out)
	{
		for (int i = 0; i <= n; i++)
		{
			out[i] = 0.0;
			for (int j = 0; j < m; j++)
				out[i] -= double(in[j - i]) * in[j];
		}
	}

	// LU decomposition (1-indexed), returns 1 if singular
	inline int lud(Mat& a, int n, int* indx, int* d)
	{
		int                 imax = 0;
		std::vector<double> vv(n + 1);
		*d = 1;
		for (int i = 1; i <= n; i++)
		{
			double big = 0.0;
			for (int j = 1; j <= n; j++)
			{
				const double t = std::fabs(a[i][j]);
				if (t > big)
					big = t;
			}
			if (big == 0.0)
				return 1;
			vv[i] = 1.0 / big;
		}
		for (int j = 1; j <= n; j++)
		{
			for (int i = 1; i < j; i++)
			{
				double sum = a[i][j];
				for (int k = 1; k < i; k++)
					sum -= a[i][k] * a[k][j];
				a[i][j] = sum;
			}
			double big = 0.0;
			for (int i = j; i <= n; i++)
			{
				double sum = a[i][j];
				for (int k = 1; k < j; k++)
					sum -= a[i][k] * a[k][j];
				a[i][j] = sum;
				const double dum = vv[i] * std::fabs(sum);
				if (dum >= big)
				{
					big  = dum;
					imax = i;
				}
			}
			if (j != imax)
			{
				for (int k = 1; k <= n; k++)
					std::swap(a[imax][k], a[j][k]);
				*d       = -(*d);
				vv[imax] = vv[j];
			}
			indx[j] = imax;
			if (a[j][j] == 0.0)
				return 1;
			if (j != n)
			{
				const double dum = 1.0 / a[j][j];
				for (int i = j + 1; i <= n; i++)
					a[i][j] *= dum;
			}
		}

		double mn = 1e10, mx = 0.0;
		for (int i = 1; i <= n; i++)
		{
			const double t = std::fabs(a[i][i]);
			mn             = std::min(mn, t);
			mx             = std::max(mx, t);
		}
		return mn / mx < 1e-10 ? 1 : 0;
	}

	inline void lubksb(const Mat& a, int n, const int* indx, double* b)
	{
		int ii = 0;
		for (int i = 1; i <= n; i++)
		{
			const int ip  = indx[i];
			double    sum = b[ip];
			b[ip]         = b[i];
			if (ii)
				for (int j = ii; j <= i - 1; j++)
					sum -= a[i][j] * b[j];
			else if (sum)
				ii = i;
			b[i] = sum;
		}
		for (int i = n; i >= 1; i--)
		{
			double sum = b[i];
			for (int j = i + 1; j <= n; j++)
				sum -= a[i][j] * b[j];
			b[i] = sum / a[i][i];
		}
	}

	inline void clampK(double* k, int n)
	{
		for (int i = 1; i <= n; i++)
		{
			if (k[i] >= 1.0)
				k[i] = 0.9999999999;
			if (k[i] <= -1.0)
				k[i] = -0.9999999999;
		}
	}

	inline void refine(Mat& table, int order, int np, const Mat& data, int iters)
	{
		Mat                 rsums(np, std::vector<double>(order + 1));
		std::vector<int>    counts(np);
		std::vector<double> tmp(order + 1);
		double              dummy;

		for (int iter = 0; iter < iters; iter++)
		{
			for (int i = 0; i < np; i++)
			{
				counts[i] = 0;
				std::fill(rsums[i].begin(), rsums[i].end(), 0.0);
			}

			for (const auto& d : data)
			{
				double best      = 1e30;
				int    bestIndex = 0;
				for (int j = 0; j < np; j++)
				{
					const double dist = modelDist(table[j].data(), d.data(), order);
					if (dist < best)
					{
						best      = dist;
						bestIndex = j;
					}
				}
				counts[bestIndex]++;
				rfroma(d.data(), order, tmp.data());
				for (int j = 0; j <= order; j++)
					rsums[bestIndex][j] += tmp[j];
			}

			for (int i = 0; i < np; i++)
				if (counts[i] > 0)
					for (int j = 0; j <= order; j++)
						rsums[i][j] /= counts[i];

			for (int i = 0; i < np; i++)
			{
				durbin(rsums[i].data(), order, tmp.data(), table[i].data(), &dummy);
				clampK(tmp.data(), order);
				afromk(tmp.data(), table[i].data(), order);
			}
		}
	}
} // namespace detail

// Designs a 264 byte WMD codebook (order 2, up to [1 << bits] predictors) for [pcm].
// Returns false if the audio has too little signal to analyse.
inline bool designBook(const std::vector<int16_t>& pcm, std::vector<uint8_t>& book_out, int bits = 3)
{
	using namespace detail;
	constexpr int order = 2, frame = 16, iters = 2;
	constexpr double thresh = 10.0;

	// History of one frame (zeros) in front of the audio so negative indices work
	std::vector<int16_t> buf(frame + (pcm.size() / frame) * frame, 0);
	std::copy(pcm.begin(), pcm.begin() + (pcm.size() / frame) * frame, buf.begin() + frame);

	Mat                 mat(order + 1, std::vector<double>(order + 1));
	std::vector<double> vec(order + 1), k(order + 1);
	int                 perm[order + 1], det;
	Mat                 data;

	for (size_t f = frame; f + frame <= buf.size(); f += frame)
	{
		const int16_t* cur = &buf[f];
		acvect(cur, order, frame, vec.data());
		if (std::fabs(vec[0]) <= thresh)
			continue;
		acmat(cur, order, frame, mat);
		if (lud(mat, order, perm, &det) != 0)
			continue;
		lubksb(mat, order, perm, vec.data());
		vec[0] = 1.0;
		if (kfroma(vec.data(), k.data(), order) != 0)
			continue;
		clampK(k.data(), order);
		std::vector<double> row(order + 1);
		afromk(k.data(), row.data(), order);
		data.push_back(row);
	}
	if (data.empty())
		return false;

	// Average autocorrelation -> first predictor
	vec[0] = 1.0;
	for (int j = 1; j <= order; j++)
		vec[j] = 0.0;
	std::vector<double> tmp(order + 1);
	for (const auto& d : data)
	{
		rfroma(d.data(), order, tmp.data());
		for (int j = 1; j <= order; j++)
			vec[j] += tmp[j];
	}
	for (int j = 1; j <= order; j++)
		vec[j] /= data.size();

	Mat table((size_t(1) << bits), std::vector<double>(order + 1));
	double dummy;
	durbin(vec.data(), order, k.data(), table[0].data(), &dummy);
	clampK(k.data(), order);
	afromk(k.data(), table[0].data(), order);

	int curBits = 0;
	while (curBits < bits)
	{
		const int np = 1 << curBits;
		// split: every entry gets a copy nudged by 0.01 * delta, delta = [0, -1, 0]
		for (int i = 0; i < np; i++)
			for (int j = 0; j <= order; j++)
				table[i + np][j] = table[i][j] + (j == order - 1 ? -1.0 : 0.0) * 0.01;
		curBits++;
		refine(table, order, 1 << curBits, data, iters);
	}

	const int np = 1 << curBits;
	book_out.assign(264, 0);
	auto put32 = [&](size_t o, uint32_t v) {
		for (int i = 0; i < 4; i++)
			book_out[o + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
	};
	put32(0, order);
	put32(4, np);

	for (int p = 0; p < np; p++)
	{
		// print_entry: build the 8x order matrix of the predictor
		double t[8][order] = {};
		const auto& row = table[p];
		for (int i = 0; i < order; i++)
			for (int j = i; j < order; j++)
				t[i][j] = -row[order - j + i];
		for (int i = 1; i < 8; i++)
			for (int j = 1; j <= order; j++)
				if (i - j >= 0)
					for (int kk = 0; kk < order; kk++)
						t[i][kk] -= row[j] * t[i - j][kk];

		for (int i = 0; i < order; i++)
			for (int j = 0; j < 8; j++)
			{
				const double fval = t[j][i] * 2048.0;
				int          ival = static_cast<int>(fval < 0.0 ? fval - 0.5 : fval + 0.5);
				ival              = std::clamp(ival, -0x8000, 0x7FFF);
				const size_t o    = 8 + (p * 16 + i * 8 + j) * 2;
				book_out[o]       = static_cast<uint8_t>((ival >> 8) & 0xFF);
				book_out[o + 1]   = static_cast<uint8_t>(ival & 0xFF);
			}
	}
	return true;
}

// Writes [book] as the 264 byte WMD structure
inline std::vector<uint8_t> bookToBytes(const Book& book)
{
	std::vector<uint8_t> out(264, 0);
	auto                 put32 = [&](size_t o, uint32_t v) {
        for (int i = 0; i < 4; i++)
            out[o + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
	};
	put32(0, book.order);
	put32(4, book.npredictors);
	for (int i = 0; i < 128; i++)
	{
		out[8 + i * 2] = static_cast<uint8_t>((book.preds[i] >> 8) & 0xFF);
		out[9 + i * 2] = static_cast<uint8_t>(book.preds[i] & 0xFF);
	}
	return out;
}

// Squared error between two sample sequences
inline double pcmError(const std::vector<int16_t>& a, const std::vector<int16_t>& b)
{
	double e = 0;
	for (size_t i = 0; i < a.size(); i++)
	{
		const double d = double(a[i]) - (i < b.size() ? b[i] : 0);
		e += d * d;
	}
	return e;
}

// -----------------------------------------------------------------------------
// WAV
// -----------------------------------------------------------------------------

// 16 bit mono WAV
inline std::vector<uint8_t> makeWav(const std::vector<int16_t>& pcm, int rate = default_sample_rate)
{
	std::vector<uint8_t> w;
	auto                 u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; i++)
            w.push_back(static_cast<uint8_t>(v >> (8 * i)));
	};
	auto u16 = [&](uint32_t v) {
		w.push_back(static_cast<uint8_t>(v));
		w.push_back(static_cast<uint8_t>(v >> 8));
	};
	auto tag = [&](const char* t) { w.insert(w.end(), t, t + 4); };

	tag("RIFF");
	u32(static_cast<uint32_t>(36 + pcm.size() * 2));
	tag("WAVE");
	tag("fmt ");
	u32(16);
	u16(1);
	u16(1);
	u32(rate);
	u32(rate * 2);
	u16(2);
	u16(16);
	tag("data");
	u32(static_cast<uint32_t>(pcm.size() * 2));
	for (auto s : pcm)
		u16(static_cast<uint16_t>(s));
	return w;
}

// Reads a PCM WAV (8/16/24 bit, any channel count, mixed to mono) as 16 bit samples
inline bool parseWav(const uint8_t* d, size_t size, std::vector<int16_t>& pcm, int& rate, std::string& err)
{
	auto le16 = [&](size_t p) { return static_cast<uint32_t>(d[p] | (d[p + 1] << 8)); };
	auto le32 = [&](size_t p) { return le16(p) | (le16(p + 2) << 16); };

	if (size < 12 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0)
	{
		err = "Not a WAV file";
		return false;
	}

	uint32_t channels = 0, bits = 0, format = 0;
	rate               = 0;
	size_t data_pos = 0, data_len = 0;
	for (size_t p = 12; p + 8 <= size;)
	{
		const size_t len = le32(p + 4);
		if (std::memcmp(d + p, "fmt ", 4) == 0 && p + 8 + 16 <= size)
		{
			format   = le16(p + 8);
			channels = le16(p + 10);
			rate     = static_cast<int>(le32(p + 12));
			bits     = le16(p + 22);
		}
		else if (std::memcmp(d + p, "data", 4) == 0)
		{
			data_pos = p + 8;
			data_len = std::min(len, size - data_pos);
			break;
		}
		p += 8 + len + (len & 1);
	}

	if ((format != 1 && format != 0xFFFE) || channels < 1 || (bits != 8 && bits != 16 && bits != 24) || !data_pos)
	{
		err = "Only uncompressed 8, 16 or 24 bit PCM WAV files are supported";
		return false;
	}

	const size_t bytes = bits / 8;
	const size_t n     = data_len / (bytes * channels);
	pcm.assign(n, 0);
	for (size_t i = 0; i < n; i++)
	{
		int64_t sum = 0;
		for (uint32_t c = 0; c < channels; c++)
		{
			const uint8_t* s = d + data_pos + (i * channels + c) * bytes;
			if (bits == 8)
				sum += (int(s[0]) - 128) * 256;
			else if (bits == 16)
				sum += static_cast<int16_t>(s[0] | (s[1] << 8));
			else
				sum += static_cast<int16_t>(s[1] | (s[2] << 8));
		}
		pcm[i] = static_cast<int16_t>(sum / channels);
	}
	return true;
}

// Converts [pcm] from [from_rate] to [to_rate] (averaging when going down, linear when going up)
inline std::vector<int16_t> resample(const std::vector<int16_t>& pcm, int from_rate, int to_rate)
{
	if (from_rate == to_rate || pcm.empty() || from_rate <= 0)
		return pcm;

	const double         step = double(from_rate) / to_rate;
	const size_t         n    = static_cast<size_t>(pcm.size() / step);
	std::vector<int16_t> out(n);
	for (size_t i = 0; i < n; i++)
	{
		const double a = i * step, b = (i + 1) * step;
		if (step > 1)
		{
			// Average the source samples covered by this output sample
			double sum = 0, w = 0;
			for (size_t s = static_cast<size_t>(a); s < pcm.size() && s < b; s++)
			{
				const double wt = std::min<double>(b, s + 1) - std::max<double>(a, s);
				sum += pcm[s] * wt;
				w += wt;
			}
			out[i] = detail::clamp16(static_cast<int64_t>(std::lround(w > 0 ? sum / w : 0)));
		}
		else
		{
			const size_t s0 = static_cast<size_t>(a), s1 = std::min(s0 + 1, pcm.size() - 1);
			const double f = a - s0;
			out[i]         = detail::clamp16(static_cast<int64_t>(std::lround(pcm[s0] * (1 - f) + pcm[s1] * f)));
		}
	}
	return out;
}
} // namespace slade::wess
