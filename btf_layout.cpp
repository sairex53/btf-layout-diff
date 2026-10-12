#include "btf_layout.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace btf_layout {
namespace {

std::string_view trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                            value.front() == '\r' || value.front() == '\n')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                            value.back() == '\r' || value.back() == '\n')) {
    value.remove_suffix(1);
  }
  return value;
}

std::optional<std::uint64_t> attribute(std::string_view text, std::string_view key) {
  std::size_t position = 0;
  while ((position = text.find(key, position)) != std::string_view::npos) {
    const bool left_boundary = position == 0 || text[position - 1] == ' ' || text[position - 1] == '\t';
    const auto value_start = position + key.size();
    const bool right_boundary = value_start < text.size() && text[value_start] >= '0' && text[value_start] <= '9';
    if (left_boundary && right_boundary) {
      std::uint64_t value{};
      const char* begin = text.data() + value_start;
      const char* end = text.data() + text.size();
      const auto result = std::from_chars(begin, end, value);
      if (result.ec != std::errc{}) {
        throw std::runtime_error("invalid numeric attribute " + std::string(key));
      }
      return value;
    }
    position = value_start;
  }
  return std::nullopt;
}

std::string quoted_name(std::string_view text, std::size_t start, std::size_t& after_quote) {
  const auto first = text.find('\'', start);
  if (first == std::string_view::npos) {
    throw std::runtime_error("missing quoted name in bpftool raw dump");
  }
  const auto last = text.find('\'', first + 1);
  if (last == std::string_view::npos) {
    throw std::runtime_error("unterminated quoted name in bpftool raw dump");
  }
  after_quote = last + 1;
  return std::string(text.substr(first + 1, last - first - 1));
}

struct PendingAggregate {
  Aggregate value;
  std::uint64_t expected_members{};
  std::size_t header_line{};
};

void finish_pending(std::optional<PendingAggregate>& pending, Types& result, std::size_t line_number) {
  if (pending && pending->value.members.size() != pending->expected_members) {
    std::ostringstream message;
    message << "line " << pending->header_line << ": type '" << pending->value.name
            << "' declares " << pending->expected_members << " members but only "
            << pending->value.members.size() << " were found before line " << line_number;
    throw std::runtime_error(message.str());
  }
  if (pending) result.push_back(std::move(pending->value));
  pending.reset();
}

void require_attr(std::string_view text, std::string_view key) {
  if (!attribute(text, key)) {
    throw std::runtime_error("missing or invalid " + std::string(key) + " attribute");
  }
}

using MemberMap = std::map<std::string, Member>;

MemberMap member_map(const Aggregate& aggregate) {
  MemberMap result;
  for (const auto& member : aggregate.members) {
    result.emplace(member.name, member);
  }
  return result;
}

bool same_layout(const Aggregate& left, const Aggregate& right) {
  if (left.kind != right.kind || left.size_bytes != right.size_bytes) return false;
  const auto lhs = member_map(left);
  const auto rhs = member_map(right);
  if (lhs.size() != left.members.size() || rhs.size() != right.members.size() || lhs.size() != rhs.size()) {
    return false;
  }
  for (const auto& [name, member] : lhs) {
    const auto found = rhs.find(name);
    if (found == rhs.end() || member.bit_offset != found->second.bit_offset ||
        member.bit_size != found->second.bit_size) {
      return false;
    }
  }
  return true;
}

std::string offset_text(const Member& member) {
  std::ostringstream out;
  out << member.bit_offset << " bits";
  if (member.bit_size) out << ", width " << *member.bit_size << " bits";
  return out.str();
}

void print_type_delta(const Aggregate& before, const Aggregate& after, std::ostream& output) {
  if (before.size_bytes != after.size_bytes) {
    output << "  size: " << before.size_bytes << " -> " << after.size_bytes << " bytes\n";
  }

  const auto lhs = member_map(before);
  const auto rhs = member_map(after);
  for (const auto& [name, old_member] : lhs) {
    const auto found = rhs.find(name);
    if (found == rhs.end()) {
      output << "  - " << name << " (was at " << offset_text(old_member) << ")\n";
      continue;
    }
    const auto& new_member = found->second;
    if (old_member.bit_offset != new_member.bit_offset || old_member.bit_size != new_member.bit_size) {
      output << "  ~ " << name << ": " << offset_text(old_member) << " -> " << offset_text(new_member) << '\n';
    }
  }
  for (const auto& [name, new_member] : rhs) {
    if (!lhs.contains(name)) {
      output << "  + " << name << " at " << offset_text(new_member) << '\n';
    }
  }
}

}  // namespace

Types parse(std::istream& input) {
  Types result;
  std::map<std::string, std::size_t> names;
  std::optional<PendingAggregate> pending;
  std::string line;
  std::size_t line_number = 0;

  while (std::getline(input, line)) {
    ++line_number;
    const auto content = trim(line);
    if (content.empty()) continue;

    if (content.front() == '[') {
      finish_pending(pending, result, line_number);

      const auto close = content.find("] ");
      if (close == std::string_view::npos) {
        throw std::runtime_error("line " + std::to_string(line_number) + ": malformed type header");
      }
      auto rest = content.substr(close + 2);
      const auto kind_end = rest.find(' ');
      if (kind_end == std::string_view::npos) continue;
      const auto kind = rest.substr(0, kind_end);
      if (kind != "STRUCT" && kind != "UNION") continue;

      std::size_t after_quote = 0;
      auto name = quoted_name(rest, kind_end, after_quote);
      const auto attrs = rest.substr(after_quote);
      const auto size = attribute(attrs, "size=");
      const auto vlen = attribute(attrs, "vlen=");
      if (!size || !vlen) {
        throw std::runtime_error("line " + std::to_string(line_number) +
                                 ": aggregate header needs size= and vlen=");
      }
      if (name.empty() || name == "(anon)") continue;
      if (++names[name] > 1) {
        throw std::runtime_error("line " + std::to_string(line_number) + ": duplicate named type '" + name +
                                 "'; choose a BTF dump with unambiguous type names");
      }
      pending = PendingAggregate{Aggregate{std::string(kind), std::move(name), *size, {}}, *vlen, line_number};
      continue;
    }

    if (pending) {
      if (content.front() != '\'') {
        throw std::runtime_error("line " + std::to_string(line_number) +
                                 ": expected a member row inside aggregate '" + pending->value.name + "'");
      }
      std::size_t after_quote = 0;
      auto name = quoted_name(content, 0, after_quote);
      const auto attrs = content.substr(after_quote);
      require_attr(attrs, "type_id=");
      const auto offset = attribute(attrs, "bits_offset=");
      if (!offset) {
        throw std::runtime_error("line " + std::to_string(line_number) + ": member row needs bits_offset=");
      }
      const auto bit_size = attribute(attrs, "bitfield_size=");
      pending->value.members.push_back(Member{std::move(name), *offset, bit_size});
    }
  }

  finish_pending(pending, result, line_number + 1);
  return result;
}

std::vector<TypeChange> compare(const Types& before, const Types& after,
                                const std::optional<std::string>& only_type) {
  std::map<std::string, Aggregate> lhs;
  std::map<std::string, Aggregate> rhs;
  for (const auto& type : before) lhs.emplace(type.name, type);
  for (const auto& type : after) rhs.emplace(type.name, type);

  std::vector<TypeChange> changes;
  auto left = lhs.begin();
  auto right = rhs.begin();
  while (left != lhs.end() || right != rhs.end()) {
    if (right == rhs.end() || (left != lhs.end() && left->first < right->first)) {
      if (!only_type || *only_type == left->first) {
        changes.push_back(TypeChange{TypeChange::Kind::removed, left->first, left->second, std::nullopt});
      }
      ++left;
    } else if (left == lhs.end() || right->first < left->first) {
      if (!only_type || *only_type == right->first) {
        changes.push_back(TypeChange{TypeChange::Kind::added, right->first, std::nullopt, right->second});
      }
      ++right;
    } else {
      if ((!only_type || *only_type == left->first) && !same_layout(left->second, right->second)) {
        changes.push_back(TypeChange{TypeChange::Kind::changed, left->first, left->second, right->second});
      }
      ++left;
      ++right;
    }
  }
  return changes;
}

void print_report(const std::vector<TypeChange>& changes, std::ostream& output) {
  if (changes.empty()) {
    output << "No layout changes found for the selected named STRUCT/UNION types.\n";
    return;
  }

  for (const auto& change : changes) {
    if (change.kind == TypeChange::Kind::added) {
      output << "+ " << change.after->kind << ' ' << change.name << " (" << change.after->size_bytes << " bytes)\n";
    } else if (change.kind == TypeChange::Kind::removed) {
      output << "- " << change.before->kind << ' ' << change.name << " (" << change.before->size_bytes << " bytes)\n";
    } else {
      output << "~ " << change.after->kind << ' ' << change.name << '\n';
      print_type_delta(*change.before, *change.after, output);
    }
  }
}

}  // namespace btf_layout
