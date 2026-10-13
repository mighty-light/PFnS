#pragma once

// SMT-LIB 2.7 (release 2026-09-09) lexical tokens.
// Source: Section 3.1 "Lexicon" and Figure 3.7 "SMT-LIB Commands".

#include <cstdint>
#include <string_view>

namespace PFnS {
namespace smtlib {

// ---------------------------------------------------------------------------
// Reserved words
// Basic set (Sec. 3.1):  BINARY DECIMAL HEXADECIMAL NUMERAL STRING
//                        _ ! as lambda let exists forall match par
// Plus every command name from Figure 3.7. Keep commands LAST so that
// isCommandName() can be a single range check.
// Appendix B's list of command names omits define-funs-rec, but Figure 3.7 has
// it as a command and Sec. 3.1 reserves every command name, so it stays.
// NOT reserved: the predefined symbols below (true, false, sat, ...), theory
// names, "->", "@".
// ---------------------------------------------------------------------------
#define SMTLIB_RESERVED_WORDS(X)                                              \
  X(Binary, "BINARY") X(Decimal, "DECIMAL") X(Hexadecimal, "HEXADECIMAL")     \
  X(Numeral, "NUMERAL") X(String, "STRING")                                   \
  X(Underscore, "_") X(Bang, "!") X(As, "as") X(Lambda, "lambda")             \
  X(Let, "let") X(Exists, "exists") X(Forall, "forall") X(Match, "match")     \
  X(Par, "par")                                                               \
  X(Assert, "assert") X(CheckSat, "check-sat")                                \
  X(CheckSatAssuming, "check-sat-assuming")                                   \
  X(DeclareConst, "declare-const") X(DeclareDatatype, "declare-datatype")     \
  X(DeclareDatatypes, "declare-datatypes") X(DeclareFun, "declare-fun")       \
  X(DeclareSort, "declare-sort")                                              \
  X(DeclareSortParameter, "declare-sort-parameter")                           \
  X(DefineConst, "define-const") X(DefineFun, "define-fun")                   \
  X(DefineFunRec, "define-fun-rec") X(DefineFunsRec, "define-funs-rec")       \
  X(DefineSort, "define-sort") X(Echo, "echo") X(Exit, "exit")                \
  X(GetAssertions, "get-assertions") X(GetAssignment, "get-assignment")       \
  X(GetInfo, "get-info") X(GetModel, "get-model")                             \
  X(GetOption, "get-option") X(GetProof, "get-proof")                         \
  X(GetUnsatAssumptions, "get-unsat-assumptions")                             \
  X(GetUnsatCore, "get-unsat-core") X(GetValue, "get-value")                  \
  X(Pop, "pop") X(Push, "push") X(Reset, "reset")                             \
  X(ResetAssertions, "reset-assertions") X(SetInfo, "set-info")               \
  X(SetLogic, "set-logic") X(SetOption, "set-option")

enum class Reserved : std::uint8_t {
    None,
#define X(id, str) id,
    SMTLIB_RESERVED_WORDS(X)
#undef X
};

struct ReservedEntry {
    std::string_view text;
    Reserved id;
};

inline constexpr ReservedEntry kReservedTable[] = {
#define X(id, str) {str, Reserved::id},
    SMTLIB_RESERVED_WORDS(X)
#undef X
};

constexpr Reserved lookupReserved(std::string_view s) noexcept {
    for (const auto& e : kReservedTable)
        if (e.text == s) return e.id;
    return Reserved::None;
}

constexpr bool isCommandName(Reserved r) noexcept {
    return r >= Reserved::Assert;  // commands are declared last
}

// ---------------------------------------------------------------------------
// Predefined symbols (Appendix B)
// These have a predefined meaning in 2.7 but are NOT reserved words: they lex
// as ordinary symbols and a script could in principle reuse them as its own
// sort or function names. Look them up with Token::predefined().
// ---------------------------------------------------------------------------
#define SMTLIB_PREDEFINED_SYMBOLS(X)                                          \
  X(Bool, "Bool") X(ContinuedExecution, "continued-execution")                \
  X(Error, "error") X(False, "false") X(ImmediateExit, "immediate-exit")      \
  X(Incomplete, "incomplete") X(Logic, "logic") X(Memout, "memout")           \
  X(Sat, "sat") X(Success, "success") X(Theory, "theory") X(True, "true")     \
  X(Unknown, "unknown") X(Unsupported, "unsupported") X(Unsat, "unsat")

enum class Predefined : std::uint8_t {
    None,
#define X(id, str) id,
    SMTLIB_PREDEFINED_SYMBOLS(X)
#undef X
};

struct PredefinedEntry {
    std::string_view text;
    Predefined id;
};

inline constexpr PredefinedEntry kPredefinedTable[] = {
#define X(id, str) {str, Predefined::id},
    SMTLIB_PREDEFINED_SYMBOLS(X)
#undef X
};

constexpr Predefined lookupPredefined(std::string_view s) noexcept {
    for (const auto& e : kPredefinedTable)
        if (e.text == s) return e.id;
    return Predefined::None;
}

// ---------------------------------------------------------------------------
// Predefined keywords (Appendix B)
// Names are stored WITHOUT the leading ':' so they match Token::payload().
// Solver-specific keywords are legal too; they simply map to Keyword::None.
// Appendix B's list omits :pairwise (Sec. 3.7) and :produce-assertions
// (Figure 3.8), so both are added here. :interactive-mode is the deprecated
// old name of :produce-assertions (Sec. 1.1.6).
// ---------------------------------------------------------------------------
#define SMTLIB_KEYWORDS(X)                                                    \
  X(AllStatistics, "all-statistics")                                          \
  X(AssertionStackLevels, "assertion-stack-levels") X(Authors, "authors")     \
  X(Category, "category") X(Chainable, "chainable")                           \
  X(Definition, "definition")                                                 \
  X(DiagnosticOutputChannel, "diagnostic-output-channel")                     \
  X(ErrorBehavior, "error-behavior") X(Extensions, "extensions")              \
  X(Funs, "funs") X(FunsDescription, "funs-description")                      \
  X(GlobalDeclarations, "global-declarations")                                \
  X(InteractiveMode, "interactive-mode") X(Language, "language")              \
  X(LeftAssoc, "left-assoc") X(License, "license") X(Name, "name")            \
  X(Named, "named") X(Notes, "notes") X(Pairwise, "pairwise")                 \
  X(Pattern, "pattern") X(PrintSuccess, "print-success")                      \
  X(ProduceAssertions, "produce-assertions")                                  \
  X(ProduceAssignments, "produce-assignments")                                \
  X(ProduceModels, "produce-models") X(ProduceProofs, "produce-proofs")       \
  X(ProduceUnsatAssumptions, "produce-unsat-assumptions")                     \
  X(ProduceUnsatCores, "produce-unsat-cores") X(RandomSeed, "random-seed")    \
  X(ReasonUnknown, "reason-unknown")                                          \
  X(RegularOutputChannel, "regular-output-channel")                           \
  X(ReproducibleResourceLimit, "reproducible-resource-limit")                 \
  X(RightAssoc, "right-assoc") X(SmtLibVersion, "smt-lib-version")            \
  X(Sorts, "sorts") X(SortsDescription, "sorts-description")                  \
  X(Source, "source") X(Status, "status") X(Theories, "theories")             \
  X(Values, "values") X(Verbosity, "verbosity") X(Version, "version")

enum class Keyword : std::uint8_t {
    None,
#define X(id, str) id,
    SMTLIB_KEYWORDS(X)
#undef X
};

struct KeywordEntry {
    std::string_view text;
    Keyword id;
};

inline constexpr KeywordEntry kKeywordTable[] = {
#define X(id, str) {str, Keyword::id},
    SMTLIB_KEYWORDS(X)
#undef X
};

constexpr Keyword lookupKeyword(std::string_view s) noexcept {
    for (const auto& e : kKeywordTable)
        if (e.text == s) return e.id;
    return Keyword::None;
}

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------
enum class TokenKind : std::uint8_t {
    LParen,       // (
    RParen,       // )
    Numeral,      // 0 | [1-9][0-9]*
    Decimal,      // <numeral> . 0* <numeral>
    Hexadecimal,  // #x[0-9a-fA-F]+
    Binary,       // #b[01]+
    String,       // "..." with "" as the only escape
    Symbol,       // simple symbol or |quoted symbol| (never a reserved word)
    Keyword,      // : <simple_symbol>
    Reserved,     // see Token::reserved
    Eof,
    Error,        // malformed input; lexer reports the detail separately
};

struct SourceLoc {
    std::uint32_t offset = 0;  // byte offset into the input buffer
    std::uint32_t line = 1;    // 1-based
    std::uint32_t column = 1;  // 1-based, counted in bytes (input may be UTF-8)
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    Reserved reserved = Reserved::None;  // set only when kind == Reserved
    bool quoted = false;                 // set only when kind == Symbol, written |...|
    SourceLoc loc{};
    std::string_view text{};  // raw lexeme, a view into the input buffer,
                              // including delimiters (|, ", #x, #b, :)

    // The lexeme without its delimiters. Per Sec. 3.1, |abc| and abc are the
    // same symbol, so compare symbols by payload(), never by text.
    // String payloads still contain "" pairs; unescape on demand in a later layer.
    constexpr std::string_view payload() const noexcept {
        switch (kind) {
        case TokenKind::Symbol:
            return quoted ? text.substr(1, text.size() - 2) : text;
        case TokenKind::String:
            return text.substr(1, text.size() - 2);
        case TokenKind::Hexadecimal:
        case TokenKind::Binary:
            return text.substr(2);
        case TokenKind::Keyword:
            return text.substr(1);
        default:
            return text;
        }
    }

    constexpr bool is(TokenKind k) const noexcept { return kind == k; }
    constexpr bool is(Reserved r) const noexcept {
        return kind == TokenKind::Reserved && reserved == r;
    }

    // Predefined keyword id, or Keyword::None for non-keyword tokens and for
    // solver-specific keywords. Matches on payload(), so the ':' is not part
    // of the name.
    constexpr Keyword keyword() const noexcept {
        return kind == TokenKind::Keyword ? lookupKeyword(payload()) : Keyword::None;
    }

    // Predefined symbol id. Matches on payload(), so |true| and true agree.
    constexpr Predefined predefined() const noexcept {
        return kind == TokenKind::Symbol ? lookupPredefined(payload()) : Predefined::None;
    }

    constexpr bool is(Keyword k) const noexcept {
        return k != Keyword::None && keyword() == k;
    }
    constexpr bool is(Predefined p) const noexcept {
        return p != Predefined::None && predefined() == p;
    }
};

}
}