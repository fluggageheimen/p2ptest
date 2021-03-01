#include "tools.h"


// ------------------------------------------------------------------------
// PoolCell implementation
// ------------------------------------------------------------------------
template <class T>
PoolCell<T>::~PoolCell()
{
	if (isValid()) {
		get()->~T();
	}
}

// ------------------------------------------------------------------------
template <class T>
PoolCell<T>::PoolCell(PoolCell&& other) noexcept
{
	nonce = other.nonce;
	if (other.isValid()) {
		new(data) T(std::move(*other.get()));
	}
}

// ------------------------------------------------------------------------
template <class T>
PoolCell<T>::PoolCell(PoolCell<T> const& other) noexcept
{
	nonce = other.nonce;
	if (other.isValid()) {
		new(data) T(*other.get());
	}
}

// ------------------------------------------------------------------------
template <class T>
template <class... ArgsTy>
void PoolCell<T>::make(uint32_t _nonce, ArgsTy&&... args)
{
	ASSERT(!isValid());

	nonce = _nonce;
	new(data) T(std::forward<ArgsTy>(args)...);
}

// ------------------------------------------------------------------------
template <class T>
void PoolCell<T>::destroy()
{
	ASSERT(isValid());
	get()->~T();
	nonce = 0;
}

// ------------------------------------------------------------------------
template <class T>
T* PoolCell<T>::get() const
{
	if (isValid()) {
		T const* ptr = reinterpret_cast<T const*>(data);
		return const_cast<T*>(ptr);
	}
	else {
		return nullptr;
	}
}

// ------------------------------------------------------------------------
template <class T>
template <class... ArgsTy>
void Pool<T>::Cell::make(uint32_t nonce, ArgsTy&&... args)
{
	next = UINT_MAX;
	PoolCell<T>::make(nonce, std::forward<ArgsTy>(args)...);
}

// ------------------------------------------------------------------------
template <class T>
void Pool<T>::Cell::destroy()
{
	PoolCell<T>::destroy();
}

// ------------------------------------------------------------------------
// Pool implementation
// ------------------------------------------------------------------------
template <class T>
Pool<T>::Pool(Pool<T>&& other) noexcept
	: m_storage(std::move(other.m_storage))
	, m_head(other.m_head)
{
	other.m_head = UINT_MAX;
	ASSERT(m_head == UINT_MAX || !m_storage[m_head].isValid());
}

// ------------------------------------------------------------------------
template <class T>
Pool<T>& Pool<T>::operator=(Pool<T>&& other) noexcept
{
	std::swap(m_storage, other.m_storage);
	std::swap(m_head, other.m_head);
	return *this;
}


// ------------------------------------------------------------------------
template <class T>
auto Pool<T>::getCell(PoolHandle idx) const -> Cell*
{
	if (m_storage.size() <= idx.index) return nullptr;
	Cell& cell = m_storage[idx.index];

	if (cell.nonce == 0) return nullptr;
	if (cell.nonce != idx.nonce) return nullptr;
	return &cell;
}

// ------------------------------------------------------------------------
template <class T>
T* Pool<T>::operator[](PoolHandle idx) const
{
	Cell* cell = getCell(idx);
	if (cell == nullptr) {
		return nullptr;
	}
	return cell->get();
}

// ------------------------------------------------------------------------
template <class T>
T& Pool<T>::at(PoolHandle idx) const
{
	ASSERT_MSG(idx.index < m_storage.size(), "Element not exist");
	ASSERT_MSG(m_storage[idx.index].nonce != 0, "Element not exist");
	ASSERT_MSG(m_storage[idx.index].nonce == idx.nonce, "Invalid handle");
	return *m_storage[idx.index].get();
}

// ------------------------------------------------------------------------
template <class T>
template <class... ArgsTy>
PoolHandle Pool<T>::alloc(ArgsTy&&... args)
{
	PoolHandle handle;
	handle.nonce = m_autoinc++;
	m_count += 1;

	if (m_head != UINT_MAX) {
		ASSERT(!m_storage[m_head].isValid());

		handle.index = m_head;
		m_head = m_storage[handle.index].next;

		ASSERT_MSG(m_head == UINT_MAX || !m_storage[m_head].isValid(), "inconsistent pool: head %d, handle [%d/%d]", m_head, handle.index, handle.nonce);
		m_storage[handle.index].make(handle.nonce, std::forward<ArgsTy>(args)...);

		ASSERT_MSG(m_head == UINT_MAX || !m_storage[m_head].isValid(), "inconsistent pool (2): head %d, handle [%d/%d]", m_head, handle.index, handle.nonce);
	} else {
		uint32_t idx = (uint32_t)m_storage.size();
		m_storage.emplace_back(UINT_MAX);

		handle.index = idx;
		m_storage[idx].make(handle.nonce, std::forward<ArgsTy>(args)...);
	}
	return handle;
}

// ------------------------------------------------------------------------
template <class T>
void Pool<T>::dealloc(PoolHandle idx)
{
	Cell* cell = getCell(idx);
	if (cell != nullptr) {
		ASSERT(idx.index != m_head);
		ASSERT(m_head == UINT_MAX || !m_storage[m_head].isValid());
		cell->destroy();
		ASSERT(m_head == UINT_MAX || !m_storage[m_head].isValid());
		cell->next = m_head;
		m_head = idx.index;
	}
	ASSERT(m_head == UINT_MAX || !m_storage[m_head].isValid());
	m_count -= 1;
}

// ------------------------------------------------------------------------
template <class T>
bool Pool<T>::check()
{
	auto current = m_head;
	while (current != UINT_MAX) {
		ASSERT(!m_storage[current].isValid());
		if (m_storage[current].isValid()) {
			return false;
		}

		current = m_storage[current].next;
	}
	return true;
}

// ------------------------------------------------------------------------
   // PoolMirror implementation
   // ------------------------------------------------------------------------
template <class T>
T* PoolMirror<T>::operator[](PoolHandle idx) const
{
	if (m_storage.size() <= idx.index) return nullptr;
	PoolCell<T>& cell = m_storage[idx.index];

	if (cell.nonce != idx.nonce) return nullptr;
	if (cell.nonce == 0) return nullptr;
	return cell.get();
}

// ------------------------------------------------------------------------
template <class T>
T& PoolMirror<T>::at(PoolHandle idx) const
{
	ASSERT_MSG(m_storage.size() > idx.index, "Element [%d/%d] not exist", idx.index, idx.nonce);
	ASSERT_MSG(m_storage[idx.index].nonce != 0, "Element [%d/%d] not exist", idx.index, idx.nonce);
	ASSERT_MSG(m_storage[idx.index].nonce == idx.nonce, "Element [%d/%d] not exist", idx.index, idx.nonce);
	return *m_storage[idx.index].get();
}

// ------------------------------------------------------------------------
template <class T>
template <class... ArgsTy>
T& PoolMirror<T>::make(PoolHandle idx, ArgsTy&&... args)
{
	ASSERT(idx.isValid());
	if (idx.index >= m_storage.size()) {
		m_storage.resize(idx.index + 1);
	}

	PoolCell<T>& cell = m_storage[idx.index];
	ASSERT_MSG(!cell.isValid(), "You have to destroy pool element before it construction (elem #%d)", idx.index);
	if (cell.isValid()) {
		cell.destroy();
	}

	cell.make(idx.nonce, std::forward<ArgsTy>(args)...);
	return *reinterpret_cast<T*>(cell.data);
}

// ------------------------------------------------------------------------
#define CHECK(FALLBACK, COND, ...) ASSERT_MSG(COND, __VA_ARGS__); if (!(COND)) { FALLBACK; }
template <class T>
void PoolMirror<T>::destroy(PoolHandle idx)
{
	CHECK(return, idx.isValid(), "Invalid handle value");
	CHECK(return, m_storage.size() > idx.index, "You have to make pool element before it destruction (elem #%d)", idx.index);
	CHECK(return, m_storage[idx.index].nonce == idx.nonce, "You have to make pool element before it destruction (elem #%d)", idx.index);

	PoolCell<T>& cell = m_storage[idx.index];
	cell.destroy();
}
#undef CHECK