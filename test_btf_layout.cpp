#include "btf_layout.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

btf_layout::Types parse_text(const std::string& text) {
  std::istringstream input(text);
  return btf_layout::parse(input);
}

void parses_named_aggregates_and_ignores_type_ids() {
  const auto old_dump = parse_text(
      "[1] INT 'unsigned int' size=4 bits_offset=0 nr_bits=32 encoding=(none)\n"
      "[2] STRUCT 'task_struct' size=16 vlen=2\n"
      "\t'pid' type_id=1 bits_offset=0\n"
      "\t'flags' type_id=1 bits_offset=32\n");
  const auto new_dump = parse_text(
      "[91] INT 'unsigned int' size=4 bits_offset=0 nr_bits=32 encoding=(none)\n"
      "[92] STRUCT 'task_struct' size=16 vlen=2\n"
      "\t'pid' type_id=91 bits_offset=0\n"
      "\t'flags' type_id=91 bits_offset=32\n");
  check(old_dump.size() == 1, "expected one named aggregate");
  check(btf_layout::compare(old_dump, new_dump).empty(), "type ID changes must not imply layout changes");
}

void detects_layout_changes() {
  const auto before = parse_text(
      "[1] STRUCT 'sample' size=8 vlen=2\n"
      "\t'a' type_id=2 bits_offset=0\n"
      "\t'count' type_id=3 bits_offset=32 bitfield_size=3\n");
  const auto after = parse_text(
      "[10] STRUCT 'sample' size=16 vlen=3\n"
      "\t'a' type_id=20 bits_offset=0\n"
      "\t'count' type_id=30 bits_offset=40 bitfield_size=4\n"
      "\t'next' type_id=40 bits_offset=64\n");
  const auto changes = btf_layout::compare(before, after);
  check(changes.size() == 1, "expected one changed type");
  std::ostringstream report;
  btf_layout::print_report(changes, report);
  const auto text = report.str();
  check(text.find("size: 8 -> 16 bytes") != std::string::npos, "size delta missing");
  check(text.find("count: 32 bits, width 3 bits -> 40 bits, width 4 bits") != std::string::npos,
        "bitfield delta missing");
  check(text.find("+ next at 64 bits") != std::string::npos, "new member missing");
}

void rejects_truncated_aggregate() {
  bool failed = false;
  try {
    (void)parse_text("[1] STRUCT 'sample' size=8 vlen=1\n");
  } catch (const std::runtime_error&) {
    failed = true;
  }
  check(failed, "truncated aggregate should fail rather than silently compare");
}

void rejects_duplicate_type_names() {
  bool failed = false;
  try {
    (void)parse_text("[1] STRUCT 'sample' size=0 vlen=0\n[2] STRUCT 'sample' size=4 vlen=0\n");
  } catch (const std::runtime_error&) {
    failed = true;
  }
  check(failed, "duplicate named types should be reported as ambiguous");
}

}  // namespace

int main() {
  try {
    parses_named_aggregates_and_ignores_type_ids();
    detects_layout_changes();
    rejects_truncated_aggregate();
    rejects_duplicate_type_names();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "All btf-layout-diff tests passed.\n";
}
