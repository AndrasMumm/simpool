#ifndef _DYNAMICSIZEPOOL_HPP
#define _DYNAMICSIZEPOOL_HPP

//If defined there will be checks to ensure that the hashmaps entries are legit and a block is not allocated two times.
#define SIMPOOL_CHECK_FOR_BUGS 0

#include <algorithm>
#include <cstddef>
#include <cassert>
#include <iostream>
#include <map>
#include <mutex>

#include "StdAllocator.hpp"
#include "FixedSizePool.hpp"

template <class MA, class IA = StdAllocator>
class DynamicSizePool
{
protected:
	struct Block
	{
		char* data;
		std::size_t size;
		bool isHead;
		Block* next;
		Block* prev;
	};

	// Allocator for the underlying data
	typedef FixedSizePool<struct Block, IA, IA, (1 << 6)> BlockPool;
	BlockPool blockPool;

	// Start of the nodes of used and free block lists
	struct Block* usedBlocks;
	struct Block* freeBlocks;

	// Total size allocated (bytes)
	std::size_t totalBytes;

	// Allocated size (bytes)
	std::size_t allocBytes;

	// Minimum size for allocations
	std::size_t minBytes;

	// Search the list of free blocks and return a usable one if that exists, else NULL
	void findUsableBlock(struct Block*& best, struct Block*& prev, std::size_t size) {
		best = prev = NULL;
		for (struct Block* iter = freeBlocks, *iterPrev = NULL; iter; iter = iter->next) {
			if (iter->size >= size && (!best || iter->size < best->size)) {
				best = iter;
				prev = iterPrev;
			}
			iterPrev = iter;
		}
	}
#undef max
#undef min

	// Allocate a new block and add it to the list of free blocks
	void allocateBlock(struct Block*& curr, struct Block*& prev, const std::size_t size) {
		const std::size_t sizeToAlloc = std::max(size, minBytes);
		curr = prev = NULL;

		// Allocate data
		void* data = MA::allocate(sizeToAlloc);
		totalBytes += sizeToAlloc;
		assert(data);

		// Find next and prev such that next->data is still smaller than data (keep ordered)
		struct Block* next;
		for (next = freeBlocks; next && next->data < data; next = next->next) {
			prev = next;
		}

		// Allocate the block
		curr = (struct Block*)blockPool.allocate();
		if (!curr) return;
		curr->data = static_cast<char*>(data);
		curr->size = sizeToAlloc;
		curr->isHead = true;
		curr->next = next;
		if (next)
		{
			next->prev = curr;
		}

		// Insert
		if (prev)
		{
			prev->next = curr;
			curr->prev = prev;
		}
		else
		{
			freeBlocks = curr;
			freeBlocks->prev = nullptr;
		}
	}

	void splitBlock(struct Block*& curr, struct Block*& prev, const std::size_t size) {
		struct Block* next;
		if (curr->size == size) {
			// Keep it
			next = curr->next;
		}
		else {
			// Split the block, curr->size is bigger than size
			//Calculating new size
			std::size_t remaining = curr->size - size;
			//allocing new block
			struct Block* newBlock = (struct Block*)blockPool.allocate();
			if (!newBlock) return;
			//Splitting current blocks data, newBlock will be the new one while current maintains it's size
			newBlock->data = curr->data + size;
			newBlock->size = remaining;
			newBlock->isHead = false;
			//Since we will remove this block from the freelist, we basically replace it with the newBlock
			newBlock->next = curr->next;
			if (newBlock->next)
			{
				newBlock->next->prev = newBlock;
			}

			next = newBlock;
			//Updating current blocks size
			curr->size = size;
		}

		if (prev)
		{
			//Updating prev references
			prev->next = next;
			if (next)
			{
				next->prev = prev;
			}
		}
		else
		{
			freeBlocks = next;
			if (freeBlocks)
			{
				freeBlocks->prev = nullptr;
			}
		}
	}

	void releaseBlock(struct Block* curr, struct Block* prev) {
		assert(curr != NULL);

		if (prev)
		{
			//Cutting block out of linked list chain.
			prev->next = curr->next;
			if (prev->next)
			{
				prev->next->prev = prev;
			}
		}
		else
		{
			usedBlocks = curr->next;
			if (usedBlocks)
			{
				usedBlocks->prev = nullptr;
			}
		}

		// Find location to put this block in the freeBlocks list
		prev = NULL;
		for (struct Block* temp = freeBlocks; temp && temp->data < curr->data; temp = temp->next) {
			prev = temp;
		}

		// Keep track of the successor
		struct Block* next = prev ? prev->next : freeBlocks;

		// Check if prev and curr can be merged
		if (prev && prev->data + prev->size == curr->data && !curr->isHead) {
			//We can merge both of the blocks.
			prev->size = prev->size + curr->size;
			blockPool.deallocate(curr); // keeps data
			prev->next = next;
			if(prev->next)
			{
				prev->next->prev = prev;
			}
			curr = prev;
		}
		else if (prev) {
			prev->next = curr;
			curr->prev = prev;
		}
		else {
			freeBlocks = curr;
			if (freeBlocks)
			{
				freeBlocks->prev = nullptr;
			}
		}

		// Check if curr and next can be merged
		if (next && curr->data + curr->size == next->data && !next->isHead) {
			curr->size = curr->size + next->size;
			curr->next = next->next;
			if (curr->next)
			{
				curr->next->prev = curr;
			}
			blockPool.deallocate(next); // keep data
		}
		else {
			curr->next = next;
			if (curr->next)
			{
				curr->next->prev = curr;
			}
		}
	}

	void freeAllBlocks() {
		// Release the used blocks
		while (usedBlocks) {
			releaseBlock(usedBlocks, NULL);
		}

		// Release the unused blocks
		while (freeBlocks) {
			assert(freeBlocks->isHead);
			MA::deallocate(freeBlocks->data);
			totalBytes -= freeBlocks->size;
			struct Block* curr = freeBlocks;
			freeBlocks = freeBlocks->next;
			blockPool.deallocate(curr);
		}
	}


	static std::mutex& getMutex()
	{
		static std::mutex m;
		return m;
	}
	std::map<void*, Block*> allocatedAddressToBlock;

public:
	static DynamicSizePool* getInstance() {
		static DynamicSizePool instance;
		return &instance;
	}

	DynamicSizePool(const std::size_t _minBytes = (1 << 8))
		: blockPool(),
		usedBlocks(NULL),
		freeBlocks(NULL),
		totalBytes(0),
		allocBytes(0),
		minBytes(_minBytes) { }

	~DynamicSizePool() { freeAllBlocks(); }

	void* allocate(std::size_t size) {
		if (size == 0)
		{
			size = 1;
		}
		getMutex().lock();
		struct Block* best, * prev;
		findUsableBlock(best, prev, size);

		// Allocate a block if needed
		if (!best) allocateBlock(best, prev, size);
		assert(best);

		// Split the free block
		splitBlock(best, prev, size);

		// Push node to the list of used nodes
		best->next = usedBlocks;
		if (usedBlocks)
		{
			usedBlocks->prev = best;
		}
		usedBlocks = best;
		usedBlocks->prev = nullptr;

		// Increment the allocated size
		allocBytes += size;

#ifdef SIMPOOL_CHECK_FOR_BUGS
		auto it = allocatedAddressToBlock.find(usedBlocks);
		if (it != allocatedAddressToBlock.end())
		{
			std::cout << "Block double allocation detected" << std::endl;
		}

		if (usedBlocks->size != size)
		{
			std::cout << "Size diff" << std::endl;
		}
#endif

		allocatedAddressToBlock.insert({usedBlocks->data, usedBlocks});
		//Resetting the memory we return
		memset(usedBlocks->data, 0, usedBlocks->size);
		getMutex().unlock();
		// Return the new pointer
		return usedBlocks->data;
	}

	bool deallocate(void* ptr) {
		assert(ptr);

		getMutex().lock();

		// Find the associated block
		auto it = allocatedAddressToBlock.find(ptr);
		if (it == allocatedAddressToBlock.end())
		{
			getMutex().unlock();
			return false;
		}
		//Getting block from iterator
		struct Block* curr = it->second;

#ifdef SIMPOOL_CHECK_FOR_BUGS
		//Sanity checks for debugging
		size_t index_ = 0;
		struct Block* curr_ = usedBlocks, * prev_ = NULL;
		for (; curr_ && curr_->data != ptr; curr_ = curr_->next)
		{
			prev_ = curr_;
			index_++;
		}

		if (ptr != (void*)curr->data)
		{
		printf("Ptr %p != curr->data %p", ptr, curr->data);;
		}
		if (ptr != (void*)curr_->data)
		{
			printf("Ptr %p != curr_->data %p", ptr, curr_->data);
		}
		if (curr != curr_)
		{
			size_t index = 0;
			struct Block* tmp = usedBlocks;
			for (; tmp && tmp != curr; tmp = tmp->next)
			{
				index_++;
			}
			printf("curr %p at index %d != curr_ %p at index %d", curr, index, curr_, index_);
		}
		if (curr->prev != prev_)
		{
			printf("curr->prev %p != prev_ %p", curr->prev, prev_);
		}
#endif

		allocatedAddressToBlock.erase(it);

		// Remove from allocBytes
		allocBytes -= curr->size;

		// Release it
		releaseBlock(curr, curr->prev);
		getMutex().unlock();
		return true;
	}

	std::size_t allocatedSize()
	{
		return allocBytes;
	}

	std::size_t managedSize()
	{
		return totalBytes;
	}

	std::size_t totalSize() {
		getMutex().lock();
		auto ret = totalBytes + blockPool.totalSize();
		getMutex().unlock();
		return ret;
	}

	std::size_t numFreeBlocks() {
		getMutex().lock();
		std::size_t nb = 0;
		for (struct Block* temp = freeBlocks; temp; temp = temp->next) nb++;
		getMutex().unlock();
		return nb;
	}

	std::size_t numUsedBlocks() {
		mutex.lock();
		std::size_t nb = 0;
		for (struct Block* temp = usedBlocks; temp; temp = temp->next) nb++;
		mutex.unlock();
		return nb;
	}

};

#endif
