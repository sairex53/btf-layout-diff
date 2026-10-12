#pragma once

#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace btf_layout {

struct Member {
  std::string name;
  std::uint64_t bit_offset{};
  std::optional<std::uint64_t> bit_size;
};

struct Aggregate {
  std::string kind;
  std::string name;
  std::uint64_t size_bytes{};
  std::vector<Member> members;
};

struct TypeChange {
  enum class Kind { added, removed, changed };

  Kind kind;
  std::string name;
  std::optional<Aggregate> before;
  std::optional<Aggregate> after;
};

using Types = std::vector<Aggregate>;

// Reads the human-readable output of `bpftool btf dump ... format raw`.
// This intentionally keeps only named STRUCT/UNION layouts. BTF type IDs are
// parsed for format validation but are not used as cross-kernel identities.
Types parse(std::istream& input);

std::vector<TypeChange> compare(const Types& before, const Types& after,
                                const std::optional<std::string>& only_type = std::nullopt);

void print_report(const std::vector<TypeChange>& changes, std::ostream& output);

}  // namespace btf_layout
