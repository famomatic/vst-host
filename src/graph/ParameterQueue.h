#pragma once

#include <atomic>
#include <vector>
#include <cstring>
#include <algorithm>

namespace host::graph
{
/// Lock-free single-producer (message thread) / single-consumer (audio thread)
/// parameter change queue for a single node.
///
/// The message thread pushes (id, value) pairs; the audio thread drains all
/// pending changes at the top of process() so a parameter move applies cleanly
/// at a block boundary - no tearing, no locks, no allocations on the audio
/// thread. This is the "delay-free" part: changes land at the next block
/// boundary rather than being deferred by a buffer of swaps.
///
/// Capacity is fixed at prepare() time; overflow drops the oldest entry for
/// the same id so the freshest value always wins.
class ParameterQueue
{
public:
    struct Entry
    {
        std::size_t idHash { 0 };
        double value { 0.0 };
    };

    ParameterQueue() = default;

    void prepare(int capacity)
    {
        const auto cap = std::max(8, capacity);
        ring_.resize(static_cast<std::size_t>(cap));
        capacity_ = static_cast<std::size_t>(cap);
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    void clear()
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    /// Message thread: enqueue a parameter change. May drop the oldest pending
    /// entry for the same id so the latest value is always consumed.
    void push(std::size_t idHash, double value)
    {
        if (ring_.empty())
            return; // not prepared yet - message-thread fallback handled by caller

        // Coalesce: if the same id is already queued, update it in place
        // rather than enqueuing a duplicate. This bounds queue depth under
        // rapid UI dragging.
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);
        std::size_t i = h;
        while (i != t)
        {
            if (ring_[i].idHash == idHash)
            {
                ring_[i].value = value;
                return;
            }
            i = (i + 1) % capacity_;
        }

        const auto next = (t + 1) % capacity_;
        if (next == h)
        {
            // Full: overwrite the oldest. This loses a change but keeps the
            // freshest for this id, which is what matters for live tweaking.
            const auto newHead = (h + 1) % capacity_;
            ring_[t] = { idHash, value };
            head_.store(newHead, std::memory_order_release);
            tail_.store(next, std::memory_order_release);
            return;
        }

        ring_[t] = { idHash, value };
        tail_.store(next, std::memory_order_release);
    }

    /// True once prepare() has allocated the ring. Lets callers guard against
    /// pushing before the audio stream is set up (e.g. a value arriving during
    /// project load, before the node is prepared).
    bool isPrepared() const noexcept { return ! ring_.empty(); }

    /// Audio thread: drain all pending changes into `out`.
    void drain(std::vector<Entry>& out)
    {
        out.clear();
        const auto h = head_.load(std::memory_order_acquire);
        const auto t = tail_.load(std::memory_order_relaxed);
        std::size_t i = h;
        while (i != t)
        {
            out.push_back(ring_[i]);
            i = (i + 1) % capacity_;
        }
        head_.store(t, std::memory_order_release);
    }

private:
    std::vector<Entry> ring_;
    std::size_t capacity_ { 16 };
    // head = next to consume, tail = next to produce
    std::atomic<std::size_t> head_ { 0 };
    std::atomic<std::size_t> tail_ { 0 };
};
} // namespace host::graph
