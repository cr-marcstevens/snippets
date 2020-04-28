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
	RandIt partition(RandIt first, RandIt last, Pred pred, Threadpool& threadpool, const std::size_t chunksize = 4096)
	{
		const std::size_t dist = last-first;

		unsigned nr_threads = std::min(threadpool.size()+1, (dist-chunksize)/(chunksize*2) );
		if (nr_threads <= 1 || dist <= chunksize*4)
			return std::partition(first, last, pred);
		
		std::pair<std::size_t,std::size_t> low_todo_interval[nr_threads], high_todo_interval[nr_threads];
		std::fill(low_todo_interval+0, low_todo_interval+nr_threads, std::pair<std::size_t,std::size_t>(0,0));
		std::fill(high_todo_interval+0, high_todo_interval+nr_threads, std::pair<std::size_t,std::size_t>(0,0));
		
		std::atomic_size_t low(nr_threads*chunksize), high(dist - (nr_threads+1)*chunksize);
		threadpool.run([=,&low_todo_interval,&high_todo_interval,&low,&high](int i, int n)
			{
				low_todo_interval[i] = high_todo_interval[i] = std::pair<std::size_t,std::size_t>(0,0);
				std::size_t mylow = i*chunksize, myhigh = dist - (i+1)*chunksize;
				auto mylowit = first+mylow;
				auto myhighit = first+myhigh;

				int l = 0, h = chunksize-1;
				bool stop = false;
				while (true)
				{
					// scan for next low element with pred = false
					while (true)
					{
						for (; l < chunksize; ++l)
							if (false == pred(*(mylowit+l)))
								break;
						if (l < chunksize)
							break;
						mylow = low.fetch_add(chunksize);
						if (mylow + chunksize >= high) { stop = true; low -= chunksize; break; } // stopping condition
						mylowit = first+mylow;
						l = 0;
					}
					// scan for next high element with pred = true
					while (true)
					{
						for (; h >= 0; --h)
							if (true == pred(*(myhighit+h)))
								break;
						if (h >= 0)
							break;
						myhigh = high.fetch_sub(chunksize);
						if (low + chunksize >= myhigh) { stop = true; high += chunksize; break; } // stopping condition
						myhighit = first+myhigh;
						h = chunksize - 1;
					}
					if (!stop)
					{
						std::iter_swap(mylowit+l, myhighit+h);
						++l; --h;
						continue;
					}
					// we have to stop, so we store any unprocessed intervals 
					if (l < chunksize)
					{
						auto it = std::partition(mylowit+l, mylowit+chunksize, pred);
						l = it - mylowit;
						if (l < chunksize)
							low_todo_interval[i] = std::pair<std::size_t,std::size_t>(mylow+l, mylow+chunksize);
					}
					if (h >= 0)
					{
						auto it = std::partition(myhighit, myhighit+h+1, pred);
						h = it - myhighit;
						if (h > 0)
							high_todo_interval[i] = std::pair<std::size_t,std::size_t>(myhigh, myhigh+h);
					}
					return;
				}
			}, nr_threads);

		// swap between lowtodo and hightodo ranges
		std::sort( low_todo_interval+0, low_todo_interval+nr_threads );
		std::sort( high_todo_interval+0, high_todo_interval+nr_threads );
		
		int li = nr_threads - 1, hi = 0;
		while (li >=0 && hi < nr_threads)
		{
			// find next non-empty low interval
			for (; li >= 0; --li)
				if (low_todo_interval[li].first != low_todo_interval[li].second)
					break;
			// find next non-empty high interval
			for (; hi < nr_threads; ++hi)
				if (high_todo_interval[hi].first != high_todo_interval[hi].second)
					break;
			if (hi >= nr_threads) break;
			if (li < 0) break;

			std::size_t lowbeg = low_todo_interval[li].first, lowend = low_todo_interval[li].second, lowsize = lowend-lowbeg;
			std::size_t highbeg = high_todo_interval[hi].first, highend = high_todo_interval[hi].second, highsize = highend-highbeg;
			std::size_t size = std::min(lowsize, highsize);
			assert(lowend < mid);
			assert(mid < highbeg);
			assert(lowsize <= chunksize);
			assert(highsize <= chunksize);

			std::swap_ranges(first + lowbeg, first + (lowbeg+size), first + (highbeg + highsize-size));
			low_todo_interval[li].first += size;
			high_todo_interval[hi].second -= size;
		}
		
		// now there are either no lowtodos or no hightodos anymore
		auto it = std::partition(first+low, first+high+chunksize, pred);
		std::size_t mid = it-first;
		
		// process remaining lowtodos with respect to mid
		for (; li >= 0; --li)
		{
			std::size_t lowbeg = low_todo_interval[li].first, lowend = low_todo_interval[li].second, lowsize = lowend-lowbeg;
			if (lowsize == 0)
				continue;
			std::size_t gapsize = mid-lowend;
			if (gapsize < lowsize)
			{
				// swap [lowend,mid) with [lowbeg,lowbeg+gapsize)
				std::swap_ranges(first+lowend, first+mid, first+lowbeg);
				mid = lowbeg + gapsize;
			}
			else
			{
				// swap [lowbeg,lowend) with [mid-lowsize,mid)
				std::swap_ranges(first+lowbeg, first+lowend, first+(mid-lowsize));
				mid -= lowsize;
			}
		}

		// process remaining hightodos with respect to mid
		for (; hi < nr_threads; ++hi)
		{
			std::size_t highbeg = high_todo_interval[hi].first, highend = high_todo_interval[hi].second, highsize = highend-highbeg;
			if (highsize == 0)
				continue;
			std::size_t gapsize = highbeg-mid;
			if (gapsize < highsize)
			{
				// swap [mid,highbeg) with [..,highend)
				std::swap_ranges(first+mid, first+highbeg, first+(highend-gapsize));
				mid = highend - gapsize;
			}
			else
			{
				// swap [highbeg,highend) with [mid,mid+highsize)
				std::swap_ranges(first+highbeg, first+highend, first+mid);
				mid += highsize;
			}
		}

		return first+mid;
	}
	
	template<typename RandIt, typename Compare, typename Threadpool>
	void nth_element(RandIt first, RandIt nth, RandIt last, Compare cf, Threadpool& threadpool, std::size_t chunksize = 4096)
	{
		typedef typename std::iterator_traits<RandIt>::difference_type difference_type;
		typedef typename std::iterator_traits<RandIt>::value_type value_type;
		std::size_t threads = threadpool.size()+1;
		while (true)
		{
			assert(first <= nth);
			assert(nth < last);
			
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
			auto mid = partition(first, pivot, [=](const value_type& r){ return cf(r, *pivot); }, threadpool, chunksize);
			
			if (nth < mid)
				last = mid;
			else
				first = mid;
		}
	}
	
	template<typename RandIt, typename Threadpool>
	void nth_element(RandIt first, RandIt nth, RandIt last, Threadpool& threadpool, std::size_t chunksize = 4096)
	{
		typedef typename std::iterator_traits<RandIt>::value_type value_type;
		nth_element(first, nth, last, std::less<value_type>(), threadpool, chunksize);
	}
	
} // namespace parallel_algorithms

#endif // PARALLEL_ALGORITHMS
