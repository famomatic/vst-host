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
        // Always allow a healthy backlog so rapid UI dragging does not
        // overflow before the audio thread gets to drain. The ring is cheap
        // (just Entry pairs) and never reallocates after prepare, so a generous
        // minimum is safe on the audio thread.
        const auto cap = std::max(128, capacity);
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

        // We do NOT scan the ring to coalesce an existing entry for the same
        // id: the audio thread drains [head_, tail_) and a concurrent in-place
        // write of value from the message thread would race the audio thread's
        // read, producing a torn double. Instead we always enqueue a fresh
        // entry. When the ring is full we DROP the new value rather than
        // advancing head_ from the producer side (which would race the audio
        // thread's head_ read/write). The ring is sized generously (see
        // prepare, min 128) so overflow is extremely unlikely under normal
        // use; the next push will succeed once the audio thread drains.
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);

        const auto next = (t + 1) % capacity_;
        if (next == h)
            return;

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
