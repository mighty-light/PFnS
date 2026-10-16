#include "pres.hpp"

#include <algorithm>

namespace PFnS {

// ---------------------------------------------------------------------------
// Hashing
//
// mix64 is the 64-bit finalizer of splitmix64 (S. Vigna, public domain,
// https://prng.di.unimi.it/splitmix64.c), whose constants come from
// D. Stafford's "variant 13" mixer:
//   https://zimbry.blogspot.com/2011/09/better-bit-mixing-improving-on.html
// Boost.ContainerHash documents the same function in
// boost/container_hash/detail/hash_mix.hpp.
//
// kGamma = floor(2^64 / golden ratio), the splitmix64 increment. Adding it
// before each mix keeps a zero state/input from mapping to zero.
//
// combine() is the splitmix64 step (state += gamma; mix) with the input word
// added in. The construction is ours, not a published hash. That is fine here:
// equality is always verified in equal_nodes(), so hash quality only affects
// speed, never correctness. The one requirement is that hash_node() reads
// exactly the fields equal_nodes() compares.
// ---------------------------------------------------------------------------
namespace {

constexpr uint64_t kGamma = 0x9e3779b97f4a7c15ULL;
constexpr uint32_t kEmpty = 0xFFFFFFFFu;   // empty table slot sentinel

inline uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

inline uint64_t combine(uint64_t h, uint64_t v) {
    return mix64(h + kGamma + v);
}

inline uint64_t pack(uint32_t hi, uint32_t lo) {
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

} // namespace

// Hashes the fields that define identity: type, then
//   LINEAR : every (child, coeff) term, then bias (aux)   [first is an offset: skipped]
//   others : (first, aux)
uint64_t Circuit::hash_node(const Node& n) const {
    uint64_t h = combine(0, n.type);
    if (n.type == LINEAR) {
        const LinTerm* t = lin_.data() + n.first;
        for (uint32_t i = 0; i < n.count; ++i)
            h = combine(h, pack(t[i].child.v, t[i].coeff.v));
        return combine(h, n.aux);
    }
    return combine(h, pack(n.first, n.aux));
}

bool Circuit::equal_nodes(const Node& x, const Node& y) const {
    if (x.type != y.type || x.aux != y.aux) return false;
    if (x.type != LINEAR) return x.first == y.first;
    if (x.count != y.count) return false;
    const LinTerm* tx = lin_.data() + x.first;
    const LinTerm* ty = lin_.data() + y.first;
    for (uint32_t i = 0; i < x.count; ++i)
        if (tx[i].child.v != ty[i].child.v || tx[i].coeff.v != ty[i].coeff.v)
            return false;
    return true;
}

// Doubles the table (min 16) and reinserts every existing node.
void Circuit::grow_table() {
    const size_t cap = table_.empty() ? 16 : table_.size() * 2;
    table_.assign(cap, NodeId{kEmpty});
    const size_t mask = cap - 1;
    for (uint32_t id = 0; id < nodes_.size(); ++id) {
        size_t i = hash_node(nodes_[id]) & mask;
        while (table_[i].v != kEmpty) i = (i + 1) & mask;
        table_[i] = NodeId{id};
    }
}

NodeId Circuit::find_or_insert(Node n) {
    #ifdef SKEPTICAL
        if (nodes_.size() >= kEmpty)
            throw PFnS::exception_with_location("Too many nodes!");
    #endif
    // Keep load factor <= 0.5; every node is in the table, so load = nodes_.size().
    while ((nodes_.size() + 1) * 2 > table_.size()) grow_table();

    const size_t mask = table_.size() - 1;
    size_t i = hash_node(n) & mask;
    while (table_[i].v != kEmpty) {
        if (equal_nodes(nodes_[table_[i].v], n)) {
            if (n.type == LINEAR)   // drop the tentative terms at the tail of lin_
                lin_.erase(lin_.begin() + static_cast<std::ptrdiff_t>(n.first), lin_.end());
            return table_[i];
        }
        i = (i + 1) & mask;
    }
    const NodeId id{static_cast<uint32_t>(nodes_.size())};
    nodes_.push_back(n);
    table_[i] = id;
    return id;
}

// ---------------------------------------------------------------------------
// Number pool
// ---------------------------------------------------------------------------
NumId Circuit::intern(number_t v) {
    auto [it, inserted] = num_index_.try_emplace(v, static_cast<uint32_t>(nums_.size()));
    if (inserted) nums_.push_back(v);
    return NumId{it->second};
}

number_t Circuit::coeff(NumId c) const { return nums_[c.v]; }
number_t Circuit::bias(NodeId n) const { return nums_[nodes_[n.v].aux]; }
number_t Circuit::divisor(NodeId n) const { return nums_[nodes_[n.v].aux]; }

// ---------------------------------------------------------------------------
// Builders: canonicalize, then find_or_insert
// ---------------------------------------------------------------------------
NodeId Circuit::add_var(uint32_t input_index) {
    return find_or_insert(Node{VARIABLE, 0, 0, 0, input_index});
}

NodeId Circuit::add_const(number_t v) {
    return find_or_insert(Node{CONST, 0, 0, 0, intern(v).v});
}

NodeId Circuit::add_max(NodeId a, NodeId b) {
    #ifdef SKEPTICAL
        if (a.v >= nodes_.size() || b.v >= nodes_.size())
            throw PFnS::exception_with_location("Out of bounds!");
    #endif
    if (a.v == b.v) return a;           // max(x, x) = x
    if (a.v > b.v) std::swap(a, b);     // commutative: sort by id
    return find_or_insert(Node{MAX, 0, 0, a.v, b.v});
}

NodeId Circuit::add_min(NodeId a, NodeId b) {
    #ifdef SKEPTICAL
        if (a.v >= nodes_.size() || b.v >= nodes_.size())
            throw PFnS::exception_with_location("Out of bounds!");
    #endif
    if (a.v == b.v) return a;
    if (a.v > b.v) std::swap(a, b);
    return find_or_insert(Node{MIN, 0, 0, a.v, b.v});
}

NodeId Circuit::add_div(NodeId a, number_t k) {
    #ifdef SKEPTICAL
        if (k <= 0)
            throw PFnS::exception_with_location("Divisor must be positive!");
        if (a.v >= nodes_.size())
            throw PFnS::exception_with_location("Out of bounds!");
    #endif
    if (k == 1) return a;               // floor(x / 1) = x
    return find_or_insert(Node{DIV, 0, 0, a.v, intern(k).v});
}

NodeId Circuit::add_eqc(NodeId guard, NodeId value) {
    #ifdef SKEPTICAL
        if (guard.v >= nodes_.size() || value.v >= nodes_.size())
            throw PFnS::exception_with_location("Out of bounds!");
    #endif
    // Order matters (guard, value): no operand sorting.
    return find_or_insert(Node{EQC, 0, 0, guard.v, value.v});
}

// Canonical form: terms sorted by child id, duplicate children merged,
// zero coefficients dropped. An empty sum becomes a CONST, and the identity
// sum 1 * x + 0 becomes x itself.
// Note: merging coefficients can overflow number_t (long long); revisit when
// the pool moves to GMP.
NodeId Circuit::add_linear(std::span<const NodeId> ch,
                           std::span<const number_t> co, number_t bias) {
    #ifdef SKEPTICAL
        if (ch.size() != co.size())
            throw PFnS::exception_with_location("Children/coefficients size mismatch!");
        for (const NodeId c : ch)
            if (c.v >= nodes_.size())
                throw PFnS::exception_with_location("Out of bounds!");
    #endif

    scratch_.clear();
    for (size_t i = 0; i < ch.size(); ++i)
        scratch_.emplace_back(ch[i].v, co[i]);
    std::sort(scratch_.begin(), scratch_.end(),
              [](const auto& x, const auto& y) { return x.first < y.first; });

    // Merge equal children in place, dropping terms whose coefficient cancels to 0.
    size_t out = 0;
    for (size_t i = 0; i < scratch_.size();) {
        const uint32_t child = scratch_[i].first;
        number_t sum = 0;
        for (; i < scratch_.size() && scratch_[i].first == child; ++i)
            sum += scratch_[i].second;
        if (sum != 0) scratch_[out++] = {child, sum};
    }
    scratch_.resize(out);

    if (scratch_.empty()) return add_const(bias);
    if (scratch_.size() == 1 && scratch_[0].second == 1 && bias == 0)
        return NodeId{scratch_[0].first};

    #ifdef SKEPTICAL
        if (scratch_.size() > UINT16_MAX)
            throw PFnS::exception_with_location("Too many linear terms!");
    #endif

    // Append tentatively; find_or_insert removes them again on a hit.
    const uint32_t offset = static_cast<uint32_t>(lin_.size());
    for (const auto& [child, c] : scratch_)
        lin_.push_back(LinTerm{NodeId{child}, intern(c)});
    return find_or_insert(Node{LINEAR, 0, static_cast<uint16_t>(scratch_.size()),
                               offset, intern(bias).v});
}

} // namespace PFnS
