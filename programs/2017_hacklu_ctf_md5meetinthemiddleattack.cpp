/*

Solution for Hack.LU 2017 CTF:
https://notmydigest.flatearth.fluxfingers.net/index.php

MIT License

Copyright (c) 2017:
Marc Stevens
Cryptology Group
Centrum Wiskunde & Informatica
P.O. Box 94079, 1090 GB Amsterdam, Netherlands
marc@marc-stevens.nl

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdlib.h>
#include <iomanip>
#include <iostream>
#include <string>

#include <array>
#include <map>
#include <vector>

#include <thread>
#include <mutex>

using namespace std;

// do a meet-in-the-middle search in three phases:
// phase 1. generate all backward outputs, store partial chaining value (CV) in a lossy hashmap (max 1 value per bucket, store only 32-bits of 128-bit CV)
// phase 2. generate all forward outputs, keep any partial matches in the hashmap: full CV together with backward input (second half secret)
// phase 3. regenerate all backward outputs, checking against candidate CV: output complete match (if any)

// Note: a lossy hashmap reduces memory usage (at least 8GB) and can be run on normal laptops and desktops, but causes a failure probability (roughly 50% ?)
// using unordered_map would not have a failure probability, but would use very large amounts of memory

/* Compilation:
g++ -O3 -march=native -std=c++11 -pthread -o notmydigest 2017_hacklu_ctf_md5meetinthemiddleattack.cpp -lpthread
*/

// do a test case recovery of secret = 13 char [a-m]
//#define DEBUG

// use all logical cores by default
// set to specific number as desired
#define THREADS (thread::hardware_concurrency())

// more memory results in a smaller failure probability
// try to use at least about 4*(36^6)~~4*2^31 bytes ~~ 8GB memory
// tweak memory use: MAJORBUCKETS * 1GB
#define MAJORBUCKETS 14

// BUCKETFIRST,BUCKETSECOND,BUCKETVALUE need to be distinct values from 0,1,2,3
// use different values for retries
#define BUCKETFIRST 0
#define BUCKETSECOND 1
#define BUCKETVALUE 2
vector<uint32_t> storage[MAJORBUCKETS];
// storage[tmpihv[BUCKETFIRST] % MAJORBUCKETS][tmpihv[BUCKETSECOND]>>4] = tmpihv[BUCKETVALUE]

// secret is 13 char [a-z0-9]
const char values[] = "abcdefghijklmnopqrstuvwxyz0123456789";
#define VALUES 36

// used target msg+secret
const string target_msg = "012345678901234567890123456789012345678901234567890123456XXXXXXXXXXXXX";

// target hash
#ifdef DEBUG
// secret = abcdefghijklm
const string target_hash_str = "e483fa4c5b386f1526d6db9ac07a2503";
#undef VALUES
#define VALUES 13
#undef MAJORBUCKETS
#define MAJORBUCKETS 1
#else
// secret = ?
const string target_hash_str = "be75f49ca582d673346bf85209aba13c";
#endif


inline uint32_t rotate_right(const uint32_t x, const unsigned n)
{
	return (x >> n) | (x << (32 - n));
}
inline uint32_t rotate_left(const uint32_t x, const unsigned n)
{
	return (x << n) | (x >> (32 - n));
}
inline uint32_t md5_ff(uint32_t b, uint32_t c, uint32_t d)
{
	return d ^ (b & (c ^ d));
}
inline uint32_t md5_gg(uint32_t b, uint32_t c, uint32_t d)
{
	return c ^ (d & (b ^ c));
}
inline uint32_t md5_hh(uint32_t b, uint32_t c, uint32_t d)
{
	return b ^ c ^ d;
}
inline uint32_t md5_ii(uint32_t b, uint32_t c, uint32_t d)
{
	return c ^ (b | ~d);
}

const array<uint32_t,4> md5iv = { 0x10325476, 0x98badcfe, 0xefcdab89, 0x67452301 };

#define HASHCLASH_MD5COMPRESS_STEP(f, a, b, c, d, m, ac, rc) \
	a += f(b, c, d) + m + ac; a = rotate_left(a,rc); a += b;

#define HASHCLASH_MD5COMPRESS_STEPINV(f, a, b, c, d, m, ac, rc) \
	a -= b; a = rotate_right(a, rc); a -= f(b, c, d) + m + ac; 

// modified md5compression function: removed davies-meyer feedforward
void md5compress(uint32_t ihv[4], const uint32_t block[16])
{
	uint32_t a = ihv[0]; uint32_t b = ihv[1]; uint32_t c = ihv[2]; uint32_t d = ihv[3];

	HASHCLASH_MD5COMPRESS_STEP(md5_ff, a, b, c, d, block[ 0], 0xd76aa478,  7);  
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, d, a, b, c, block[ 1], 0xe8c7b756, 12); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, c, d, a, b, block[ 2], 0x242070db, 17); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, b, c, d, a, block[ 3], 0xc1bdceee, 22); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, a, b, c, d, block[ 4], 0xf57c0faf,  7);  
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, d, a, b, c, block[ 5], 0x4787c62a, 12); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, c, d, a, b, block[ 6], 0xa8304613, 17); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, b, c, d, a, block[ 7], 0xfd469501, 22); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, a, b, c, d, block[ 8], 0x698098d8,  7);  
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, d, a, b, c, block[ 9], 0x8b44f7af, 12); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, c, d, a, b, block[10], 0xffff5bb1, 17);
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, b, c, d, a, block[11], 0x895cd7be, 22);
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, a, b, c, d, block[12], 0x6b901122,  7); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, d, a, b, c, block[13], 0xfd987193, 12);
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, c, d, a, b, block[14], 0xa679438e, 17);
	HASHCLASH_MD5COMPRESS_STEP(md5_ff, b, c, d, a, block[15], 0x49b40821, 22);
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, a, b, c, d, block[ 1], 0xf61e2562,  5);  
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, d, a, b, c, block[ 6], 0xc040b340,  9);  
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, c, d, a, b, block[11], 0x265e5a51, 14);
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, b, c, d, a, block[ 0], 0xe9b6c7aa, 20); 
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, a, b, c, d, block[ 5], 0xd62f105d,  5);  
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, d, a, b, c, block[10], 0x02441453,  9); 
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, c, d, a, b, block[15], 0xd8a1e681, 14);
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, b, c, d, a, block[ 4], 0xe7d3fbc8, 20); 
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, a, b, c, d, block[ 9], 0x21e1cde6,  5);  
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, d, a, b, c, block[14], 0xc33707d6,  9); 
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, c, d, a, b, block[ 3], 0xf4d50d87, 14); 
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, b, c, d, a, block[ 8], 0x455a14ed, 20); 
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, a, b, c, d, block[13], 0xa9e3e905,  5); 
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, d, a, b, c, block[ 2], 0xfcefa3f8,  9);  
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, c, d, a, b, block[ 7], 0x676f02d9, 14); 
	HASHCLASH_MD5COMPRESS_STEP(md5_gg, b, c, d, a, block[12], 0x8d2a4c8a, 20);
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, a, b, c, d, block[ 5], 0xfffa3942,  4); 
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, d, a, b, c, block[ 8], 0x8771f681, 11); 
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, c, d, a, b, block[11], 0x6d9d6122, 16);
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, b, c, d, a, block[14], 0xfde5380c, 23);
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, a, b, c, d, block[ 1], 0xa4beea44,  4);  
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, d, a, b, c, block[ 4], 0x4bdecfa9, 11); 
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, c, d, a, b, block[ 7], 0xf6bb4b60, 16); 
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, b, c, d, a, block[10], 0xbebfbc70, 23);
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, a, b, c, d, block[13], 0x289b7ec6,  4); 
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, d, a, b, c, block[ 0], 0xeaa127fa, 11); 
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, c, d, a, b, block[ 3], 0xd4ef3085, 16); 
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, b, c, d, a, block[ 6], 0x04881d05, 23); 
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, a, b, c, d, block[ 9], 0xd9d4d039,  4);  
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, d, a, b, c, block[12], 0xe6db99e5, 11);
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, c, d, a, b, block[15], 0x1fa27cf8, 16);
	HASHCLASH_MD5COMPRESS_STEP(md5_hh, b, c, d, a, block[ 2], 0xc4ac5665, 23); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, a, b, c, d, block[ 0], 0xf4292244,  6);  
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, d, a, b, c, block[ 7], 0x432aff97, 10); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, c, d, a, b, block[14], 0xab9423a7, 15);
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, b, c, d, a, block[ 5], 0xfc93a039, 21); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, a, b, c, d, block[12], 0x655b59c3,  6); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, d, a, b, c, block[ 3], 0x8f0ccc92, 10); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, c, d, a, b, block[10], 0xffeff47d, 15);
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, b, c, d, a, block[ 1], 0x85845dd1, 21); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, a, b, c, d, block[ 8], 0x6fa87e4f,  6);  
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, d, a, b, c, block[15], 0xfe2ce6e0, 10);
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, c, d, a, b, block[ 6], 0xa3014314, 15); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, b, c, d, a, block[13], 0x4e0811a1, 21);
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, a, b, c, d, block[ 4], 0xf7537e82,  6);  
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, d, a, b, c, block[11], 0xbd3af235, 10);
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, c, d, a, b, block[ 2], 0x2ad7d2bb, 15); 
	HASHCLASH_MD5COMPRESS_STEP(md5_ii, b, c, d, a, block[ 9], 0xeb86d391, 21); 

	ihv[0] = a; ihv[1] = b; ihv[2] = c; ihv[3] = d;
}

// since davies-meyer feedforward is removed, we can invert md5compression function
void md5compress_inv(uint32_t ihv[4], const uint32_t block[16])
{
	uint32_t a = ihv[0]; uint32_t b = ihv[1]; uint32_t c = ihv[2]; uint32_t d = ihv[3];

	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, b, c, d, a, block[ 9], 0xeb86d391, 21); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, c, d, a, b, block[ 2], 0x2ad7d2bb, 15); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, d, a, b, c, block[11], 0xbd3af235, 10);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, a, b, c, d, block[ 4], 0xf7537e82,  6);  
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, b, c, d, a, block[13], 0x4e0811a1, 21);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, c, d, a, b, block[ 6], 0xa3014314, 15); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, d, a, b, c, block[15], 0xfe2ce6e0, 10);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, a, b, c, d, block[ 8], 0x6fa87e4f,  6);  
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, b, c, d, a, block[ 1], 0x85845dd1, 21); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, c, d, a, b, block[10], 0xffeff47d, 15);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, d, a, b, c, block[ 3], 0x8f0ccc92, 10); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, a, b, c, d, block[12], 0x655b59c3,  6); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, b, c, d, a, block[ 5], 0xfc93a039, 21); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, c, d, a, b, block[14], 0xab9423a7, 15);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, d, a, b, c, block[ 7], 0x432aff97, 10); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ii, a, b, c, d, block[ 0], 0xf4292244,  6);  

	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, b, c, d, a, block[ 2], 0xc4ac5665, 23); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, c, d, a, b, block[15], 0x1fa27cf8, 16);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, d, a, b, c, block[12], 0xe6db99e5, 11);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, a, b, c, d, block[ 9], 0xd9d4d039,  4);  
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, b, c, d, a, block[ 6], 0x04881d05, 23); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, c, d, a, b, block[ 3], 0xd4ef3085, 16); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, d, a, b, c, block[ 0], 0xeaa127fa, 11); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, a, b, c, d, block[13], 0x289b7ec6,  4); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, b, c, d, a, block[10], 0xbebfbc70, 23);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, c, d, a, b, block[ 7], 0xf6bb4b60, 16); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, d, a, b, c, block[ 4], 0x4bdecfa9, 11); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, a, b, c, d, block[ 1], 0xa4beea44,  4);  
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, b, c, d, a, block[14], 0xfde5380c, 23);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, c, d, a, b, block[11], 0x6d9d6122, 16);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, d, a, b, c, block[ 8], 0x8771f681, 11); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_hh, a, b, c, d, block[ 5], 0xfffa3942,  4); 

	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, b, c, d, a, block[12], 0x8d2a4c8a, 20);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, c, d, a, b, block[ 7], 0x676f02d9, 14); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, d, a, b, c, block[ 2], 0xfcefa3f8,  9);  
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, a, b, c, d, block[13], 0xa9e3e905,  5); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, b, c, d, a, block[ 8], 0x455a14ed, 20); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, c, d, a, b, block[ 3], 0xf4d50d87, 14); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, d, a, b, c, block[14], 0xc33707d6,  9); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, a, b, c, d, block[ 9], 0x21e1cde6,  5);  
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, b, c, d, a, block[ 4], 0xe7d3fbc8, 20); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, c, d, a, b, block[15], 0xd8a1e681, 14);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, d, a, b, c, block[10], 0x02441453,  9); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, a, b, c, d, block[ 5], 0xd62f105d,  5);  
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, b, c, d, a, block[ 0], 0xe9b6c7aa, 20); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, c, d, a, b, block[11], 0x265e5a51, 14);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, d, a, b, c, block[ 6], 0xc040b340,  9);  
	HASHCLASH_MD5COMPRESS_STEPINV(md5_gg, a, b, c, d, block[ 1], 0xf61e2562,  5);  

	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, b, c, d, a, block[15], 0x49b40821, 22);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, c, d, a, b, block[14], 0xa679438e, 17);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, d, a, b, c, block[13], 0xfd987193, 12);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, a, b, c, d, block[12], 0x6b901122,  7); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, b, c, d, a, block[11], 0x895cd7be, 22);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, c, d, a, b, block[10], 0xffff5bb1, 17);
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, d, a, b, c, block[ 9], 0x8b44f7af, 12); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, a, b, c, d, block[ 8], 0x698098d8,  7);  
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, b, c, d, a, block[ 7], 0xfd469501, 22); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, c, d, a, b, block[ 6], 0xa8304613, 17); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, d, a, b, c, block[ 5], 0x4787c62a, 12); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, a, b, c, d, block[ 4], 0xf57c0faf,  7);  
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, b, c, d, a, block[ 3], 0xc1bdceee, 22); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, c, d, a, b, block[ 2], 0x242070db, 17); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, d, a, b, c, block[ 1], 0xe8c7b756, 12); 
	HASHCLASH_MD5COMPRESS_STEPINV(md5_ff, a, b, c, d, block[ 0], 0xd76aa478,  7);  

	ihv[0] = a; ihv[1] = b; ihv[2] = c; ihv[3] = d;
}

// be able to easy print hash
ostream& operator<<(ostream& o, const array<uint32_t,4>& v)
{
	union {
		array<uint32_t,4> a;
		char b[16];
	} h;
	h.a = v;
	for (size_t i = 0; i < 16; ++i)
		o << hex << setfill('0') << setw(2) << unsigned((unsigned char)(h.b[i])) << dec;
	return o;
}

// parse word-tuple from hashstring
array<uint32_t,4> hash_from_str(const string& s)
{
	if (s.size() != 32)
		throw runtime_error("hash string length not 32");
	union {
		array<uint32_t,4> a;
		char b[16];
	} h;
	for (size_t i = 0; i < 16; ++i)
	{
		char x1 = s[2*i], x2 = s[2*i+1];
		unsigned byte = 0;
		if (x1 >= '0' && x1 <= '9')
			byte = (unsigned)(x1-'0');
		else if (x1 >= 'a' && x1 <= 'f')
			byte = (unsigned)(x1-'a'+10);
		else if (x1 >= 'A' && x1 <= 'F')
			byte = (unsigned)(x1-'A'+10);
		else
			throw runtime_error("hex error");
		byte <<= 4;
		if (x2 >= '0' && x2 <= '9')
			byte += (unsigned)(x2-'0');
		else if (x2 >= 'a' && x2 <= 'f')
			byte += (unsigned)(x2-'a'+10);
		else if (x2 >= 'A' && x2 <= 'F')
			byte += (unsigned)(x2-'A'+10);
		else
			throw runtime_error("hex error2");
		h.b[i] = (char)(byte);
	}
	return h.a;	
}

// custom md5: reversed IV constants, no davies-meyer feedforward, but add IV to final chaining value
string blocks;
array<uint32_t,4> md5custom(const string& str)
{
	blocks = str + char(0x80);
	while (blocks.size() % 64 != 64-8)
		blocks += char(0);
	union {
		uint64_t len;
		char data[8];
	} length;
	length.len = str.size() * 8;
	blocks += string(length.data, 8);

	array<uint32_t, 4> cv = md5iv;
	cout << "cv=" << cv << endl;
	for (size_t i = 0; i < blocks.size(); i+= 64)
	{
		cout << "data=";
		for (size_t j = 0; j < 64; ++j)
			cout << hex << (uint32_t)((unsigned char)(blocks[i+j])) << " " << dec;
		cout << endl;
		md5compress(&cv[0], reinterpret_cast<const uint32_t*>(blocks.data()+i));
		cout << "cv=" << cv << endl;
	}
	for (size_t i = 0; i < 4; ++i)
		cv[i] += md5iv[i];
	cout << "hash=" << cv << endl;
	return cv;
}


// these are partial matches, including false positives, these will get final verification in third phase
map< array<uint32_t, 4>, string > candidates, solution;
mutex candidates_mutex;

void solve_challenge()
{
	struct phase_1_3
	{
		unsigned b1;
		array<uint32_t,4> cv2;

		void operator()()
		{
			string blocks2 = blocks;
			array<uint32_t, 4> tmp;
			array<char, 6> secretpart2;
			for (; b1 < VALUES; b1 += THREADS)
			{
				cout << " " << b1 << flush;
				for (unsigned b2 = 0; b2 < VALUES; ++b2)
				{
					for (unsigned b3 = 0; b3 < VALUES; ++b3)
					{
						for (unsigned b4 = 0; b4 < VALUES; ++b4)
						{
							for (unsigned b5 = 0; b5 < VALUES; ++b5)
							{
								blocks2[64 + 0] = secretpart2[0] = values[b1];
								blocks2[64 + 1] = secretpart2[1] = values[b2];
								blocks2[64 + 2] = secretpart2[2] = values[b3];
								blocks2[64 + 3] = secretpart2[3] = values[b4];
								blocks2[64 + 4] = secretpart2[4] = values[b5];
								for (unsigned b6 = 0; b6 < VALUES; ++b6)
								{
									blocks2[64 + 5] = secretpart2[5] = values[b6];
									tmp = cv2;
									md5compress_inv(&tmp[0], reinterpret_cast<const uint32_t*>(blocks2.data() + 64));
									if (candidates.empty())
									{
										// phase 1
										storage[tmp[BUCKETFIRST] % MAJORBUCKETS][tmp[BUCKETSECOND] >> 4] = tmp[BUCKETVALUE];
									}
									else
									{
										// phase 3
										if (candidates.count(tmp) > 0)
										{
											lock_guard<mutex> lcx(candidates_mutex);
											cout << endl << "Found secret: " << candidates[tmp] << string(&secretpart2[0], 6) << endl;
											solution[tmp] = candidates[tmp] + string(&secretpart2[0], 6);
										}
									}
								} // b6
							} // b5
						} // b4 
					} // b3
				} // b2
			} // b1
		} // operator()()
	}; // phase_1_3

	struct phase_2
	{
		unsigned b1;
		void operator()()
		{
			array<uint32_t,4> tmp;
			string blocks2 = blocks;
			array<char,7> secretpart1;
			for (; b1 < VALUES; b1 += THREADS )
			{
				cout << b1 << " " << flush;
				for (unsigned b2 = 0; b2 < VALUES; ++b2)
				{
					for (unsigned b3 = 0; b3 < VALUES; ++b3)
					{
						for (unsigned b4 = 0; b4 < VALUES; ++b4)
						{
							for (unsigned b5 = 0; b5 < VALUES; ++b5)
							{
								for (unsigned b6 = 0; b6 < VALUES; ++b6)
								{
									blocks2[64 - 7 + 0] = secretpart1[0] = values[b1];
									blocks2[64 - 7 + 1] = secretpart1[1] = values[b2];
									blocks2[64 - 7 + 2] = secretpart1[2] = values[b3];
									blocks2[64 - 7 + 3] = secretpart1[3] = values[b4];
									blocks2[64 - 7 + 4] = secretpart1[4] = values[b5];
									blocks2[64 - 7 + 5] = secretpart1[5] = values[b6];
									for (unsigned b7 = 0; b7 < VALUES; ++b7)
									{
										blocks2[64 - 7 + 6] = secretpart1[6] = values[b7];
										tmp = md5iv;
										md5compress(&tmp[0], reinterpret_cast<const uint32_t*>(blocks2.data()));
										if (storage[tmp[BUCKETFIRST] % MAJORBUCKETS][tmp[BUCKETSECOND] >> 4] == tmp[BUCKETVALUE] && tmp[BUCKETVALUE] > 0)
										{
											lock_guard<mutex> lcx(candidates_mutex);
											cout << endl << "Found partial match: " << tmp << " " << string(&secretpart1[0], 7) << endl;
											candidates[tmp] = string(&secretpart1[0], 7);
										}
									} // b7
								} // b6
							} // b5
						} // b4 
					} // b3
				} // b2
			} // b1
		} // operator()()
	}; // workerthread

	cout << "Setting up targetmsgstring via md5custom call:" << endl;
	array<uint32_t, 4> target_hash = hash_from_str(target_hash_str);

	array<uint32_t, 4> hash = md5custom(target_msg);

	cout << "Initializing hashmap..." << flush;
	/* build storage over 6 secret 'X' bytes in second block */
	for (unsigned i = 0; i < MAJORBUCKETS; ++i)
		storage[i].resize(1 << 28, 0);
	cout << "done." << endl;

	array<uint32_t, 4> cv2 = target_hash;
	for (unsigned i = 0; i < 4; ++i)
		cv2[i] -= md5iv[i];

	vector<thread> mythreads;
	cout << "Launching phase 1\n(fill hashmap with backward outputs of all possible last 6 bytes of secret)" << endl;
	phase_1_3 phase_1_3_threads[THREADS];
	for (unsigned i = 0; i < THREADS; ++i)
	{
		phase_1_3_threads[i].b1 = i;
		phase_1_3_threads[i].cv2 = cv2;
		mythreads.emplace_back(phase_1_3_threads[i]);
	}
	for (unsigned i = 0; i < THREADS; ++i)
		mythreads[i].join();
	mythreads.clear();
	cout << endl << "DONE!" << endl;

	cout << "Launching phase 2\n(check forward outputs of all possible first 7 bytes of secret)" << endl;
	phase_2 phase_2_threads[THREADS];
	for (unsigned i = 0; i < THREADS; ++i)
	{
		phase_2_threads[i].b1 = i;
		mythreads.emplace_back(phase_2_threads[i]);
	}
	for (unsigned i = 0; i < THREADS; ++i)
		mythreads[i].join();
	mythreads.clear();
	cout << endl << "DONE!" << endl;

	if (candidates.empty())
		throw std::runtime_error("Attack failed, try using more memory and other parameters");

	cout << "Launching phase 3\n(verify found partial matches and recover full secret)" << endl;
	for (unsigned i = 0; i < THREADS; ++i)
	{
		phase_1_3_threads[i].b1 = i;
		phase_1_3_threads[i].cv2 = cv2;
		mythreads.emplace_back(phase_1_3_threads[i]);
	}
	for (unsigned i = 0; i < THREADS; ++i)
		mythreads[i].join();
	mythreads.clear();
	cout << "DONE!" << endl;
	
	if (solution.empty())
	{
		cout << "Attack failed: try again using other BUCKET parameters in source code" << endl;
	}
	else
	{
		for (auto& s : solution)
			cout << "Solution: " << s.second << endl;
	}
}


int main(int argc, char** argv)
{
	if (argc >= 2)
	{
		string str(argv[1]);
		md5custom(str);
	}
	else
	{
		solve_challenge();
	}
	return 0;
}
