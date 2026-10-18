#include "pres.hpp"

#include <algorithm>
#include <ranges>
#include <functional>

namespace PFnS
{

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
    namespace
    {

        constexpr uint64_t kGamma = 0x9e3779b97f4a7c15ULL;
        constexpr uint32_t kEmpty = 0xFFFFFFFFu; // empty table slot sentinel

        constexpr uint64_t mix64(uint64_t z)
        {
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            return z ^ (z >> 31);
        }

        constexpr uint64_t combine(uint64_t h, uint64_t v)
        {
            return mix64(h + kGamma + v);
        }

        constexpr uint64_t pack(uint32_t hi, uint32_t lo)
        {
            return (static_cast<uint64_t>(hi) << 32) | lo;
        }

    } // namespace

    // Hashes the fields that define identity: type, then
    //   LINEAR : every (child, coeff) term, then bias (aux)   [first is an offset: skipped]
    //   others : (first, aux)
    constexpr uint64_t Circuit::hash_node(const Node &n) const
    {
        uint64_t h = combine(0, n.type);
        if (n.type == LINEAR)
        {
            const LinTerm *t = lin_.data() + n.first;
            for (uint32_t i = 0; i < n.count; ++i)
                h = combine(h, pack(t[i].child, t[i].coeff));
            return combine(h, n.aux);
        }
        return combine(h, pack(n.first, n.aux));
    }

    constexpr bool Circuit::equal_nodes(const Node &x, const Node &y) const
    {
        if (x.type != y.type || x.aux != y.aux)
            return false;
        if (x.type != LINEAR)
            return x.first == y.first;
        if (x.count != y.count)
            return false;
        const LinTerm *tx = lin_.data() + x.first;
        const LinTerm *ty = lin_.data() + y.first;
        for (uint32_t i = 0; i < x.count; ++i)
            if (tx[i].child != ty[i].child || tx[i].coeff != ty[i].coeff)
                return false;
        return true;
    }

    // Doubles the table (min 16) and reinserts every existing node.
    constexpr void Circuit::grow_table()
    {
        const size_t cap = table_.empty() ? 16 : table_.size() * 2;
        table_.assign(cap, NodeId{kEmpty});
        const size_t mask = cap - 1;
        for (uint32_t id = 0; id < nodes_.size(); ++id)
        {
            size_t i = hash_node(nodes_[id]) & mask;
            while (table_[i] != kEmpty)
                i = (i + 1) & mask;
            table_[i] = NodeId{id};
        }
    }

    constexpr NodeId Circuit::find_or_insert(Node n)
    {
        skeptical_check(nodes_.size() < kEmpty, "Too many nodes!");
        // Keep load factor <= 0.5; every node is in the table, so load = nodes_.size().
        while ((nodes_.size() + 1) * 2 > table_.size())
            grow_table();

        const size_t mask = table_.size() - 1;
        size_t i = hash_node(n) & mask;
        while (table_[i] != kEmpty)
        {
            if (equal_nodes(nodes_[table_[i]], n))
            {
                if (n.type == LINEAR) // drop the tentative terms at the tail of lin_
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
    constexpr NumId Circuit::intern(number_t v)
    {
        auto [it, inserted] = num_index_.try_emplace(v, static_cast<uint32_t>(nums_.size()));
        if (inserted)
            nums_.push_back(v);
        return NumId{it->second};
    }

    // ---------------------------------------------------------------------------
    // Builders: canonicalize, then find_or_insert
    // ---------------------------------------------------------------------------
    constexpr NodeId Circuit::add_var(uint32_t input_index)
    {
        return find_or_insert(Node{VARIABLE, 0, 0, 0, input_index});
    }

    constexpr NodeId Circuit::add_const(number_t v)
    {
        return find_or_insert(Node{CONST, 0, 0, 0, intern(v)});
    }

    constexpr NodeId Circuit::add_max(NodeId a, NodeId b)
    {
        skeptical_check(a < nodes_.size() && b < nodes_.size(), "Out of bounds!");
        if (a == b)
            return a; // max(x, x) = x
        if (a > b)
            std::swap(a, b); // commutative: sort by id
        return find_or_insert(Node{MAX, 0, 0, a, b});
    }

    constexpr NodeId Circuit::add_min(NodeId a, NodeId b)
    {
        skeptical_check(a < nodes_.size() && b < nodes_.size(), "Out of bounds!");
        if (a == b)
            return a;
        if (a > b)
            std::swap(a, b);
        return find_or_insert(Node{MIN, 0, 0, a, b});
    }

    constexpr NodeId Circuit::add_div(NodeId a, number_t k)
    {
        skeptical_check(k > 0, "Divisor must be positive!");
        skeptical_check(a < nodes_.size(), "Out of bounds!");
        if (k == 1)
            return a; // floor(x / 1) = x
        return find_or_insert(Node{DIV, 0, 0, a, intern(k)});
    }

    constexpr NodeId Circuit::add_eqc(NodeId guard, NodeId value)
    {
        skeptical_check(guard < nodes_.size() && value < nodes_.size(), "Out of bounds!");
        // Order matters (guard, value): no operand sorting.
        return find_or_insert(Node{EQC, 0, 0, guard, value});
    }

    // Canonical form: terms sorted by child id, duplicate children merged,
    // zero coefficients dropped. An empty sum becomes a CONST, and the identity
    // sum 1 * x + 0 becomes x itself.
    // Note: merging coefficients can overflow number_t (long long); revisit when
    // the pool moves to GMP.
    constexpr NodeId Circuit::add_linear(std::span<const NodeId> ch,
                                         std::span<const number_t> co, number_t bias)
    {
        skeptical_check(ch.size() == co.size(), "Children/coefficients size mismatch!");
        for (const NodeId c : ch)
            skeptical_check(c < nodes_.size(), "Out of bounds!");

        scratch_.clear();
        for (size_t i = 0; i < ch.size(); ++i)
            scratch_.emplace_back(ch[i], co[i]);

        std::ranges::sort(scratch_, {}, &std::pair<uint32_t, number_t>::first);

        // Merge equal children in place, dropping terms whose coefficient cancels to 0.
        size_t out = 0;
        for (size_t i = 0; i < scratch_.size();)
        {
            const uint32_t child = scratch_[i].first;
            number_t sum = number_t(0);
            for (; i < scratch_.size() && scratch_[i].first == child; ++i)
                sum = sum + scratch_[i].second;
            if (sum != 0)
                scratch_[out++] = {child, sum};
        }
        scratch_.resize(out);

        if (scratch_.empty())
            return add_const(bias);
        if (scratch_.size() == 1 && scratch_[0].second == 1 && bias == 0)
            return NodeId{scratch_[0].first};

        skeptical_check(scratch_.size() <= UINT16_MAX, "Too many linear terms!");

        // Append tentatively; find_or_insert removes them again on a hit.
        const uint32_t offset = static_cast<uint32_t>(lin_.size());
        for (const auto &[child, c] : scratch_)
            lin_.push_back(LinTerm{NodeId{child}, intern(c)});
        return find_or_insert(Node{LINEAR, 0, static_cast<uint16_t>(scratch_.size()),
                                   offset, intern(bias)});
    }

} // namespace PFnS
