#ifndef _FIXEDSIZEPOOL_HPP
#define _FIXEDSIZEPOOL_HPP

#include <cstring>
#define  _XOPEN_SOURCE_EXTENDED 1
#include <string.h>
#include <iostream>
#include <stdio.h>
#include <windows.h>

#include "StdAllocator.hpp"

template<class T, class MA, class IA = StdAllocator, int NP = (1 << 8)>
class FixedSizePool
{
protected:
	struct Pool
	{
		unsigned char* data;
		unsigned int* avail;
		unsigned int numAvail;
		struct Pool* next;
	};

	struct Pool* pool;
	const std::size_t numPerPool;
	const std::size_t totalPoolSize;

	std::size_t numBlocks;

	void newPool(struct Pool** pnew) {
		struct Pool* p = static_cast<struct Pool*>(IA::allocate(sizeof(struct Pool) + NP * sizeof(unsigned int)));
		p->numAvail = numPerPool;
		p->next = NULL;

		p->data = reinterpret_cast<unsigned char*>(MA::allocate(numPerPool * sizeof(T)));
		p->avail = reinterpret_cast<unsigned int*>(p + 1);
		for (int i = 0; i < NP; i++) p->avail[i] = -1;

		*pnew = p;
	}

	T* allocInPool(struct Pool* p) {
		if (!p->numAvail) return NULL;

		for (int i = 0; i < NP; i++) {
			DWORD tmp = 0;
			DWORD mask = *(DWORD*)&p->avail[i];
			const bool isNonzero = _BitScanForward(&tmp, mask);
			const int bit = (int)tmp;
			if (isNonzero && bit >= 0) {
				p->avail[i] ^= 1 << bit;
				--p->numAvail;
				const int entry = i * sizeof(unsigned int) * 8 + bit;
				return reinterpret_cast<T*>(p->data) + entry;
			}
		}

		return NULL;
	}

	//std::mutex mutex;
public:
	static inline FixedSizePool& getInstance() {
		static FixedSizePool instance;
		return instance;
	}

	FixedSizePool()
		: numPerPool(NP * sizeof(unsigned int) * 8),
		totalPoolSize(sizeof(struct Pool) +
			numPerPool * sizeof(T) +
			NP * sizeof(unsigned int)),
		numBlocks(0)
	{
		newPool(&pool);
	}

	~FixedSizePool() {
		for (struct Pool* curr = pool; curr; ) {
			struct Pool* next = curr->next;
			MA::deallocate(curr);
			curr = next;
		}
	}

	T* allocate() {
		//mutex.lock();
		T* ptr = NULL;
		struct Pool* prev = NULL;
		struct Pool* curr = pool;
		while (!ptr && curr) {
			ptr = allocInPool(curr);
			prev = curr;
			curr = curr->next;
		}

		if (!ptr) {
			newPool(&prev->next);
			//mutex.unlock();
			ptr = allocate();
			//mutex.lock();
			// TODO: In this case we should reverse the linked list for optimality
		}
		else {
			numBlocks++;
		}
		//mutex.unlock();
		return ptr;
	}

	void deallocate(T* ptr) {
		//mutex.lock();
		int i = 0;
		for (struct Pool* curr = pool; curr; curr = curr->next) {
			const T* start = reinterpret_cast<T*>(curr->data);
			const T* end = reinterpret_cast<T*>(curr->data) + numPerPool;
			if ((ptr >= start) && (ptr < end)) {
				// indexes bits 0 - numPerPool-1
				const int indexD = ptr - reinterpret_cast<T*>(curr->data);
				const int indexI = indexD / (sizeof(unsigned int) * 8);
				const int indexB = indexD % (sizeof(unsigned int) * 8);
#ifndef NDEBUG
				if ((curr->avail[indexI] & (1 << indexB))) {
					std::cerr << "Trying to deallocate an entry that was not marked as allocated" << std::endl;
				}
#endif
				curr->avail[indexI] ^= 1 << indexB;
				curr->numAvail++;
				numBlocks--;
				//mutex.unlock();
				return;
			}
			i++;
		}
		//mutex.unlock();
		std::cerr << "Could not find pointer to deallocate" << std::endl;
		throw(std::bad_alloc());
	}

	/// Return allocated size to user.
	std::size_t allocatedSize()
	{
		//mutex.lock();
		auto ret = numBlocks * sizeof(T);
		//mutex.unlock();
		return ret;
	}

	/// Return total size with internal overhead.
	std::size_t totalSize() {
		//mutex.lock();
		auto ret = numPools() * totalPoolSize;
		//mutex.unlock();
		return ret;
	}

	/// Return the number of pools
	std::size_t numPools() {
		//mutex.lock();
		std::size_t np = 0;
		for (struct Pool* curr = pool; curr; curr = curr->next) np++;
		//mutex.unlock();
		return np;
	}

	/// Return the pool size
	std::size_t poolSize()
	{
		//mutex.lock();
		auto ret = totalPoolSize;
		//mutex.unlock();
		return ret;
	}
};


#endif // _FIXEDSIZEPOOL_HPP
