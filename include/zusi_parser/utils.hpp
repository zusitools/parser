#ifndef ZUSI_PARSER_UTILS_HPP_
#define ZUSI_PARSER_UTILS_HPP_

#include <algorithm>
#include <cassert>
#include <chrono>
#include <exception>
#include <ios>
#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <winerror.h>
#else
#  include <cerrno>
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#include "zusi_parser/zusi_types.hpp"
#include "zusi_parser/zusi_parser.hpp"

#define MMAP_THRESHOLD_BYTES 1000000000

namespace zusixml {

static std::unique_ptr<Zusi> parse(std::string_view dateiname) {
#ifdef _WIN32
  // TODO mmap
  std::basic_ifstream<std::remove_const_t<rapidxml::Ch>> stream;
  stream.exceptions(std::ifstream::failbit | std::ifstream::eofbit | std::ifstream::badbit);
  try {
    stream.open(std::string(dateiname), std::ios::binary);
  } catch (const std::ifstream::failure& e) {
    throw std::runtime_error(std::string(dateiname) + ": open() failed: " + e.what());
  }
  try {
    stream.seekg(0, std::ios::end);
  } catch (const std::ifstream::failure& e) {
    throw std::runtime_error(std::string(dateiname) + ": seek() failed: " + e.what());
  }
  size_t size = stream.tellg();
  std::vector<std::remove_const_t<rapidxml::Ch>> buffer(size + 1, 0);
  try {
    stream.seekg(0);
    stream.read(buffer.data(), size);
  } catch (const std::ifstream::failure& e) {
    throw std::runtime_error(std::string(dateiname) + ": read() failed: " + e.what());
  }
  try {
    return rapidxml::parse<Zusi>(buffer.data());
  } catch (const rapidxml::parse_error& e) {
    std::cerr << "Error parsing " << dateiname << ": " << e.what() << " at char " << (e.where() - buffer.data()) << "\n";
    return nullptr;
  }
#else
  struct FdHelper {
    ~FdHelper() {
      if (fd != -1) { close(fd); }
    }
    int fd = -1;
  } fdHelper;

  fdHelper.fd = open(std::string(dateiname).c_str(), O_RDONLY);
  if (fdHelper.fd == -1) {
    throw std::runtime_error(std::string(dateiname) + ": open() failed: " + std::strerror(errno));
  }

  struct stat sb;
  if (fstat(fdHelper.fd, &sb) == -1) {
    throw std::runtime_error(std::string(dateiname) + ": fstat() failed: " + std::strerror(errno));
  }

  if (!S_ISREG(sb.st_mode)) {
    throw std::runtime_error(std::string(dateiname) + ": not a file");
  }

  std::unique_ptr<Zusi> result;

  if (sb.st_size > MMAP_THRESHOLD_BYTES && sb.st_size % getpagesize() != 0) {
    struct MmapHelper {
      ~MmapHelper() {
        if (buffer != MAP_FAILED) {
          munmap(const_cast<void*>(reinterpret_cast<const void*>(buffer)), size);
        }
      }
      void* buffer = MAP_FAILED;
      off_t size;
    } mmapHelper;

    mmapHelper.size = sb.st_size;
    mmapHelper.buffer = mmap(
      nullptr,          // addr: kernel chooses mapping address
      sb.st_size,       // length
      PROT_READ,        // prot
      MAP_SHARED,       // flags
      fdHelper.fd,      // fd
      0                 // offset in file
    );
    if (mmapHelper.buffer == MAP_FAILED) {
      throw std::runtime_error(std::string(dateiname) + ": mmap() failed: " + std::strerror(errno));
    }

    try {
      result = rapidxml::parse<Zusi>(static_cast<rapidxml::Ch*>(mmapHelper.buffer));
    } catch (const rapidxml::parse_error& e) {
      std::cerr << "Error parsing " << dateiname << ": " << e.what() << " at char " << (e.where() - static_cast<rapidxml::Ch*>(mmapHelper.buffer)) << "\n";
    }

    int munmap_result = munmap(mmapHelper.buffer, mmapHelper.size);
    mmapHelper.buffer = MAP_FAILED;
    if (munmap_result == -1) {
      throw std::runtime_error(std::string(dateiname) + ": munmap() failed: " + std::strerror(errno));
    }

  } else {
    std::vector<std::remove_const_t<rapidxml::Ch>> buffer(sb.st_size + 1, 0);
    read(fdHelper.fd, buffer.data(), sb.st_size);
    try {
      result = rapidxml::parse<Zusi>(buffer.data());
    } catch (const rapidxml::parse_error& e) {
      std::cerr << "Error parsing " << dateiname << ": " << e.what() << " at char " << (e.where() - buffer.data()) << "\n";
    }
  }

  int close_result = close(fdHelper.fd);
  fdHelper.fd = -1;
  if (close_result == -1) {
    throw std::runtime_error(std::string(dateiname) + ": close() failed: " + std::strerror(errno));
  }

  return result;
#endif  // ifdef _WIN32
}

static std::string bestimmeZusiDatenpfad() {
#ifdef _WIN32
  HKEY key;
  char buffer[MAX_PATH];
  DWORD len = MAX_PATH;
  std::string result;
  if (SUCCEEDED(RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Zusi3", 0, KEY_READ | KEY_WOW64_32KEY, &key)) &&
      SUCCEEDED(RegGetValueA(key, nullptr, "DatenVerzeichnis", RRF_RT_REG_SZ, nullptr, (LPBYTE)buffer, &len))) {
    result = std::string(buffer, len - 1);  // buffer ist nullterminiert
    RegCloseKey(key);
  }
  return result;
#else
  return std::string(getenv("ZUSI3_DATAPATH"));
#endif
}

static const std::string zusiDatenpfad = bestimmeZusiDatenpfad();

const std::string& getZusiDatenpfad() {
  return zusiDatenpfad;
}

/**
 * Konvertiert den angegebenen Zusi-Pfad in einen Betriebssystempfad.
 * Wenn @p zusiPfad nur aus einem Dateinamen besteht, wird er als relativ zu
 * @p zusiPfadUebergeordnet betrachtet. Falls @p osPfadUebergeordnet der Name eines
 * eines Verzeichnisses ist, muss es mit einem Backslash (Windows) bzw. Slash (Unix) enden.
 */
std::string zusiPfadZuOsPfad(std::string_view zusiPfad, std::string_view osPfadUebergeordnet) {
  constexpr char zusiSep = '\\';
#ifdef _WIN32
  constexpr char osSep = '\\';
#else
  constexpr char osSep = '/';
#endif

  std::string result;
  if (zusiPfad.find(zusiSep) == std::string::npos) {
    // Relativ zu uebergeordnetem Pfad
    result = osPfadUebergeordnet.substr(0, osPfadUebergeordnet.rfind(osSep));
  } else {
    // Relativ zum Zusi-Datenverzeichnis
    result = getZusiDatenpfad();
  }

  if (result.back() == osSep && zusiPfad.front() == zusiSep) {
    result.pop_back();
  } else if (result.back() != osSep && zusiPfad.front() != zusiSep) {
    result.push_back(osSep);
  }

  if constexpr (osSep != zusiSep) {
    std::replace_copy(zusiPfad.begin(), zusiPfad.end(), std::back_inserter(result), zusiSep, osSep);
    return result;
  } else {
    return result += zusiPfad;
  }
}

}  // namespace zusixml

#endif  // ZUSI_PARSER_UTILS_HPP_
