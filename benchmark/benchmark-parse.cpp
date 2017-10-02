#include <cassert>
#include <chrono>
#include <ios>
#include <iostream>
#include <fstream>
#include <vector>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "zusi_types.hpp"
#include "rapidxml.hpp"
#include "zusi_parser.hpp"

#ifndef MMAP_THRESHOLD_BYTES
#  ifdef USE_MMAP
#    define MMAP_THRESHOLD_BYTES 0
#  else
#    define MMAP_THRESHOLD_BYTES 1000000000
#  endif
#endif

int main(int argc, char** argv) {
  assert(argc == 2);
  auto start = std::chrono::high_resolution_clock::now();

  // argv[1]: Liste von Dateinamen
  std::vector<std::string> dateinamen;
  using Ch = char;

  std::vector<Ch*> dateien;
  std::vector<size_t> dateigroessen;

  size_t total_size = 0;
  char buf[256];
  std::ifstream i(argv[1]);
  while (i.getline(buf, 256)) {
    int fd = open (buf, O_RDONLY);
    if (fd == -1) {
      perror ("open");
      return 1;
    }

    struct stat sb;
    if (fstat (fd, &sb) == -1) {
      perror ("fstat");
      return 1;
    }

    if (!S_ISREG (sb.st_mode)) {
      fprintf (stderr, "%s is not a file\n", argv[1]);
      return 1;
    }

    if (sb.st_size > MMAP_THRESHOLD_BYTES) {
      Ch* p = static_cast<Ch*>(mmap(
        nullptr,          // addr: kernel chooses mapping address
        sb.st_size,       // length
        PROT_READ,        // prot
        MAP_SHARED,       // flags
        fd,               // fd
        0                 // offset in file
        ));

      if (p == MAP_FAILED) {
        perror ("mmap");
        return 1;
      }

      dateien.push_back(p);
    } else {
      char* buffer = reinterpret_cast<char*>(malloc(sb.st_size + 1));
      read(fd, buffer, sb.st_size);
      buffer[sb.st_size] = '\0';
      dateien.push_back(std::move(buffer));
    }

    dateigroessen.push_back(sb.st_size);
    total_size += sb.st_size;

    if (close (fd) == -1) {
      perror ("close");
      return 1;
    }

    dateinamen.push_back(buf);
  }

  auto ende_laden = std::chrono::high_resolution_clock::now();

  std::vector<std::unique_ptr<Zusi>> results;
  for (size_t i = 0; i < dateien.size(); i++) {
    try {
      results.push_back(rapidxml::parse<Zusi>(&dateien[i][0]));
    } catch (rapidxml::parse_error& e) {
      std::cerr << dateinamen[i] << ": " << e.what() << " @ char " << (e.where() - &dateien[i][0]) << std::endl;
    }
  }
  auto ende_parsen = std::chrono::high_resolution_clock::now();
  std::cout << "Parsed " << results.size() << " files (" << total_size << " bytes)" << std::endl;  // use results variable

  std::cout << " - load: " << std::chrono::duration_cast<std::chrono::milliseconds>(ende_laden - start).count() << " ms " << std::endl;
  std::cout << " - parse: " << std::chrono::duration_cast<std::chrono::milliseconds>(ende_parsen - ende_laden).count() << " ms " << std::endl;

  _exit(0);  // do not call destructors -- their time must not be taken into account when benchmarking
}
