/****************************************************************************************\
*                                                                                        *
* https://github.com/cr-marcstevens/snippets/tree/master/cxxheaderonly                   *
*                                                                                        *
* concurrent_unordered_map.hpp - A header only C++ light-weight concurrent unordered map *
* Copyright (c) 2019 Marc Stevens                                                        *
*                                                                                        *
* MIT License                                                                            *
*                                                                                        *
* Permission is hereby granted, free of charge, to any person obtaining a copy           *
* of this software and associated documentation files (the "Software"), to deal          *
* in the Software without restriction, including without limitation the rights           *
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell              *
* copies of the Software, and to permit persons to whom the Software is                  *
* furnished to do so, subject to the following conditions:                               *
*                                                                                        *
* The above copyright notice and this permission notice shall be included in all         *
* copies or substantial portions of the Software.                                        *
*                                                                                        *
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR             *
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,               *
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE            *
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER                 *
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,          *
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE          *
* SOFTWARE.                                                                              *
*                                                                                        *
\****************************************************************************************/

#ifndef CONCURRENT_UNORDERED_MAP_HPP
#define CONCURRENT_UNORDERED_MAP_HPP

#include <cassert>
#include <cmath>

#include <mutex>
#include <atomic>

#include <array>
#include <unordered_map>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <functional>

/******************** example usage ***********************************\
grep "^//test.cpp" concurrent_unordered_map.hpp -A45 > test.cpp
g++ -std=c++11 -o test test.cpp -pthread -lpthread

//test.cpp:
#include "concurrent_unordered_map.hpp"
#include <iostream>

void print(const std::pair<const std::string,std::string>& p)
{
	std::cout << "- " << p.first << "=>" << p.second << std::endl;
}

int main()
{
	typedef std::unordered_map<std::string, std::string> hash_map;
	typedef concurrent_unordered_map::concurrent_unordered_map<std::string, std::string> my_hash_map;
	hash_map hm({{"abc","def"}});
	// test initialization
	my_hash_map m, m2({{{"abc"},{"def"}}}), m3(5), m4(hm.begin(),hm.end()), m5(std::move(m4));
	// test copy, move
	m3 = m2; m4 = std::move(m3); m5 = m4.locked_const_ref();
	m.lock(); m.unlock(); m.unsafe_mode(); m.safe_mode(); m5.clear(); m5.max_load_factor(0.5); m5.rehash(5); m5.reserve(5);
	std::cout << "e s ms = " << m.empty() << " " << m.size() << " " << m.max_size() << std::endl;
	// element operations
	*(m["abc"]) = "def";
	// note that operator[] and at() holds a lock until it is destroyed,
	// so printing these with a single statement will deadlock
	std::cout << "[] at c = " << *(m["abc"]) << " ";
	std::cout << *(m.at("abc")) << " ";
	std::cout << m.count("abc") << std::endl;
	// insert
	std::cout << "insert((a,b)): " << m.insert(std::pair<std::string,std::string>("a", "b")) << std::endl;
	std::cout << "insert((a,c)): " << m.insert(std::pair<std::string,std::string>("a", "c")) << std::endl;
	std::cout << "emplace((a,d)): " << m.emplace("a", "b") << std::endl;
	// erase
	{
		auto it = m.begin(); // holds lock
		it = m.end(); // releases lock to avoid deadlock
		it = m.find("a"); // holds lock
		m.erase(it);
	} // releases lock
	std::cout << "erase(a): " << m.erase("a") << std::endl;
	// for each
	std::cout << "m : " << std::endl;
	m.for_each(print);
	std::cout << "m2: " << std::endl;
	for (auto& v : m2)
		print(v);
}
\**********************************************************************/

namespace concurrent_unordered_map {

	namespace detail {

		template<typename Value, typename Mutex = std::mutex> class locked_pointer;
		template<typename Value, typename Mutex = std::mutex> class locked_reference;
		
		// Value* pointer semantics, but holds a lock as well
		// default constructable & move semantics
		// operators * ->
		template<typename Value, typename Mutex>
		class locked_pointer {
		public:
			friend class locked_reference<Value,Mutex>;
			
			typedef std::unique_lock<Mutex> unique_lock;
			locked_pointer(): _value(nullptr) {}
			locked_pointer(unique_lock&& lock, Value* value): _lock(std::move(lock)), _value(value) {}

			locked_pointer(locked_pointer&& r): _lock(std::move(r._lock)), _value(r._value) { r._value = nullptr; }
			locked_pointer& operator= (locked_pointer&& r) { _lock.swap(r._lock); std::swap(_value, r._value); return *this; }

			Value& operator*() const { assert(_value != nullptr); return *_value; }
			Value* operator->() const { assert(_value != nullptr); return _value; }

		private:
			unique_lock _lock;
			Value* _value;
		};

		// automatic conversion to Value&, but holds a lock as well
		// default constructable & move semantics
		// operator Value&(), get()
		template<typename Value, typename Mutex>
		class locked_reference {
		public:
			typedef std::unique_lock<Mutex> unique_lock;
			locked_reference(): _value(nullptr) {}
			locked_reference(unique_lock&& lock, Value& value): _lock(std::move(lock)), _value(&value) {}

			locked_reference(locked_reference&& r): _lock(std::move(r._lock)), _value(r._value) { r._value = nullptr; }
			locked_reference& operator= (locked_reference&& r) { _lock.swap(r._lock); std::swap(_value, r._value); return *this; }

			explicit locked_reference(locked_pointer<Value,Mutex>&& ptr): _lock(std::move(ptr.lock)), _value(ptr._value) { ptr._value = nullptr; }

			Value& get() const { assert(_value != nullptr); return *_value; }
			operator Value& () const { assert(_value != nullptr); return *_value; }

		private:
			unique_lock _lock;
			Value* _value;
		};

	} // namespace detail



	template<typename Key, typename T, 
		typename Cont = std::unordered_map<Key, T>,
		std::size_t Buckets = 8191, std::size_t PrimeFactor = 127>
	class concurrent_unordered_map
	{
	public:
		typedef Cont unordered_map;
		typedef typename unordered_map::key_type         key_type;
		typedef typename unordered_map::mapped_type      mapped_type;
		typedef typename unordered_map::value_type       value_type;
		typedef typename unordered_map::hasher           hasher;
		typedef typename unordered_map::key_equal        key_equal;
		typedef typename unordered_map::allocator_type   allocator_type;
		typedef typename unordered_map::reference        reference;
		typedef typename unordered_map::const_reference  const_reference;
		typedef typename unordered_map::pointer          pointer;
		typedef typename unordered_map::const_pointer    const_pointer;
		typedef typename unordered_map::size_type        size_type;
		typedef typename unordered_map::difference_type  difference_type;
		typedef typename unordered_map::iterator         base_iterator;
		typedef typename unordered_map::const_iterator   base_const_iterator;
		typedef std::mutex mutex;
		typedef std::lock_guard <mutex> lock_guard;
		typedef std::unique_lock<mutex> unique_lock;
		typedef detail::locked_pointer<mapped_type> locked_mapped_pointer;
		typedef detail::locked_pointer<const mapped_type> locked_const_mapped_pointer;

		// Operating on const concurrent_unordered_map pointer or reference is not thread_safe.
		// These types allow to lock the concurrent_unordered_map and pass a const pointer or a const reference type
		// in a thread safe manner.
		typedef std::unique_lock<concurrent_unordered_map> unique_lock_this;
		typedef detail::locked_reference<const concurrent_unordered_map,concurrent_unordered_map> locked_const_reference;
		locked_const_reference locked_const_ref() { return locked_const_reference (unique_lock_this(*this), *this); }

		
		/* empty constructors */
		concurrent_unordered_map() {}
		explicit concurrent_unordered_map(std::size_t n, 
			const hasher& hf = hasher(), const key_equal& eql = key_equal(), const allocator_type& alloc = allocator_type());
		explicit concurrent_unordered_map(const allocator_type& alloc);
		concurrent_unordered_map(std::size_t n, const allocator_type& alloc);
		concurrent_unordered_map(std::size_t n, const hasher& hf, const allocator_type& alloc);
		
		/* range constructors */
		template<class InputIterator>
		concurrent_unordered_map(InputIterator first, InputIterator last, std::size_t n = 0,
			const hasher& hf = hasher(), const key_equal& eql = key_equal(), const allocator_type& alloc = allocator_type());
		template<class InputIterator>
		concurrent_unordered_map(InputIterator first, InputIterator last, std::size_t n, const allocator_type& alloc);
		template<class InputIterator>
		concurrent_unordered_map(InputIterator first, InputIterator last, std::size_t n, const hasher& hf, const allocator_type& alloc);
		
		/* copy & move constructors */
		concurrent_unordered_map(const concurrent_unordered_map &  m); // REQUIRES m.unsafe_mode()
		concurrent_unordered_map(      concurrent_unordered_map &  m);
		concurrent_unordered_map(const locked_const_reference   &  m); // safe: proves current thread holds all locks
		concurrent_unordered_map(      concurrent_unordered_map && m);

		/* initializer list constructors */
		concurrent_unordered_map(std::initializer_list<value_type> il, std::size_t n = 0,
			const hasher& hf = hasher(), const key_equal& eql = key_equal(), const allocator_type& alloc = allocator_type());
		concurrent_unordered_map(std::initializer_list<value_type> il, std::size_t n, const allocator_type& alloc);
		concurrent_unordered_map(std::initializer_list<value_type> il, std::size_t n, const hasher& hf, const allocator_type& alloc);


		/* assign functions */
		void unsafe_assign(const concurrent_unordered_map &  m);   // NOT THREAD SAFE
		void assign       (const concurrent_unordered_map &  m);   // REQUIRES m.unsafe_mode()
		void assign       (      concurrent_unordered_map &  m);
		void assign       (      concurrent_unordered_map && m);
		void assign       (const locked_const_reference   &  ref); // safe: proves current thread holds all locks

		/* copy & move operators */
		concurrent_unordered_map& operator=(const concurrent_unordered_map &  m); // REQUIRES m.unsafe_mode()
		concurrent_unordered_map& operator=(      concurrent_unordered_map &  m);
		concurrent_unordered_map& operator=(const locked_const_reference   &  m);
		concurrent_unordered_map& operator=(      concurrent_unordered_map && m);


		/* lock & unlock of entire concurrent_unordered_map */
		void lock();
		void unlock();

		/* enter and exit unsafe mode */
		/* this will disable all internal locking */
		void unsafe_mode() { _unsafe = true; }
		void   safe_mode() { _unsafe = false; }


		/* container properties */
		hasher      hash_function() const { return _hf; }
		key_equal   key_eq()        const { return _eql; }
		bool        empty();
		bool        empty()     const; // REQUIRES unsafe_mode()
		std::size_t size();
		std::size_t size()      const; // REQUIRES unsafe_mode()
		std::size_t max_size();
		std::size_t max_size()  const; // REQUIRES unsafe_mode()

		void clear();
		void max_load_factor(float z);
		void rehash(std::size_t n);
		void reserve(std::size_t n);


		/* element operations */
		std::size_t count(const key_type& key);
		std::size_t count(const key_type& key) const; // REQUIRES unsafe_mode()

		// operator[](key): note that this returns a locked_mapped_pointer
		// which acts as a pointer to mapped_type instead of C++ standardized reference to mapped_type
		// this is necessary because it is a smart pointer that maintains the required lock
		// remember: []() creates new element if key is not found, at() throws std::out_of_range
		locked_mapped_pointer       operator[](const key_type& key);
		locked_mapped_pointer       at        (const key_type& key);
		locked_const_mapped_pointer at        (const key_type& key) const;         // REQUIRES unsafe_mode()


		// iterator and const_iterator by default use internal locking
		// both require non-const concurrent_unordered_map
		// however: when this->_unsafe == true (by calling this->lock())
		// then do not use internal locking, and also allow const_iterator from const concurrent_unordered_map
		template<typename It> class _locked_iterator;
		typedef _locked_iterator<base_iterator>       iterator;
		typedef _locked_iterator<base_const_iterator> const_iterator;
		
		      iterator  begin();
		      iterator  end();
		const_iterator  begin() const; // REQUIRES unsafe_mode()
		const_iterator  end()   const;
		const_iterator cbegin();
		const_iterator cbegin() const; // REQUIRES unsafe_mode()
		const_iterator cend()   const;


		// insert & emplace : return bool instead of pair<iterator,bool> to reduce lifetime of lock
		// call insert_std or emplace_std to obtain pair<iterator,bool>
		                                 bool insert (const value_type& val);
		template<typename P>             bool insert (P&& val);
		template<typename InputIterator> void insert (InputIterator first, InputIterator last);
		                                 void insert (std::initializer_list<value_type> il);
		template<typename... Args>       bool emplace(Args&&... args);

		// insert_std & emplace_std : standard conforming insert & emplace
		                                 std::pair<iterator,bool> insert_std (const value_type& val);
		template<typename P>             std::pair<iterator,bool> insert_std (P&& val);
		template<typename... Args>       std::pair<iterator,bool> emplace_std(Args&&... args);


		// erase
		void        erase(iterator& it);         // MODIFIES iterator, instead of returning one
		void        erase(const_iterator& it);   // MODIFIES const_iterator, instead of returning one
		std::size_t erase(const key_type& key);

		
		// find & cfind
		      iterator  find(const key_type& key);
		const_iterator  find(const key_type& key) const; // REQUIRES unsafe_mode()
		const_iterator cfind(const key_type& key);


		// for_each custom API
		// calls f on each internal unordered_map
		template<typename Func>                      void for_each_map(Func&& f);
		template<typename Func>                      void for_each_map(Func&& f) const; // REQUIRES unsafe_mode()
		// calls f on each element (of type value_type)
		template<typename Func>	                     void for_each(Func&& f);
		template<typename Func>	                     void for_each(Func&& f) const; // REQUIRES unsafe_mode()
		// assumes threadpool.run(func) will run func in threads, and blocks till all is done
		template<typename Func, typename ThreadPool> void for_each(ThreadPool& threadpool, Func&& f);
		template<typename Func, typename ThreadPool> void for_each(ThreadPool& threadpool, Func&& f) const; // REQUIRES unsafe_mode()





	private:
		void _require_unsafe() const;
		
		std::size_t _minbuckets(std::size_t n) const;
		void _unsafe_initialize(const unordered_map& map); // NOT THREAD SAFE
		
		std::size_t _majorbucket(const key_type& key) const;
		unique_lock _getlock(std::size_t i);
		unique_lock _getlock(std::size_t i) const;                            // REQUIRES unsafe_mode()
		unique_lock _getlock(std::pair<mutex,unordered_map>& b);
		unique_lock _getlock(const std::pair<mutex,unordered_map>& b) const;  // REQUIRES unsafe_mode()

		template<typename It> void _locked_iter_first_value(_locked_iterator<It>& it);
		template<typename It> void _locked_iter_increase   (_locked_iterator<It>& it);
		
		bool      _unsafe = false; // when completely locked, switch to thread unsafe non-locking iterator
		hasher    _hf;
		key_equal _eql;
		std::array<std::pair<mutex, unordered_map>,Buckets> _maps;
	};














	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(std::size_t n, const hasher& hf, const key_equal& eql, const allocator_type& alloc)
		: _hf(hf), _eql(eql)
	{
		_unsafe_initialize( unordered_map(_minbuckets(n), hf, eql, alloc) );
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(const allocator_type& alloc)
	{
		_unsafe_initialize( unordered_map(alloc) );
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(std::size_t n, const allocator_type& alloc)
	{
		_unsafe_initialize( unordered_map(_minbuckets(n), alloc) );
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(std::size_t n, const hasher& hf, const allocator_type& alloc)
		: _hf(hf)
	{
		_unsafe_initialize( unordered_map(_minbuckets(n), hf, alloc) );
	}


	/* range constructors */
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<class InputIterator>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(InputIterator first, InputIterator last, std::size_t n, const hasher& hf, const key_equal& eql, const allocator_type& alloc)
		: _hf(hf), _eql(eql)
	{
		_unsafe_initialize( unordered_map(_minbuckets(n), hf, eql, alloc) );
		insert(first, last);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<class InputIterator>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(InputIterator first, InputIterator last, std::size_t n, const allocator_type& alloc)
	{
		_unsafe_initialize( unordered_map(_minbuckets(n), alloc) );
		insert(first, last);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<class InputIterator>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(InputIterator first, InputIterator last, std::size_t n, const hasher& hf, const allocator_type& alloc)
		: _hf(hf)
	{
		_unsafe_initialize( unordered_map(_minbuckets(n), hf, alloc) );
		insert(first, last);
	}

	
	/* copy & move constructors */
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(const concurrent_unordered_map& m)
		: _hf(m._hf), _eql(m._eql), _maps(m._maps)
	{
		m._require_unsafe();
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(concurrent_unordered_map& m)
		: _hf(m._hf), _eql(m._eql)
	{
		for (size_t i = 0; i < B; ++i)
		{
			unique_lock lock = m._getlock(i);
			_maps[i].second = m._maps[i].second;
		}
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(const locked_const_reference& m)
		: _hf(m.get()._hf), _eql(m.get()._eql), _maps(m.get()._maps)
	{
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(concurrent_unordered_map&& m)
		: _hf(std::move(m._hf)), _eql(std::move(m._eql))
	{
		for (size_t i = 0; i < B; ++i)
		{
			unique_lock lock = m._getlock(i);
			_maps[i].second = std::move(m._maps[i].second);
		}
	}


	/* initializer list constructors */
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(std::initializer_list<value_type> il, std::size_t n, const hasher& hf, const key_equal& eql, const allocator_type& alloc)
		: _hf(hf), _eql(eql)
	{
		if (n == 0)
			n = il.size();
		_unsafe_initialize( unordered_map(_minbuckets(n), hf, eql, alloc) );
		insert(il);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(std::initializer_list<value_type> il, std::size_t n, const allocator_type& alloc)
	{
		_unsafe_initialize( unordered_map(_minbuckets(n), alloc) );
		insert(il);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	concurrent_unordered_map<K,T,C,B,PF>::concurrent_unordered_map(std::initializer_list<value_type> il, std::size_t n, const hasher& hf, const allocator_type& alloc)
		: _hf(hf)
	{
		_unsafe_initialize( unordered_map(_minbuckets(n), hf, alloc) );
		insert(il);
	}


	/* assign function */
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::assign(concurrent_unordered_map& m)
	{
		if (&m == this)
			return;
		_hf = m._hf;
		_eql = m._eql;
		for (size_t i = 0; i < B; ++i)
		{
			unique_lock lock1 = _getlock(i), lock2 = m._getlock(i);
			_maps[i].second = m._maps[i].second;
		}
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::assign(concurrent_unordered_map&& m)
	{
		if (&m == this)
			return;
		_hf = std::move(m._hf);
		_eql = std::move(m._eql);
		for (size_t i = 0; i < B; ++i)
		{
			unique_lock lock1 = _getlock(i), lock2 = m._getlock(i);
			_maps[i].second = std::move(m._maps[i].second);
		}
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::unsafe_assign(const concurrent_unordered_map& m) // NOT THREAD SAFE
	{
		if (&m == this)
			return;
		_hf = m._hf;
		_eql = m._eql;
		for (size_t i = 0; i < B; ++i)
		{
			unique_lock lock1 = _getlock(i);
			_maps[i].second = m._maps[i].second;
		}
	}
	
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline void concurrent_unordered_map<K,T,C,B,PF>::assign(const concurrent_unordered_map& m) // REQUIRES m.unsafe_mode()
	{
		m._require_unsafe();
		unsafe_assign(m);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline void concurrent_unordered_map<K,T,C,B,PF>::assign(const locked_const_reference& ref)
	{
		unsafe_assign(ref);
	}


	/* copy & move operators */
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline concurrent_unordered_map<K,T,C,B,PF>& concurrent_unordered_map<K,T,C,B,PF>::operator=(concurrent_unordered_map& m)
	{
		assign(m);
		return *this;
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline concurrent_unordered_map<K,T,C,B,PF>& concurrent_unordered_map<K,T,C,B,PF>::operator=(const concurrent_unordered_map& m) // REQUIRES m.unsafe_mode()
	{
		assign(m);
		return *this;
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline concurrent_unordered_map<K,T,C,B,PF>& concurrent_unordered_map<K,T,C,B,PF>::operator=(const locked_const_reference& m)
	{
		assign(m);
		return *this;
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline concurrent_unordered_map<K,T,C,B,PF>& concurrent_unordered_map<K,T,C,B,PF>::operator=(concurrent_unordered_map&& m)
	{
		assign(std::move(m));
		return *this;
	}


	/* container properties */
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	bool concurrent_unordered_map<K,T,C,B,PF>::empty()
	{
		unique_lock lock;
		for (auto& b : _maps)
		{
			lock = _getlock(b);
			if (!b.second.empty())
				return false;
		}
		return true;
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	bool concurrent_unordered_map<K,T,C,B,PF>::empty() const
	{
		_require_unsafe();
		for (auto& b : _maps)
		{
			if (!b.second.empty())
				return false;
		}
		return true;
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	std::size_t concurrent_unordered_map<K,T,C,B,PF>::size()
	{
		unique_lock lock;
		std::size_t s = 0;
		for (auto& b : _maps)
		{
			lock = _getlock(b);
			s += b.second.size();
		}
		return s;
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	std::size_t concurrent_unordered_map<K,T,C,B,PF>::size() const
	{
		_require_unsafe();
		std::size_t s = 0;
		for (auto& b : _maps)
			s += b.second.size();
		return s;
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	std::size_t concurrent_unordered_map<K,T,C,B,PF>::max_size()
	{
		unique_lock lock = _getlock(0);
		return _maps[0].second.max_size();
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	std::size_t concurrent_unordered_map<K,T,C,B,PF>::max_size() const
	{
		_require_unsafe();
		return _maps[0].second.max_size();
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::clear()
	{
		for_each_map( [](unordered_map& m) { m.clear(); } );
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::max_load_factor(float z)
	{
		for_each_map( [z](unordered_map& m) { m.max_load_factor(z); } );
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::rehash(std::size_t n)
	{
		n = _minbuckets(n);
		for_each_map( [n](unordered_map& m) { m.rehash(n); } );
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::reserve(std::size_t n)
	{
		n = _minbuckets(n);
		for_each_map( [n](unordered_map& m) { m.reserve(n); } );
	}


	/* element operations */
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	std::size_t concurrent_unordered_map<K,T,C,B,PF>::count(const key_type& key)
	{
		const std::size_t i = _majorbucket(key);
		assert(i < B);
		unique_lock lock = _getlock(i);
		return _maps[i].second.count(key);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	std::size_t concurrent_unordered_map<K,T,C,B,PF>::count(const key_type& key) const // REQUIRES unsafe_mode()
	{
		_require_unsafe();
		const std::size_t i = _majorbucket(key);
		assert(i < B);
		return _maps[i].second.count(key);
	}

	// operator[](key): note that this returns a locked_mapped_pointer
	// which acts as a pointer to mapped_type instead of C++ standardized reference to mapped_type
	// this is necessary because it is a smart pointer that maintains the required lock
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	typename concurrent_unordered_map<K,T,C,B,PF>::locked_mapped_pointer concurrent_unordered_map<K,T,C,B,PF>::operator[](const key_type& key)
	{
		const std::size_t i = _majorbucket(key);
		assert(i < B);
		unique_lock lock = _getlock(i);
		return locked_mapped_pointer(std::move(lock), & _maps[i].second[key]);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	typename concurrent_unordered_map<K,T,C,B,PF>::locked_mapped_pointer concurrent_unordered_map<K,T,C,B,PF>::at(const key_type& key)
	{
		const std::size_t i = _majorbucket(key);
		assert(i < B);
		unique_lock lock = _getlock(i);
		return locked_mapped_pointer(std::move(lock), & _maps[i].second.at(key));
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	typename concurrent_unordered_map<K,T,C,B,PF>::locked_const_mapped_pointer concurrent_unordered_map<K,T,C,B,PF>::at(const key_type& key) const
	{
		_require_unsafe();
		const std::size_t i = _majorbucket(key);
		assert(i < B);
		return locked_mapped_pointer(unique_lock(), & _maps[i].second.at(key));
	}


	// lock & unlock of entire concurrent_unordered_map
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::lock()
	{
		for (auto& b : _maps)
			b.first.lock();
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::unlock()
	{
		for (auto& b : _maps)
			b.first.unlock();
	}

	// iterator and const_iterator by default use internal locking
	// both require non-const concurrent_unordered_map
	// however: when this->_unsafe == true (by calling this->lock())
	// then do not use internal locking, and also allow const_iterator from const concurrent_unordered_map
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename It>
	class concurrent_unordered_map<K,T,C,B,PF>::_locked_iterator: public std::iterator<std::forward_iterator_tag,typename It::value_type,difference_type>
	{
	public:
		_locked_iterator(): _m(nullptr), _i(B) {}
		template<typename It2>
		explicit _locked_iterator(concurrent_unordered_map& m, std::size_t i, unique_lock&& lock, It2&& it)
			: _m(&m), _i(i), _lock(std::move(lock)), _it(std::forward<It2>(it))
		{
			// ensure we're actually pointing to a real value, or end()
			_m->_locked_iter_first_value(*this);
		}
			
		_locked_iterator(_locked_iterator&&) = default;
		_locked_iterator& operator=(_locked_iterator&&) = default;

		template<typename It2>
		_locked_iterator(_locked_iterator<It2>&& it)
			: _m(it._m), _i(it._i), _lock(std::move(it._lock)), _it(std::move(it._it))
		{
			it.i = B;
		}
		template<typename It2>
		_locked_iterator& operator=(_locked_iterator<It2>&& it)
		{
			_m = it._m;
			_i = it._i;
			_lock = std::move(it._lock);
			_it = std::move(it._it);
			it._i = B;
			return *this;
		}
			
		typename It::reference operator*() const { return *_it; }
		typename It::pointer operator->() const { return &(*_it); }

		template<typename It2>
		bool operator==(const It2& r) const { return _i == r._i && (_i == B || _it == r._it); }
		template<typename It2>
		bool operator!=(const It2& r) const { return !(*this == r); }

		_locked_iterator& operator++() { _m->_locked_iter_increase(*this); return *this; }

	private:
		friend concurrent_unordered_map;
		concurrent_unordered_map* _m;
		std::size_t _i;
		unique_lock _lock;
		It _it;
	};


	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::iterator concurrent_unordered_map<K,T,C,B,PF>::begin()
	{
		unique_lock lock = _getlock(0);
		return iterator(*this, 0, std::move(lock), _maps[0].second.begin());
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::iterator concurrent_unordered_map<K,T,C,B,PF>::end()
	{
		return iterator();
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::const_iterator concurrent_unordered_map<K,T,C,B,PF>::cbegin()
	{
		unique_lock lock = _getlock(0);
		return const_iterator(*this, 0, std::move(lock), _maps[0].second.cbegin());
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::const_iterator concurrent_unordered_map<K,T,C,B,PF>::begin() const // REQUIRES unsafe_mode()
	{
		_require_unsafe();
		return const_iterator(*const_cast<concurrent_unordered_map*>(this), 0, unique_lock(), _maps[0].second.cbegin());
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::const_iterator concurrent_unordered_map<K,T,C,B,PF>::end() const
	{
		return const_iterator();
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::const_iterator concurrent_unordered_map<K,T,C,B,PF>::cbegin() const // REQUIRES unsafe_mode()
	{
		_require_unsafe();
		return const_iterator(*const_cast<concurrent_unordered_map*>(this), 0, unique_lock(), _maps[0].second.cbegin());
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::const_iterator concurrent_unordered_map<K,T,C,B,PF>::cend() const
	{
		return const_iterator();
	}


	// insert & emplace : return bool instead of pair<iterator,bool> to reduce lifetime of lock
	// call insert_std or emplace_std to obtain pair<iterator,bool>
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	bool concurrent_unordered_map<K,T,C,B,PF>::insert(const typename concurrent_unordered_map<K,T,C,B,PF>::value_type& val)
	{
		const std::size_t i = _majorbucket(val.first);
		assert(i < B);
		unique_lock lock = _getlock(i);
		return _maps[i].second.insert(val).second;
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename P>
	bool concurrent_unordered_map<K,T,C,B,PF>::insert(P&& val)
	{
		const std::size_t i = _majorbucket(val.first);
		assert(i < B);
		unique_lock lock = _getlock(i);
		return _maps[i].second.insert(std::forward<P>(val)).second;
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename InputIterator>
	inline void concurrent_unordered_map<K,T,C,B,PF>::insert(InputIterator first, InputIterator last)
	{
		for (; first != last; ++first)
			insert(*first);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline void concurrent_unordered_map<K,T,C,B,PF>::insert(std::initializer_list<typename concurrent_unordered_map<K,T,C,B,PF>::value_type> il)
	{
		insert(il.begin(), il.end());
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename... Args>
	inline bool concurrent_unordered_map<K,T,C,B,PF>::emplace(Args&&... args)
	{
		return insert(value_type(std::forward<Args>(args)...));
	}

	// insert_std & emplace_std : standard conforming insert & emplace
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	std::pair<typename concurrent_unordered_map<K,T,C,B,PF>::iterator,bool> concurrent_unordered_map<K,T,C,B,PF>::insert_std(const value_type& val)
	{
		const std::size_t i = _majorbucket(val.first);
		assert(i < B);
		unique_lock lock = _getlock(i);
		auto ret = _maps[i].second.insert(val);
		return std::pair<iterator,bool>(iterator(*this, i, std::move(lock), std::move(ret.first)), ret.second);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename P>
	std::pair<typename concurrent_unordered_map<K,T,C,B,PF>::iterator,bool> concurrent_unordered_map<K,T,C,B,PF>::insert_std(P&& val)
	{
		const std::size_t i = _majorbucket(val.first);
		assert(i < B);
		unique_lock lock = _getlock(i);
		auto ret = _maps[i].second.insert(std::forward<P>(val));
		return std::pair<iterator,bool>(iterator(*this, i, std::move(lock), std::move(ret.first)), ret.second);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename... Args>
	inline std::pair<typename concurrent_unordered_map<K,T,C,B,PF>::iterator,bool> concurrent_unordered_map<K,T,C,B,PF>::emplace_std(Args&&... args)
	{
		return insert_std(value_type(std::forward<Args>(args)...));
	}

	// NOTE: erase takes reference to iterator, because locked iterators cannot be copied
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::erase(iterator& it)
	{
		if (it._i == B)
			return;
		it._it = _maps[it._i].second.erase(it._it);
		_locked_iter_first_value(it);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::erase(const_iterator& it)
	{
		if (it._i == B)
			return;
		it._it = _maps[it._i].second.erase(it._it);
		_locked_iter_first_value(it);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	std::size_t concurrent_unordered_map<K,T,C,B,PF>::erase(const key_type& key)
	{
		const std::size_t i = _majorbucket(key);
		assert(i < B);
		unique_lock lock = _getlock(i);
		return _maps[i].second.erase(key);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	typename concurrent_unordered_map<K,T,C,B,PF>::iterator concurrent_unordered_map<K,T,C,B,PF>::find(const key_type& key)
	{
		const std::size_t i = _majorbucket(key);
		assert(i < B);
		unique_lock lock = _getlock(i);
		base_iterator it = _maps[i].second.find(key);
		if (it == _maps[i].second.end())
			return end();
		return iterator(*this, i, std::move(lock), std::move(it));
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	typename concurrent_unordered_map<K,T,C,B,PF>::const_iterator concurrent_unordered_map<K,T,C,B,PF>::find(const key_type& key) const
	{
		_require_unsafe();
		const std::size_t i = _majorbucket(key);
		assert(i < B);
		base_const_iterator it = _maps[i].second.find(key);
		if (it == _maps[i].second.end())
			return end();
		return const_iterator(*this, i, unique_lock(), std::move(it));
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	typename concurrent_unordered_map<K,T,C,B,PF>::const_iterator concurrent_unordered_map<K,T,C,B,PF>::cfind(const key_type& key)
	{
		const std::size_t i = _majorbucket(key);
		assert(i < B);
		unique_lock lock = _getlock(i);
		base_const_iterator it = _maps[i].second.find(key);
		if (it == _maps[i].second.end())
			return end();
		return const_iterator(*this, i, std::move(lock), std::move(it));
	}


	/* for_each custom API */
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename Func>
	void concurrent_unordered_map<K,T,C,B,PF>::for_each_map(Func&& f)
	{
		for (auto& b : _maps)
		{
			unique_lock lock = _getlock(b);
			f(b.second);
		}
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename Func>
	void concurrent_unordered_map<K,T,C,B,PF>::for_each_map(Func&& f) const
	{
		_require_unsafe();
		for (auto& b : _maps)
			f(b.second);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename Func>
	void concurrent_unordered_map<K,T,C,B,PF>::for_each(Func&& f)
	{
		for (auto& b: _maps)
		{
			unique_lock lock = _getlock(b);
			for (auto& e : b.second)
				f(e);
		}
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename Func>
	void concurrent_unordered_map<K,T,C,B,PF>::for_each(Func&& f) const
	{
		_require_unsafe();
		for (auto& b: _maps)
		{
			for (auto& e : b.second)
				f(e);
		}
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename Func,typename ThreadPool>
	void concurrent_unordered_map<K,T,C,B,PF>::for_each(ThreadPool& threadpool, Func&& f)
	{
		std::atomic_size_t ai(0);
		threadpool.run([this,&ai,f]()
			{
				while (true)
				{
					std::size_t i = ai++;
					if (i >= B)
						return;
					unique_lock lock = _getlock(i);
					for (auto& e : _maps[i].second)
						f(e);
				}
			});
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename Func,typename ThreadPool>
	void concurrent_unordered_map<K,T,C,B,PF>::for_each(ThreadPool& threadpool, Func&& f) const
	{
		_require_unsafe();
		std::atomic_size_t ai(0);
		threadpool.run([this,&ai,f]()
			{
				while (true)
				{
					std::size_t i = ai++;
					if (i >= B)
						return;
					for (auto& e : _maps[i].second)
						f(e);
				}
			});
	}



	/* private functions */
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline void concurrent_unordered_map<K,T,C,B,PF>::_require_unsafe() const
	{
		if (_unsafe == false)
			throw std::runtime_error("const concurrent_unordered_map::_require_unsafe(): unsafe mode required: cannot lock in const member function");
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename It>
	void concurrent_unordered_map<K,T,C,B,PF>::_locked_iter_first_value(_locked_iterator<It>& it)
	{
		// already done if at end
		if (it._i == B)
			return;
		// find next real element
		while (it._it == _maps[it._i].second.end())
		{
			// release lock if necessary
			if (it._lock.owns_lock())
				it._lock.unlock();
			if (++it._i == B)
				return;
			// acquire new lock if concurrent_unordered_map itself is not locked
			it._lock = _getlock(it._i);
			it._it = _maps[it._i].second.begin();
		}
	}
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	template<typename It>
	void concurrent_unordered_map<K,T,C,B,PF>::_locked_iter_increase(_locked_iterator<It>& it)
	{
		if (it._i < B)
			++it._it;
		_locked_iter_first_value(it);
	}

	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	std::size_t concurrent_unordered_map<K,T,C,B,PF>::_minbuckets(std::size_t n) const
	{
		return (n/B) + std::sqrt(n);
	}
	// NOT THREAD SAFE
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	void concurrent_unordered_map<K,T,C,B,PF>::_unsafe_initialize(const unordered_map& map)
	{
		assert(map.empty());
		for (auto& m : _maps)
			m.second = map;
	}
		
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline std::size_t concurrent_unordered_map<K,T,C,B,PF>::_majorbucket(const key_type& key) const
	{
		return (_hf(key) * PF) % B;
	}
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::unique_lock concurrent_unordered_map<K,T,C,B,PF>::_getlock(std::size_t i)
	{
		assert(i < B);
		return _unsafe ? unique_lock() : unique_lock(_maps[i].first);
	}
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::unique_lock concurrent_unordered_map<K,T,C,B,PF>::_getlock(std::size_t i) const
	{
		_require_unsafe();
		return unique_lock();
	}
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::unique_lock concurrent_unordered_map<K,T,C,B,PF>::_getlock(std::pair<mutex,unordered_map>& b)
	{
		return _unsafe ? unique_lock() : unique_lock(b.first);
	}
	template<typename K, typename T, typename C, std::size_t B, std::size_t PF>
	inline typename concurrent_unordered_map<K,T,C,B,PF>::unique_lock concurrent_unordered_map<K,T,C,B,PF>::_getlock(const std::pair<mutex,unordered_map>& b) const
	{
		_require_unsafe();
		return unique_lock();
	}

} // namespace concurrent_unordered_map

#endif // CONCURRENT_UNORDERED_MAP_HPP
