#pragma once

#include <array>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <type_traits>

#include <glog/logging.h>

namespace spmc
{

inline constexpr bool is_power_two(int N)
{
	return ((N & (N-1)) == 0);
};

static constexpr std::size_t g_ring_buffer_size = 1024;

template<typename Ptr>
class alignas(64) ring_buffer
{
public:
	static_assert(std::is_pointer<Ptr>::value, "buffer item should be a pointer");

	using index_type = std::int64_t;
	using value_type = std::atomic<Ptr>;
	using cursor_type = std::atomic<index_type>;
	using buffer_type = std::vector<value_type>;

	ring_buffer(index_type arity=g_ring_buffer_size)
		: m_pptr(0)
		, m_gptr(0)
		, m_ticket(0)
		, m_arity(arity)
		, m_mask(arity - 1)
		, m_buffer(arity)
	{
		for(value_type &v : m_buffer)
			v.store(nullptr, std::memory_order_release);
		assert(is_power_two(arity));
	}

public:
	inline void push(Ptr p)
	{
		Ptr vp = nullptr;
		value_type &v = get(m_pptr++);
		while(!v.compare_exchange_weak(vp, p))
		{
			vp = nullptr;
			std::this_thread::yield();
		}
		m_ticket.fetch_add(1);
	}
	inline Ptr pop()
	{
		index_type ticket = m_ticket.fetch_sub(1);
		if(ticket <= 0)
		{
			m_ticket.fetch_add(1);
			return nullptr;
		}
		index_type ginx = m_gptr.fetch_add(1);
		return get(ginx).exchange(nullptr);
	}
	inline std::size_t pop(std::vector<Ptr> &op)
	{
		index_type ticket = m_ticket.load(std::memory_order_acquire);
		if(ticket <= 0)
			return 0;
		index_type nticket = m_ticket.fetch_sub(ticket);
		if(nticket <= 0)
		{
			m_ticket.fetch_add(ticket);
			return 0;
		}
		index_type det = ticket - nticket;
		if(det > 0)
		{
			m_ticket.fetch_add(det);
			ticket = nticket;
		}
		for(index_type i = 0; i < ticket; ++i)
		{
			index_type ginx = m_gptr.fetch_add(1);
			op.emplace_back(get(ginx).exchange(nullptr));
		}
		return ticket;
	}

	inline std::size_t size() const
	{
		return m_buffer.size();
	}
private:
	inline value_type & get(index_type inx)
	{
		return m_buffer[inx & m_mask];
	}

	index_type		m_pptr;
	char			m_padding_1[64-sizeof(index_type)];
	cursor_type		m_gptr;
	char			m_padding_2[64-sizeof(cursor_type)];
	cursor_type		m_ticket;
	char			m_padding_3[64-sizeof(cursor_type)];
	index_type		m_arity;
	index_type		m_mask;
	char			m_padding_4[64-sizeof(index_type)*2];
	buffer_type		m_buffer;
};

}
