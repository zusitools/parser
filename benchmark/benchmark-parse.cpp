#include <cassert>
#include <chrono>
#include <ios>
#include <iostream>
#include <fstream>
#include <vector>

#include "zusi_parser/zusi_types.hpp"
#include "zusi_parser/zusi_parser.hpp"
#include "zusi_parser/utils.hpp"

#ifdef __linux__
#include <sys/resource.h>
#endif

int main(int argc, char** argv) {
#ifdef NDEBUG
  (void)argc;
#endif
  assert(argc == 2);
  auto start = std::chrono::high_resolution_clock::now();

  // argv[1]: Liste von Dateinamen
  std::vector<std::string> dateinamen;
  std::vector<zusixml::FileReader> dateien;
  size_t total_size = 0;

#ifdef __linux__
  rlimit rlim;
  getrlimit(RLIMIT_NOFILE, &rlim);
  if (rlim.rlim_cur < rlim.rlim_max) {
    std::cout << "Setting RLIMIT_NOFILE from " << rlim.rlim_cur << " to " << rlim.rlim_max << "\n";
    rlim.rlim_cur = rlim.rlim_max;
    setrlimit(RLIMIT_NOFILE, &rlim);
  }

  getrlimit(RLIMIT_AS, &rlim);
  if (rlim.rlim_cur < rlim.rlim_max) {
    std::cout << "Setting RLIMIT_AS from " << rlim.rlim_cur << " to " << rlim.rlim_max << "\n";
    rlim.rlim_cur = rlim.rlim_max;
    setrlimit(RLIMIT_AS, &rlim);
  }
#endif

  {
    char buf[256];
    std::ifstream i(argv[1]);
    while (i.getline(buf, sizeof(buf))) {
      dateinamen.push_back(buf);
    }
  }

#ifdef __linux__
  {
    char buf[256];
    std::ifstream i("/proc/sys/vm/max_map_count");
    i.getline(buf, sizeof(buf));
    if (atoi(buf) < dateinamen.size() + 50) {
      std::cerr << "Mapping files into memory might fail, consider running sudo sysctl -w vm.max_map_count=" << (dateinamen.size() + 50) << "\n";
    }
  }
#endif

  for (const auto& dateiname : dateinamen) {
    const auto& fileReader = dateien.emplace_back(std::string_view(dateiname));
    total_size += fileReader.size();
  }

  auto ende_laden = std::chrono::high_resolution_clock::now();

  std::vector<std::unique_ptr<Zusi>> results;
  for (size_t i = 0; i < dateien.size(); i++) {
    try {
      results.push_back(zusixml::parse_root<Zusi>(dateien[i].data()));
    } catch (zusixml::parse_error& e) {
      std::cerr << dateinamen[i] << ": " << e.what() << " @ char " << (e.where() - dateien[i].data()) << std::endl;
    }
  }
  auto ende_parsen = std::chrono::high_resolution_clock::now();
  std::cout << "Parsed " << results.size() << " files (" << total_size << " bytes)" << std::endl;  // use results variable

  std::cout << " - load: " << std::chrono::duration_cast<std::chrono::milliseconds>(ende_laden - start).count() << " ms " << std::endl;
  std::cout << " - parse: " << std::chrono::duration_cast<std::chrono::milliseconds>(ende_parsen - ende_laden).count() << " ms " << std::endl;

  _exit(0);  // do not call destructors -- their time must not be taken into account when benchmarking
}
