#include "tools.h"
#include <chrono>
#include <thread>
#include <algorithm>


uint32_t memhash(void const* mem, int length)
{
	uint8_t const* ptr = (uint8_t const*)mem;
	uint32_t const* ptrDW = (uint32_t const*)ptr;

	int dw = length / sizeof(uint32_t);
	uint32_t hash = 0;
	for (int k = dw - 1; k >= 0; --k) {
		hash = 31 * hash + ptrDW[k];
	}

	dw *= sizeof(uint32_t);
	for (int k = length - 1; k >= dw; --k) {
		hash = 31 * hash + (uint32_t)ptr[k];
	}
	return hash;
}


int bytecopy(Bytes dest, CBytes src)
{
	int copied = (int)std::min(dest.size(), src.size());
	memcpy(dest.begin, src.begin, copied);
	return copied;
}


uint64_t getTimeMs()
{
	auto t = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
}

void usleep(size_t time)
{
	std::this_thread::sleep_for(std::chrono::microseconds(time));
}

void sleep(size_t time)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(time));
}