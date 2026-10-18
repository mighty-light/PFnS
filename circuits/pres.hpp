#pragma once

#include "../include/types.hpp"
#include "../include/error.hpp"
#include <algorithm>
#include <functional>
#include <cstdint>
#include <cstddef>
#include <span>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PFnS
{

    enum NodeType : uint8_t
    {
        VARIABLE, // aux = input index
        CONST,    // aux = number id
        LINEAR,   // sum_i coeff_i * child_i + bias, count >= 1 (empty sums collapse to CONST)
        MAX,      // max(a, b), operands sorted by id
        MIN,      // min(a, b), operands sorted by id
        DIV,      // floor(a / k), k > 0 enforced by the builder
        EQC       // a == 0 ? b : 0   (a = guard, b = value; order matters)
    };

    // Field meaning by type:
    //   type      count   first          aux
    //   VARIABLE  -       0              input index
    //   CONST     -       0              number id
    //   LINEAR    #terms  offset in lin_ bias (NumId)
    //   MAX/MIN   -       child a        child b
    //   DIV       -       child a        divisor (NumId)
    //   EQC       -       child a (guard) child b (value)
    // Nodes are hash-consed: structurally equal nodes share one NodeId.
    struct Node
    {
        NodeType type;
        uint8_t flags;  // spare, must be 0 (not part of node identity)
        uint16_t count; // LINEAR term count; builder rejects > UINT16_MAX
        uint32_t first;
        uint32_t aux;
    };
    static_assert(sizeof(Node) == 12);
    static_assert(sizeof(NodeId) == 4);
    static_assert(sizeof(NumId) == 4);
    static_assert(std::is_trivially_copyable_v<Node>);

    struct LinTerm
    {
        NodeId child;
        NumId coeff;
    };
    static_assert(sizeof(LinTerm) == 8); // assumes 32-bit NodeId and NumId

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

    class Circuit
    {
    private:
        // ---------------------------------------------------------------------------
        // Number pool
        // ---------------------------------------------------------------------------
        constexpr NumId intern(number_t v)
        {
            auto [it, inserted] = num_index_.try_emplace(v, static_cast<uint32_t>(nums_.size()));
            if (inserted)
                nums_.push_back(v);
            return NumId{it->second};
        }
        constexpr NodeId find_or_insert(Node n)
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

    public:
        // Construction (children must already exist: ids stay topological)
        constexpr NodeId add_var(uint32_t input_index)
        {
            return find_or_insert(Node{VARIABLE, 0, 0, 0, input_index});
        }

        constexpr NodeId add_const(number_t v)
        {
            return find_or_insert(Node{CONST, 0, 0, 0, intern(v)});
        }

        constexpr NodeId add_max(NodeId a, NodeId b)
        {
            skeptical_check(a < nodes_.size() && b < nodes_.size(), "Out of bounds!");
            if (a == b)
                return a; // max(x, x) = x
            if (a > b)
                std::swap(a, b); // commutative: sort by id
            return find_or_insert(Node{MAX, 0, 0, a, b});
        }

        constexpr NodeId add_min(NodeId a, NodeId b)
        {
            skeptical_check(a < nodes_.size() && b < nodes_.size(), "Out of bounds!");
            if (a == b)
                return a;
            if (a > b)
                std::swap(a, b);
            return find_or_insert(Node{MIN, 0, 0, a, b});
        }

        constexpr NodeId add_div(NodeId a, number_t k)
        {
            skeptical_check(k > 0, "Divisor must be positive!");
            skeptical_check(a < nodes_.size(), "Out of bounds!");
            if (k == 1)
                return a; // floor(x / 1) = x
            return find_or_insert(Node{DIV, 0, 0, a, intern(k)});
        }

        constexpr NodeId add_eqc(NodeId guard, NodeId value)
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
        constexpr NodeId add_linear(std::span<const NodeId> ch,
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

        // Queries
        // Spans/references returned here are invalidated by the next add_* call.
        constexpr size_t size() const { return nodes_.size(); }
        constexpr NodeType type(NodeId n) const { return nodes_[n].type; }
        constexpr uint32_t input_index(NodeId n) const
        {
            skeptical_check(n < nodes_.size(), "Out of bounds!");
            const Node &x = nodes_[n];
            skeptical_check(x.type == VARIABLE, "Not Variable");
            return x.aux;
        }
        constexpr number_t constant_value(NodeId n) const
        {
            skeptical_check(n < nodes_.size(), "Out of bounds!");
            const Node &x = nodes_[n];
            skeptical_check(x.type == CONST, "Not Constant");
            skeptical_check(x.aux < nums_.size(), "Out of bounds!");
            return nums_[x.aux];
        }
        constexpr NodeId a(NodeId n) const
        {
            const Node &x = nodes_[n];
            switch (x.type)
            {
#ifdef SKEPTICAL
            case VARIABLE:
            case CONST:
            case LINEAR:
                throw exception_with_location("Not MAX/MIN/DIV/EQC");
#endif
            default:
                return NodeId{x.first};
            }
        }
        constexpr NodeId b(NodeId n) const
        {
            const Node &x = nodes_[n];
            switch (x.type)
            {
#ifdef SKEPTICAL
            case VARIABLE:
            case CONST:
            case LINEAR:
            case DIV:
                throw exception_with_location("Not MAX/MIN/EQC");
#endif
            default:
                return NodeId{x.aux};
            }
        }

        constexpr std::span<const LinTerm> terms(NodeId n) const
        {
            skeptical_check(n < nodes_.size(), "Out of bounds!");
            const Node &x = nodes_[n];
            skeptical_check(x.type == LINEAR, "Not Linear");
            return {lin_.data() + x.first, x.count};
        }

        constexpr number_t bias(NodeId n) const
        {
            const Node &x = nodes_[n];
            skeptical_check(x.type == LINEAR, "Not Linear");
            skeptical_check(x.aux < nums_.size(), "Out of bounds!");
            return nums_[x.aux];
        } // LINEAR, resolves NumId via pool

        constexpr number_t coeff(NumId c) const
        {
            skeptical_check(c < nums_.size(), "Out of bounds!");
            return nums_[c];
        }
        constexpr number_t divisor(NodeId n) const
        {
            skeptical_check(n < nodes_.size(), "Out of bounds!");
            const uint32_t x = nodes_[n].aux;
            skeptical_check(x < nums_.size(), "Out of bounds!");
            return nums_[x];
        }

        void add_output(NodeId n)
        {

            skeptical_check(n < nodes_.size(), "Out of bounds!");
            outputs_.push_back(n);
        }
        constexpr const std::vector<NodeId> &outputs() const { return outputs_; }

        // Hashes the fields that define identity: type, then
        //   LINEAR : every (child, coeff) term, then bias (aux)   [first is an offset: skipped]
        //   others : (first, aux)
        constexpr uint64_t hash_node(const Node &n) const
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

        constexpr bool equal_nodes(const Node &x, const Node &y) const
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
        constexpr void grow_table()
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

    private:
        std::vector<Node> nodes_;
        std::vector<LinTerm> lin_;
        std::vector<number_t> nums_;                       // number pool (deduped), swap for GMP later
        std::unordered_map<number_t, uint32_t> num_index_; // value -> index in nums_
        std::vector<NodeId> outputs_;
        // Hash-consing table: open addressing, linear probing, power-of-two size,
        // holds ids into nodes_. Empty slots hold the sentinel UINT32_MAX.
        std::vector<NodeId> table_;
        // Scratch for add_linear canonicalization (avoids a heap alloc per call).
        std::vector<std::pair<uint32_t, number_t>> scratch_;
    };

} // namespace PFnS
