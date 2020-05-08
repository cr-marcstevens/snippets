/***************************************************************************************\
*                                                                                      *
* https://github.com/cr-marcstevens/snippets/tree/master/cxxheaderonly                 *
*                                                                                      *
* parallel_algorithms.hpp - A header only C++ light-weight parallel algorithms library *
* Copyright (c) 2020 Marc Stevens                                                      *
*                                                                                      *
* MIT License                                                                          *
*                                                                                      *
* Permission is hereby granted, free of charge, to any person obtaining a copy         *
* of this software and associated documentation files (the "Software"), to deal        *
* in the Software without restriction, including without limitation the rights         *
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell            *
* copies of the Software, and to permit persons to whom the Software is                *
* furnished to do so, subject to the following conditions:                             *
*                                                                                      *
* The above copyright notice and this permission notice shall be included in all       *
* copies or substantial portions of the Software.                                      *
*                                                                                      *
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR           *
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,             *
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE          *
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER               *
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,        *
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE        *
* SOFTWARE.                                                                            *
*                                                                                      *
\**************************************************************************************/

#ifndef PARALLEL_ALGORITHMS_HPP
#define PARALLEL_ALGORITHMS_HPP

#include "thread_pool.hpp"
#include <cassert>

/*************************** example usage ***************************************\
grep "^//test.cpp" parallel_algoritms.hpp -A34 > test.cpp
g++ -std=c++11 -o test test.cpp -pthread -lpthread

//test.cpp:
#include "thread_pool.hpp"
#include <iostream>

int main()
{
	// use main thread also as worker using wait_work(), so init 1 less in thread pool
	// (alternatively use wait_sleep() and make threads for all logical hardware cores)
    thread_pool::thread_pool tp(std::thread::hardware_concurrency() - 1);

}

\************************* end example usage *************************************/

namespace parallel_algorithms {

	// template<typename RandIt, typename Compare, typename Threadpool>
	// void nth_element(RandIt first, RandIt nth, RandIt last, Compare cf, Threadpool& threadpool)

	class range_iterator
		: public std::iterator<std::random_access_iterator_tag, const std::size_t>
	{
		std::size_t _i;

	public:
		range_iterator(std::size_t i = 0): _i(i) {}

		bool operator== (const range_iterator& r) const { return _i == r._i; }
		bool operator!= (const range_iterator& r) const { return _i != r._i; }
		bool operator<  (const range_iterator& r) const { return _i <  r._i; }
		bool operator<= (const range_iterator& r) const { return _i <= r._i; }
		bool operator>  (const range_iterator& r) const { return _i >  r._i; }
		bool operator>= (const range_iterator& r) const { return _i >= r._i; }
		
		const std::size_t operator*() const { return _i; }
		const std::size_t* operator->() const { return &_i; }
		
		range_iterator& operator++() { ++_i; return *this; }
		range_iterator& operator--() { --_i; return *this; }
		range_iterator& operator+=(std::size_t n) { _i += n; return *this; }
		range_iterator& operator-=(std::size_t n) { _i -= n; return *this; }

		range_iterator operator++(int) { range_iterator copy(_i); ++_i; return copy; }
		range_iterator operator--(int) { range_iterator copy(_i); --_i; return copy; }
		
		range_iterator operator+(std::size_t n) const { return range_iterator(_i + n); }
		range_iterator operator-(std::size_t n) const { return range_iterator(_i - n); }
		std::size_t operator-(const range_iterator& r) const { return _i - r._i; }
		
		const std::size_t operator[](std::size_t n) const { return _i + n; }
	};
	
	// divide integer interval [0,size-1] equally into n intervals and return iterators for i-th subinterval
	class subinterval {
	public:
		subinterval(const std::size_t size = 0, const std::size_t i = 0, const std::size_t n = 1)
		{
			assert(i < n);
			const std::size_t div = size/n, rem = size%n;
			_begin = range_iterator(i*div + std::min(i,rem));
			_end = range_iterator((i+1)*div + std::min(i+1,rem));
		}
		range_iterator begin() const { return _begin; }
		range_iterator end() const { return _end; }
	private:
		range_iterator _begin, _end;
	};


	template<typename RandIt, typename Pred, typename Threadpool>
	RandIt partition(RandIt first, RandIt last, Pred pred, Threadpool& threadpool, const std::size_t chunksize = 1024)
	{
		typedef typename std::iterator_traits<RandIt>::difference_type difference_type;
		typedef typename std::iterator_traits<RandIt>::value_type value_type;

		const std::size_t dist = last-first;

		unsigned nr_threads = std::min(threadpool.size()+1, dist/(chunksize*2) );
		if (nr_threads <= 2 || dist <= chunksize*4)
			return std::partition(first, last, pred);

		typedef std::pair<std::size_t,std::size_t> size_pair;
		std::vector< size_pair > low_false_interval(nr_threads, size_pair(dist,dist));
		std::vector< size_pair > high_true_interval(nr_threads, size_pair(dist,dist));

		// we know there is enough room to give two chunks per thread (one at the begin, one at the end) at the start
		// low and high point to the *beginning* of the next available chunk, so low<=high
		std::atomic_size_t low(nr_threads*chunksize), high(dist - ((nr_threads+1)*chunksize));
		std::atomic_size_t usedchunks(2*nr_threads);
		const std::size_t availablechunks=dist/chunksize;

		// each thread processes on a 'low' and a 'high' chunk, obtaining a new chunk whenever one is fully processed.
		threadpool.run([&,first,last,dist,pred,chunksize,availablechunks](int thi, int thn)
			{
				std::size_t mylow = thi*chunksize, myhigh = dist-(thi+1)*chunksize;
				auto lowfirst=first+mylow, lowlast=lowfirst+chunksize, lowit=lowfirst;
				auto highfirst=first+myhigh, highlast=highfirst+chunksize, highit=highfirst;
				value_type tmp;

				while (true)
				{
					for (; lowit != lowlast; ++lowit)
					{
						if (true == pred(*lowit))
							continue;
						for (; highit != highlast && false == pred(*highit); ++highit)
							;
						if (highit == highlast)
							break;
						std::iter_swap(lowit,highit);
						++highit;
					}
					if (lowit == lowlast)
					{
						if (usedchunks.fetch_add(1) < availablechunks)
						{
							mylow = low.fetch_add(chunksize);
							lowit = lowfirst = first+mylow; lowlast = lowfirst+chunksize;
						} else
							break;
					}
					if (highit == highlast)
					{
						if (usedchunks.fetch_add(1) < availablechunks)
						{
							myhigh = high.fetch_sub(chunksize);
							highit = highfirst = first+myhigh; highlast = highfirst+chunksize;
						} else
							break;
					}
				}

				if (lowit != lowlast)
				{
					auto lm = std::partition(lowit, lowlast, pred);
					low_false_interval[thi] = size_pair(lm-first, lowlast-first);
				} else
					low_false_interval[thi] = size_pair(0,0);

				if (highit != highlast)
				{
					auto hm = std::partition(highit, highlast, pred);
					high_true_interval[thi] = size_pair(highit-first, hm-first);
				} else
					high_true_interval[thi] = size_pair(0,0);
			}, nr_threads);

		assert(low <= high+chunksize);

		std::sort( low_false_interval.begin(), low_false_interval.end(), [](size_pair l,size_pair r){return l.first < r.first;});
		std::sort( high_true_interval.begin(), high_true_interval.end(), [](size_pair l,size_pair r){return l.first < r.first;});

		// current status:
		// on range [0,mid)  : pred(*x)=true unless x in some low_todo_interval
		// on range [mid,end): pred(*x)=false unless x in some high_todo_interval
		std::size_t mid = std::partition(first+low, first+high+chunksize, pred) - first;

		// compute the final middle
		std::size_t realmid = mid;
		for (auto& be : low_false_interval)
			realmid -= be.second - be.first;
		for (auto& be : high_true_interval)
			realmid += be.second - be.first;

		// compute the remaining intervals to swap
		std::vector< size_pair > toswap_false, toswap_true;
		std::sort( low_false_interval.begin(), low_false_interval.end() );
		std::sort( high_true_interval.begin(), high_true_interval.end() );
		
		std::size_t lowdone = 0;
		for (auto& be : low_false_interval)
		{
			if (be.second - be.first == 0)
				continue;
			// [lowdone, be.first): pred=true
			// [be.first, be.second): pred=false
			if (realmid < be.first)
			{
				// we only have to swap [lowdone, be.first) intersect [realmid,be.first)
				if (lowdone < be.first)
					toswap_true.emplace_back(std::max(lowdone,realmid),be.first);
			} else {
				// we only have to swap [be.first, be.second) intersect [be.first, realmid)
				if (realmid > be.first);
					toswap_false.emplace_back(be.first,std::min(be.second,realmid));
			}
			lowdone = be.second;
		}
		// [lowdone,mid): pred=true
		if (realmid < mid)
		{
			// we have to swap [lowdone,mid) intersect [realmid,mid)
			if (lowdone < mid)
				toswap_true.emplace_back(std::max(lowdone,realmid),mid);
		}

		std::size_t highdone = mid;
		for (auto& be : high_true_interval)
		{
			if (be.second - be.first == 0)
				continue;
			// [highdone,be.first): pred=false
			// [be.first, be.second): pred=true
			if (highdone < realmid && highdone < be.first)
			{
				// we have to swap [highdone,be.first) intersect [highdone,realmid)
				toswap_false.emplace_back(highdone, std::min(be.first, realmid));
			}
			if (realmid < be.second)
			{
				// we have to swap [be.first, be.second) intersect [realmid, be.second)
				toswap_true.emplace_back(std::max(be.first,realmid), be.second);
			}
			highdone = be.second;
		}
		// [highdone,last): pred=false
		if (realmid > highdone)
			toswap_false.emplace_back(highdone, realmid);

		// swap the remaining intervals
		while (!toswap_false.empty() && !toswap_true.empty())
		{
			auto& swf = toswap_false.back();
			auto& swt = toswap_true.back();
			assert(swf.first <= swf.second);
			assert(swt.first <= swt.second);
			std::size_t count = std::min(swf.second-swf.first, swt.second-swt.first);
			std::swap_ranges(first+swf.first, first+(swf.first+count), first+swt.first);
			swf.first += count;
			swt.first += count;
			if (swf.first == swf.second)
				toswap_false.pop_back();
			if (swt.first == swt.second)
				toswap_true.pop_back();
		}
		assert(toswap_false.empty() && toswap_true.empty());
		return first+realmid;
	}
	
	template<typename RandIt, typename Compare, typename Threadpool>
	void nth_element(RandIt first, RandIt nth, RandIt last, Compare cf, Threadpool& threadpool, std::size_t chunksize = 1024)
	{
		typedef typename std::iterator_traits<RandIt>::difference_type difference_type;
		typedef typename std::iterator_traits<RandIt>::value_type value_type;
		while (true)
		{
			assert(first <= nth && nth < last);
			
			difference_type len = last - first;
			if (len <= chunksize*4)
			{
				std::nth_element(first, nth, last, cf);
				return;
			}

			// select a small constant number of elements
			const std::size_t selectionsize = 7;
			RandIt selit = first;
			for (std::size_t i = 0; i < selectionsize; ++i,++selit)
				std::iter_swap(selit, first + (rand()%len));
			std::sort(first, selit, cf);
			
			// pick median as pivot and move to end
			RandIt pivot = last-1;
			std::iter_swap(first+selectionsize/2, pivot);
			auto mid = partition(first, pivot, [cf,pivot](const value_type& r){ return cf(r, *pivot); }, threadpool, chunksize);
			
			if (nth < mid)
				last = mid;
			else
				first = mid;
		}
	}
	
	template<typename RandIt, typename Threadpool>
	void nth_element(RandIt first, RandIt nth, RandIt last, Threadpool& threadpool, std::size_t chunksize = 1024)
	{
		typedef typename std::iterator_traits<RandIt>::value_type value_type;
		nth_element(first, nth, last, std::less<value_type>(), threadpool, chunksize);
	}


	template<typename RandIt, typename Compare, typename Threadpool>
	RandIt merge(RandIt first1, RandIt last1, RandIt first2, RandIt last2, RandIt dest, Compare cf, Threadpool& threadpool)
	{
		typedef typename std::iterator_traits<RandIt>::difference_type difference_type;
		typedef typename std::iterator_traits<RandIt>::value_type value_type;
		const std::size_t minchunksize = 4096;

		difference_type size1 = last1-first1, size2=last2-first2;
		if (size1+size2 < 4*minchunksize)
			return std::merge(first1, last1, first2, last2, dest, cf);
		if (size1 < size2)
			return merge(first2, last2, first1, last1, dest, cf, threadpool);

		const std::size_t threads = std::min(threadpool.size()+1, size1/minchunksize);

		threadpool.run([=](int thi, int thn)
			{
				subinterval iv1(size1, thi, thn);
				RandIt iv1first=first1+*iv1.begin(), iv1last=first1+*iv1.end();
				RandIt iv2first=first2, iv2last=last2;
				if (thi>0)
					iv2first=std::lower_bound(first2, last2, *iv1first, cf);
				if (thi<thn-1)
					iv2last=std::lower_bound(first2, last2, *iv1last, cf);
				RandIt d = dest+(*iv1.begin()+(iv2first-first2));
				if (iv2first == iv2last)
				{
					std::copy(iv1first,iv1last,d);
					return;
				}
				while (true)
				{
					if (cf(*iv1first,*iv2first))
					{
						*d = *iv1first; ++d;
						if (++iv1first == iv1last)
						{
							std::copy(iv2first,iv2last,d);
							return;
						}
					} else
					{
						*d = *iv2first; ++d;
						if (++iv2first == iv2last)
						{
							std::copy(iv1first,iv1last,d);
							return;
						}
					}
				}
			});
		return dest+(size1+size2);
	}

	template<typename RandIt, typename Threadpool>
	RandIt merge(RandIt first1, RandIt last1, RandIt first2, RandIt last2, RandIt dest, Threadpool& threadpool)
	{
		typedef typename std::iterator_traits<RandIt>::value_type value_type;
		return merge(first1, last1, first2, last2, dest, std::less<value_type>(), threadpool);
	}
	
} // namespace parallel_algorithms

#endif // PARALLEL_ALGORITHMS
