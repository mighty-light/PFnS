#pragma once

#include "../include/types.hpp"
#include <cstdint>
#include <cstddef>
#include <span>
#include <type_traits>
#include <vector>

namespace PFnS {

enum NodeType : uint8_t {
    VARIABLE,  // aux = input index
    CONST,     // aux = number id
    LINEAR,    // sum_i coeff_i * child_i + bias, count >= 0 (count == 0 is a constant)
    MAX,       // max(a, b), operands sorted by id
    MIN,       // min(a, b), operands sorted by id
    DIV,       // floor(a / k), k > 0 enforced by the builder
    EQC        // a == 0 ? b : 0   (a = guard, b = value; order matters)
};

// Field meaning by type:
//   type      count   first          aux
//   VARIABLE  -       -              input index
//   CONST     -       -              number id
//   LINEAR    #terms  offset in lin_ bias (NumId)
//   MAX/MIN   -       child a        child b
//   DIV       -       child a        divisor (NumId)
//   EQC       -       child a (guard) child b (value)
struct Node {
    NodeType type;
    uint8_t  flags;   // spare
    uint16_t count;
    uint32_t first;
    uint32_t aux;
};
static_assert(sizeof(Node) == 12);
static_assert(std::is_trivially_copyable_v<Node>);

struct LinTerm {
    NodeId child;
    NumId  coeff;
};
static_assert(sizeof(LinTerm) == 8);

class Circuit {
public:
    // Construction (children must already exist: ids stay topological)
    NodeId add_var(uint32_t input_index);
    NodeId add_const(number_t v);
    NodeId add_linear(std::span<const NodeId> ch,
                      std::span<const number_t> co, number_t bias);
    NodeId add_max(NodeId a, NodeId b);
    NodeId add_min(NodeId a, NodeId b);
    NodeId add_div(NodeId a, number_t k); // asserts k > 0
    NodeId add_eqc(NodeId guard, NodeId value);

    // Queries
    size_t size() const { return nodes_.size(); }
    NodeType type(NodeId n) const { return nodes_[n.v].type; }
    NodeId a(NodeId n) const { return NodeId{nodes_[n.v].first}; }   // MAX/MIN/DIV/EQC
    NodeId b(NodeId n) const { return NodeId{nodes_[n.v].aux}; }     // MAX/MIN/EQC
    std::span<const LinTerm> terms(NodeId n) const {         // LINEAR only
        const Node& x = nodes_[n.v];
        return {lin_.data() + x.first, x.count};
    }
    number_t bias(NodeId n) const;       // LINEAR, resolves NumId via pool
    number_t coeff(NumId c) const;
    number_t divisor(NodeId n) const;    // DIV

    void add_output(NodeId n) {
        #ifdef SKEPTICAL
            if (n.v >= nodes_.size())
                throw PFnS::exception_with_location("Out of bounds!");
        #endif
        outputs_.push_back(n);
    }
    const std::vector<NodeId>& outputs() const { return outputs_; }

private:
    NumId intern(number_t v);            // later: inline small / GMP pool

    std::vector<Node>      nodes_;
    std::vector<LinTerm>   lin_;
    std::vector<number_t>  nums_;        // number pool (deduped), swap for GMP later
    std::vector<NodeId>    outputs_;
    // hash-consing table: open-addressing vector<NodeId>, added next
    std::vector<NodeId>    table_;
};

} // namespace PFnS