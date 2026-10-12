#include "btf_layout.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void usage(std::ostream& out) {
  out << "Usage: btf-layout-diff OLD.raw NEW.raw [--type TYPE]\n"
         "Compare named STRUCT/UNION layouts in bpftool raw BTF dumps.\n"
         "Create dumps with: bpftool btf dump file /sys/kernel/btf/vmlinux format raw\n";
}

std::ifstream open_input(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open '" + path + "'");
  return input;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
    usage(std::cout);
    return 0;
  }
  if (argc < 3 || argc > 5) {
    usage(std::cerr);
    return 2;
  }

  std::optional<std::string> only_type;
  if (argc == 4) {
    usage(std::cerr);
    return 2;
  }
  if (argc == 5) {
    if (std::string(argv[3]) != "--type") {
      usage(std::cerr);
      return 2;
    }
    only_type = argv[4];
  }

  try {
    auto old_input = open_input(argv[1]);
    auto new_input = open_input(argv[2]);
    const auto old_types = btf_layout::parse(old_input);
    const auto new_types = btf_layout::parse(new_input);
    const auto changes = btf_layout::compare(old_types, new_types, only_type);
    if (only_type) {
      const auto has_old = std::any_of(old_types.begin(), old_types.end(), [&](const auto& type) {
        return type.name == *only_type;
      });
      const auto has_new = std::any_of(new_types.begin(), new_types.end(), [&](const auto& type) {
        return type.name == *only_type;
      });
      if (!has_old && !has_new) throw std::runtime_error("type '" + *only_type + "' was not found in either dump");
    }
    btf_layout::print_report(changes, std::cout);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
