#pragma once

#include <string>
#include <vector>
#include <assert.h>

#define ASSERT assert
#define ASSERT_MSG(COND, ...) assert(COND)


template <class T>
struct Array {
	T* begin;
	T* end;

public:
	template <size_t N>
	Array(T(&vals)[N]) : begin(vals), end(vals + N) {}
	Array(T* begin, T* end) : begin(begin), end(end) {}
	Array() : begin(nullptr), end(nullptr) {}

	T& operator[](size_t idx) { return begin[idx]; }
	T const& operator[](size_t idx) const { return begin[idx]; }

	size_t count() const { return end - begin; }
	size_t size() const { return sizeof(T) * (end - begin); }

	bool empty() const { return begin == end; }
};

using Bytes = Array<uint8_t>;
using CBytes = Array<uint8_t const>;

template <class T, size_t N>
Array<T> toArray(T(&vals)[N]) { return Array<T>(vals, vals + N); }

template <class T>
Bytes toBytes(Array<T> const& array) { return Bytes((uint8_t*)array.begin, (uint8_t*)array.end); }

template <class T>
CBytes toBytes(Array<T const> const& array) { return Bytes((uint8_t const*)array.begin, (uint8_t const*)array.end); }

int bytecopy(Bytes dest, CBytes src);


struct String : CBytes {

};

inline std::string toString(CBytes bytes) { return std::string(bytes.begin, bytes.end); }


struct Buffer : Bytes {
	Buffer(size_t size) noexcept : Bytes((uint8_t*)malloc(size), nullptr) { end = begin + size; }
	~Buffer() { free(begin); }
};


struct PoolHandle {
	uint32_t index = 0;
	uint32_t nonce = 0;

public:
	PoolHandle(void) = default;
	PoolHandle(uint32_t index, uint32_t nonce) : index(index), nonce(nonce) {}

	PoolHandle(PoolHandle&& other) noexcept : index(other.index), nonce(other.nonce) { other.index = 0, other.nonce = 0; }
	PoolHandle(PoolHandle const& other) = default;

	PoolHandle& operator=(PoolHandle const&) = default;
	PoolHandle& operator=(PoolHandle&&) = default;

	bool operator==(PoolHandle const& rhs) const { return index == rhs.index && nonce == rhs.nonce; }
	bool operator!=(PoolHandle const& rhs) const { return index != rhs.index || nonce != rhs.nonce; }

	bool isValid(void) const { return nonce != 0; }
};

template <class T>
struct PoolCell {
	uint32_t nonce = 0;
	char data[sizeof(T)];

public:
	PoolCell() = default;
	~PoolCell();

	PoolCell(PoolCell&&) noexcept;
	PoolCell(PoolCell const&) noexcept;

	template <class... ArgsTy>
	void make(uint32_t nonce, ArgsTy&&... args);
	void destroy();

	bool isValid() const { return nonce != 0; }
	T* get() const;
};

template <class T>
struct PoolValue {
	PoolHandle handle;
	T* value;

public:
	operator bool() const { return handle.nonce != 0; }
	bool operator!() const { return handle.nonce == 0; }

	T* operator->() const { ASSERT(handle.nonce != 0); return value; }
	T& operator*() const { ASSERT(handle.nonce != 0); return *value; }
};

template <class T, class CellTy>
class PoolIterator {
public:
	PoolIterator(CellTy* begin, uint32_t index) : m_cell(begin), m_index(index) {}

	PoolIterator& operator++() { ++m_cell; ++m_index; return *this; }

	PoolValue<const T> operator*() const { return PoolValue<const T>{ handle(), m_cell->get() }; }
	PoolValue<T>       operator*() { return PoolValue<T>{ handle(), m_cell->get() }; }

	bool operator==(PoolIterator const& other) const { return m_cell != other.m_cell; }
	bool operator!=(PoolIterator const& other) const { return m_cell != other.m_cell; }

	T&         value()  const { ASSERT(m_cell->isValid()); return *m_cell->get(); }
	PoolHandle handle() const { return PoolHandle{ m_index, m_cell->nonce }; }

private:
	CellTy* m_cell;
	uint32_t m_index;
};

template <class T>
class Pool {
	struct Cell : public PoolCell<T> {
		uint32_t next;
	public:
		Cell(uint32_t next) : next(next) {}

		template <class... ArgsTy>
		void make(uint32_t nonce, ArgsTy&&... args);
		void destroy();
	};

public:
	using Iterator = PoolIterator<T, Cell>;

public:
	Pool() = default;

	Pool(Pool&&) noexcept;
	Pool(Pool const&) = delete;
	Pool& operator=(Pool&&) noexcept;
	Pool& operator=(Pool const&) = delete;

	T* operator[](PoolHandle idx) const;
	T& at(PoolHandle idx) const;

	void reserve(size_t n) { m_storage.reserve(n); }
	void clear() { m_head = UINT_MAX; m_storage.clear(); }

	size_t count() const { return m_count; }

	Iterator begin() const { return Iterator(m_storage.data(), 0); }
	Iterator end() const { return Iterator(m_storage.data() + m_storage.size(), -1); }

	template <class... ArgsTy>
	PoolHandle alloc(ArgsTy&&... args);
	void dealloc(PoolHandle handle);

	bool check();

private:
	mutable std::vector<Cell> m_storage;
	uint32_t m_head = UINT_MAX;
	uint32_t m_autoinc = 1;
	size_t m_count = 0;

private:
	Cell* getCell(PoolHandle idx) const;
};


template <class T>
class PoolMirror {
public:
	using Iterator = PoolIterator<T, PoolCell<T>>;

public:
	PoolMirror(void) = default;

	PoolMirror(PoolMirror&&) = default;
	PoolMirror(PoolMirror const&) = delete;
	PoolMirror& operator=(PoolMirror&&) = default;
	PoolMirror& operator=(PoolMirror const&) = delete;

	T* operator[](PoolHandle idx) const;
	T& at(PoolHandle idx) const;

	void reserve(size_t n) { m_storage.reserve(n); }
	void clear() { m_storage.clear(); }

	Iterator begin() const { return Iterator(m_storage.data(), 0); }
	Iterator end() const { return Iterator(m_storage.data() + m_storage.size(), -1); }
	size_t count() const { return m_storage.size(); }

	template <class... ArgsTy>
	T& make(PoolHandle idx, ArgsTy&&... args);
	void destroy(PoolHandle idx);

private:
	mutable std::vector<PoolCell<T>> m_storage;
};

uint32_t memhash(void const* mem, int length);

uint64_t getTimeMs();
void usleep(size_t time);
void sleep(size_t time);


struct Timer {
	uint64_t start;
	size_t duration;

public:
	Timer() : start(0), duration(0) {}
	Timer(size_t duration) : start(getTimeMs()), duration(duration) {}

	bool expired() const { return getTimeMs() - start >= duration; }
	bool shedule() { if (expired()) { reset(); return true; } return false; }

	void reset() { start = getTimeMs(); }
	void activate() { start = getTimeMs() - duration; }
};


#include "pool.hpp"