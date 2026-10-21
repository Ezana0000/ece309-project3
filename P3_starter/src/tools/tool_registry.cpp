// src/tools/tool_registry.cpp — hash table from tool name to Tool (spec §4.3).
//
// Design: separate chaining, FNV-1a hash, power-of-two bucket count,
// and the table doubles before the load factor would go over 1.0.

#include "tools/tool_registry.h"

#include <cstdint>
#include <utility>

namespace {
constexpr std::size_t kInitialBuckets = 8;   // starting table size (a power of two)
constexpr double kMaxLoadFactor = 1.0;       // grow before going past one tool per bucket
}  // namespace

ToolRegistry::ToolRegistry() : buckets_(kInitialBuckets) {}

// The unique_ptrs in the buckets delete every tool automatically.
ToolRegistry::~ToolRegistry() = default;

// FNV-1a: mixes in every character, so order and every letter matter.
std::size_t ToolRegistry::hash(std::string_view key) noexcept {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(h);
}

// Bucket count is a power of two, so masking is the same as % but faster.
std::size_t ToolRegistry::index_for(std::string_view key) const noexcept {
    return hash(key) & (buckets_.size() - 1);
}

void ToolRegistry::rehash() {
    std::vector<Bucket> bigger(buckets_.size() * 2);

    // Move each unique_ptr into its new bucket; the Tool objects never move.
    for (Bucket& bucket : buckets_) {
        for (std::unique_ptr<Tool>& tool : bucket) {
            std::size_t i = hash(tool->name()) & (bigger.size() - 1);
            bigger[i].push_back(std::move(tool));
        }
    }
    buckets_ = std::move(bigger);
}

bool ToolRegistry::add(std::unique_ptr<Tool> tool) {
    if (tool == nullptr) return false;

    // Duplicate name: returning drops the new unique_ptr, which deletes it.
    const std::string name = tool->name();
    if (find(name) != nullptr) return false;

    // Grow first so the load factor never goes over the limit.
    if (static_cast<double>(count_ + 1) / buckets_.size() > kMaxLoadFactor) {
        rehash();
    }

    buckets_[index_for(name)].push_back(std::move(tool));
    ++count_;
    return true;
}

Tool* ToolRegistry::find(std::string_view name) const {
    // Only the one bucket this name hashes to needs to be searched.
    for (const std::unique_ptr<Tool>& tool : buckets_[index_for(name)]) {
        if (tool->name() == name) return tool.get();
    }
    return nullptr;
}

std::size_t ToolRegistry::size() const noexcept {
    return count_;
}

void ToolRegistry::for_each(const std::function<void(const Tool&)>& fn) const {
    for (const Bucket& bucket : buckets_) {
        for (const std::unique_ptr<Tool>& tool : bucket) fn(*tool);
    }
}

std::size_t ToolRegistry::bucket_count() const noexcept {
    return buckets_.size();
}

double ToolRegistry::load_factor() const noexcept {
    return static_cast<double>(count_) / buckets_.size();
}

// With chaining, the worst lookup is the longest chain.
std::size_t ToolRegistry::max_probe_length() const {
    std::size_t longest = 0;
    for (const Bucket& bucket : buckets_) {
        if (bucket.size() > longest) longest = bucket.size();
    }
    return longest;
}
