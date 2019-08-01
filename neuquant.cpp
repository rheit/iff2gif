/* NeuQuant Neural-Net Quantization Algorithm
 * ------------------------------------------
 *
 * Copyright (c) 1994 Anthony Dekker
 *
 * NEUQUANT Neural-Net quantization algorithm by Anthony Dekker, 1994.
 * See "Kohonen neural networks for optimal colour quantization"
 * in "Network: Computation in Neural Systems" Vol. 5 (1994) pp 351-367.
 * for a discussion of the algorithm.
 * See also  http://www.acm.org/~dekker/NEUQUANT.HTML
 *
 * Any party obtaining a copy of these files from the author, directly or
 * indirectly, is granted, free of charge, a full and unrestricted irrevocable,
 * world-wide, paid up, royalty-free, nonexclusive right and license to deal
 * in this software and documentation files (the "Software"), including without
 * limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons who receive
 * copies from any such party to do so, with the only requirement being
 * that this copyright notice remain intact.
 */

#include <assert.h>
#include "iff2gif.h"

static constexpr int ncycles = 100;			// no. of learning cycles

static constexpr int netsize = 256;		// number of colours used
static constexpr int specials = 3;		// number of reserved colours used
static constexpr int bgColour = specials - 1;	// reserved background colour
static constexpr int cutnetsize = netsize - specials;
static constexpr int maxnetpos = netsize - 1;

static constexpr int initrad = netsize / 8;   // for 256 cols, radius starts at 32
static constexpr int radiusbiasshift = 6;
static constexpr int radiusbias = 1 << radiusbiasshift;
static constexpr int initBiasRadius = initrad * radiusbias;
static constexpr int radiusdec = 30; // factor of 1/30 each cycle

static constexpr int alphabiasshift = 10;			// alpha starts at 1
static constexpr int initalpha = 1 << alphabiasshift; // biased by 10 bits

static constexpr double gamma = 1024.0;
static constexpr double beta = 1.0 / 1024.0;
static constexpr double betagamma = beta * gamma;


class NeuQuant
{
	private: double network[netsize][3]; // the network itself
	protected: int colormap[netsize][4]; // the network itself

private:
	int netindex[256]; // for network lookup - really 256

	double bias[netsize];  // bias and freq arrays for learning
	double freq[netsize];

	// four primes near 500 - assume no image has a length so large
	// that it is divisible by all four primes

public:
	static constexpr int prime1 = 499;
	static constexpr int prime2 = 491;
	static constexpr int prime3 = 487;
	static constexpr int prime4 = 503;
	static constexpr int maxprime = prime4;

protected: const ChunkyBitmap &pixels;
private: int samplefac = 0;

public:
	NeuQuant(const ChunkyBitmap &im)
	: NeuQuant(1, im) {
	}

	NeuQuant(int sample, const ChunkyBitmap &im)
	: pixels(im), samplefac(sample) {
		if (sample < 1 || sample > 30) throw std::out_of_range("Sample must be 1..30");
		if (im.Width * im.Height < maxprime) throw std::domain_error("Image is too small");
		assert(im.BytesPerPixel == 4);
		setUpArrays();
	}

	int getColorCount() const {
		return netsize;
	}

	ColorRegister getColor(int i) const {
		if (i < 0 || i >= netsize) return { 0, 0, 0 };
		int bb = colormap[i][0];
		int gg = colormap[i][1];
		int rr = colormap[i][2];
		return { rr, gg, bb };
	}

	std::vector<ColorRegister> getPalette() const {
		std::vector<ColorRegister> pal(netsize);
		for (int i = 0; i < netsize; ++i)
		{
			pal[i] = ColorRegister(colormap[i][2], colormap[i][1], colormap[i][0]);
		}
		return pal;
	}

	void setUpArrays() {
		network[0][0] = 0.0;	// black
		network[0][1] = 0.0;
		network[0][2] = 0.0;

		network[1][0] = 255.0;	// white
		network[1][1] = 255.0;
		network[1][2] = 255.0;

		// RESERVED bgColour	// background

		for (int i = 0; i < specials; i++) {
			freq[i] = 1.0 / netsize;
			bias[i] = 0.0;
		}

		for (int i = specials; i < netsize; i++) {
			double *p = network[i];
			p[0] = (255.0 * (i - specials)) / cutnetsize;
			p[1] = (255.0 * (i - specials)) / cutnetsize;
			p[2] = (255.0 * (i - specials)) / cutnetsize;

			freq[i] = 1.0 / netsize;
			bias[i] = 0.0;
		}
	}

	void init() {
		learn();
		fix();
		inxbuild();
	}

private:
	void altersingle(double alpha, int i, double b, double g, double r) {
		// Move neuron i towards biased (b,g,r) by factor alpha
		double *n = network[i];				// alter hit neuron
		n[0] -= (alpha * (n[0] - b));
		n[1] -= (alpha * (n[1] - g));
		n[2] -= (alpha * (n[2] - r));
	}

	void alterneigh(double alpha, int rad, int i, double b, double g, double r) {
		int lo = i - rad;   if (lo < specials - 1) lo = specials - 1;
		int hi = i + rad;   if (hi > netsize) hi = netsize;

		int j = i + 1;
		int k = i - 1;
		int q = 0;
		while ((j < hi) || (k > lo)) {
			double a = (alpha * (rad * rad - q * q)) / (rad * rad);
			q++;
			if (j < hi) {
				double *p = network[j];
				p[0] -= (a * (p[0] - b));
				p[1] -= (a * (p[1] - g));
				p[2] -= (a * (p[2] - r));
				j++;
			}
			if (k > lo) {
				double *p = network[k];
				p[0] -= (a * (p[0] - b));
				p[1] -= (a * (p[1] - g));
				p[2] -= (a * (p[2] - r));
				k--;
			}
		}
	}

	int contest(double b, double g, double r) {    // Search for biased BGR values
		// finds closest neuron (min dist) and updates freq 
		// finds best neuron (min dist-bias) and returns position 
		// for frequently chosen neurons, freq[i] is high and bias[i] is negative 
		// bias[i] = gamma*((1/netsize)-freq[i]) 

		double bestd = DBL_MAX;
		double bestbiasd = bestd;
		int bestpos = -1;
		int bestbiaspos = bestpos;

		for (int i = specials; i < netsize; i++) {
			double *n = network[i];
			double dist = abs(n[0] - b) + abs(n[1] - g) + abs(n[2] - r);
			if (dist < bestd) { bestd = dist; bestpos = i; }
			double biasdist = dist - bias[i];
			if (biasdist < bestbiasd) { bestbiasd = biasdist; bestbiaspos = i; }
			freq[i] -= beta * freq[i];
			bias[i] += betagamma * freq[i];
		}
		freq[bestpos] += beta;
		bias[bestpos] -= betagamma;
		return bestbiaspos;
	}

	int specialFind(double b, double g, double r) const {
		for (int i = 0; i < specials; i++) {
			const double *n = network[i];
			if (n[0] == b && n[1] == g && n[2] == r) return i;
		}
		return -1;
	}

	void learn() {
		int biasRadius = initBiasRadius;
		int alphadec = 30 + ((samplefac - 1) / 3);
		int lengthcount = pixels.Width * pixels.Height;
		int samplepixels = lengthcount / samplefac;
		int delta = samplepixels / ncycles;
		int alpha = initalpha;

		int i = 0;
		int rad = biasRadius >> radiusbiasshift;
		if (rad <= 1) rad = 0;

		fprintf(stderr, "beginning 1D learning: samplepixels=%d  rad=%d\n", samplepixels, rad);

		int step = 0;
		int pos = 0;

		if ((lengthcount % prime1) != 0) step = prime1;
		else {
			if ((lengthcount % prime2) != 0) step = prime2;
			else {
				if ((lengthcount % prime3) != 0) step = prime3;
				else step = prime4;
			}
		}

		i = 0;
		while (i < samplepixels) {
			const uint8_t *p = &pixels.Pixels[pos * 4];
			double b = p[2];
			double g = p[1];
			double r = p[0];

			if (i == 0) {   // remember background colour
				network[bgColour][0] = b;
				network[bgColour][1] = g;
				network[bgColour][2] = r;
			}

			int j = specialFind(b, g, r);
			j = j < 0 ? contest(b, g, r) : j;

			if (j >= specials) {   // don't learn for specials
				double a = (1.0 * alpha) / initalpha;
				altersingle(a, j, b, g, r);
				if (rad > 0) alterneigh(a, rad, j, b, g, r);   // alter neighbours
			}

			pos += step;
			while (pos >= lengthcount) pos -= lengthcount;

			i++;
			if (i % delta == 0) {
				alpha -= alpha / alphadec;
				biasRadius -= biasRadius / radiusdec;
				rad = biasRadius >> radiusbiasshift;
				if (rad <= 1) rad = 0;
			}
		}
		fprintf(stderr, "finished 1D learning: final alpha=%f!\n", (1.0 * alpha) / initalpha);
	}

	void fix() {
		for (int i = 0; i < netsize; i++) {
			for (int j = 0; j < 3; j++) {
				int x = (int)(0.5 + network[i][j]);
				if (x < 0) x = 0;
				if (x > 255) x = 255;
				colormap[i][j] = x;
			}
			colormap[i][3] = i;
		}
	}

	void inxbuild() {
		// Insertion sort of network and building of netindex[0..255]

		int previouscol = 0;
		int startpos = 0;

		for (int i = 0; i < netsize; i++) {
			int *p = colormap[i];
			int *q = nullptr;
			int smallpos = i;
			int smallval = p[1];			// index on g
			// find smallest in i..netsize-1
			for (int j = i + 1; j < netsize; j++) {
				q = colormap[j];
				if (q[1] < smallval) {		// index on g
					smallpos = j;
					smallval = q[1];	// index on g
				}
			}
			q = colormap[smallpos];
			// swap p (i) and q (smallpos) entries
			if (i != smallpos) {
				int j = q[0];   q[0] = p[0];   p[0] = j;
				j = q[1];   q[1] = p[1];   p[1] = j;
				j = q[2];   q[2] = p[2];   p[2] = j;
				j = q[3];   q[3] = p[3];   p[3] = j;
			}
			// smallval entry is now in position i
			if (smallval != previouscol) {
				netindex[previouscol] = (startpos + i) >> 1;
				for (int j = previouscol + 1; j < smallval; j++) netindex[j] = i;
				previouscol = smallval;
				startpos = i;
			}
		}
		netindex[previouscol] = (startpos + maxnetpos) >> 1;
		for (int j = previouscol + 1; j < 256; j++) netindex[j] = maxnetpos; // really 256
	}

public:
	int lookup(ColorRegister c) const {
		return inxsearch(c.blue, c.green, c.red);
	}

	int lookup(bool rgb, int x, int g, int y) const {
		return rgb ? inxsearch(y, g, x) : inxsearch(x, g, y);
	}

protected:
	int inxsearch(int b, int g, int r) const {
		// Search for BGR values 0..255 and return colour index
		int bestd = 1000;		// biggest possible dist is 256*3
		int best = -1;
		int i = netindex[g];	// index on g
		int j = i - 1;		// start at netindex[g] and work outwards

		while ((i < netsize) || (j >= 0)) {
			if (i < netsize) {
				const int *p = colormap[i];
				int dist = p[1] - g;		// inx key
				if (dist >= bestd) i = netsize;	// stop iter
				else {
					if (dist < 0) dist = -dist;
					dist += abs(p[0] - b);
					if (dist < bestd) {
						dist += abs(p[2] - r);
						if (dist < bestd) { bestd = dist; best = i; }
					}
					i++;
				}
			}
			if (j >= 0) {
				const int *p = colormap[j];
				int dist = g - p[1]; // inx key - reverse dif
				if (dist >= bestd) j = -1; // stop iter
				else {
					if (dist < 0) dist = -dist;
					dist += abs(p[0] - b);
					if (dist < bestd) {
						dist += abs(p[2] - r);
						if (dist < bestd) { bestd = dist; best = j; }
					}
					j--;
				}
			}
		}

		return best;
	}

};

std::vector<ColorRegister> ChunkyBitmap::NeuQuant(int maxcolors) const
{
	assert(BytesPerPixel == 4);
	::NeuQuant nq(*this);
	nq.init();
	return nq.getPalette();
}
