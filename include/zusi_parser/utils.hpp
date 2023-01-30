#ifndef ZUSI_PARSER_UTILS_HPP_
#define ZUSI_PARSER_UTILS_HPP_

#include <algorithm>
#include <cassert>
#include <chrono>
#include <exception>
#include <ios>

#ifdef _WIN32
#  include <boost/nowide/iostream.hpp>
#  include <boost/nowide/fstream.hpp>
#else
#  include <iostream>
#  include <fstream>
#endif

#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <winerror.h>
#  include <boost/nowide/convert.hpp>
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

#define MMAP_THRESHOLD_BYTES 0

#ifdef _WIN32
namespace io = boost::nowide;
#else
namespace io = std;
#endif

#ifdef ZUSI_PARSER_USE_BOOST_FILESYSTEM
  #include <boost/filesystem.hpp>
  namespace fs = boost::filesystem;
#else
  #include <filesystem>
  namespace fs = std::filesystem;
#endif

namespace zusixml {

class FileReader {
 public:
  FileReader(std::string_view dateiname) {
#ifdef _WIN32
    // TODO mmap
    io::basic_ifstream<std::remove_const_t<zusixml::Ch>> stream;
    stream.exceptions(io::ifstream::failbit | io::ifstream::eofbit | io::ifstream::badbit);
    try {
      stream.open(std::string(dateiname), std::ios::binary);
    } catch (const io::ifstream::failure& e) {
      throw std::runtime_error(std::string(dateiname) + ": open() failed: " + e.what());
    }
    try {
      stream.seekg(0, std::ios::end);
    } catch (const io::ifstream::failure& e) {
      throw std::runtime_error(std::string(dateiname) + ": seek() failed: " + e.what());
    }
    size_t size = stream.tellg();
    m_buffer = std::vector<std::remove_const_t<zusixml::Ch>>(size + 1, 0);
    try {
      stream.seekg(0);
      stream.read(m_buffer.data(), size);
    } catch (const io::ifstream::failure& e) {
      throw std::runtime_error(std::string(dateiname) + ": read() failed: " + e.what());
    }
#else
    struct FdHelper {
      FdHelper() = default;
      FdHelper(const FdHelper&) = delete;
      FdHelper& operator=(const FdHelper&) = delete;
      ~FdHelper() {
        if (fd != -1) { close(fd); }
      }
      int fd { -1 };
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

    if (sb.st_size > MMAP_THRESHOLD_BYTES && sb.st_size % getpagesize() != 0) {
      m_mmap = true;
      m_mapsize = sb.st_size;
      m_data = mmap(
        nullptr,          // addr: kernel chooses mapping address
        sb.st_size,       // length
        PROT_READ,        // prot
        MAP_SHARED,       // flags
        fdHelper.fd,      // fd
        0                 // offset in file
      );
      if (m_data == MAP_FAILED) {
        throw std::runtime_error(std::string(dateiname) + ": mmap() failed: " + std::strerror(errno));
      }
    } else {
      m_buffer = std::vector<std::remove_const_t<zusixml::Ch>>(sb.st_size + 1, 0);
      read(fdHelper.fd, m_buffer.data(), sb.st_size);
      m_data = m_buffer.data();
    }

    int close_result = close(fdHelper.fd);
    fdHelper.fd = -1;
    if (close_result == -1) {
      throw std::runtime_error(std::string(dateiname) + ": close() failed: " + std::strerror(errno));
    }
#endif
  }

  ~FileReader() {
#ifdef _WIN32
#else
    if (m_mmap) {
      munmap(const_cast<void*>(reinterpret_cast<const void*>(m_data)), m_mapsize);
    }
#endif
  }

  FileReader(const FileReader&) = delete;
  FileReader& operator=(const FileReader&) = delete;

#ifdef _WIN32
  FileReader(FileReader&&) = default;
#else
  FileReader(FileReader&& other) {
    m_data = other.m_data;
    m_mapsize = other.m_mapsize;
    m_mmap = other.m_mmap;
    m_buffer = std::move(other.m_buffer);

    other.m_data = MAP_FAILED;
    other.m_mapsize = 0;
    other.m_mmap = false;
  }
#endif
  FileReader& operator=(FileReader&&) = delete;

  const zusixml::Ch* data() {
#ifdef _WIN32
    return m_buffer.data();
#else
    return static_cast<zusixml::Ch*>(m_data);
#endif
  }

  size_t size() const {
#ifdef _WIN32
    return m_buffer.size();
#else
    return m_mmap ? m_mapsize : m_buffer.size();
#endif
  }

 private:
#ifdef _WIN32
#else
  void* m_data { MAP_FAILED };
  off_t m_mapsize { 0 };
  bool m_mmap { false };
#endif
  std::vector<std::remove_const_t<zusixml::Ch>> m_buffer;
};

static inline std::unique_ptr<Zusi> parseFile(std::string_view dateiname) {
  try {
    FileReader reader(dateiname);
    try {
      return zusixml::parse_root<Zusi>(reader.data());
    } catch (const zusixml::parse_error& e) {
      io::cerr << "Error parsing " << dateiname << ": " << e.what() << " at char " << (e.where() - reader.data()) << "\n";
    }
  } catch (const std::exception& e) {
    io::cerr << "Error reading " << dateiname << ": " << e.what() << "\n";
  }
  return nullptr;
}

static inline std::unique_ptr<Zusi> tryParseFile(std::string_view dateiname) {
  try {
    return parseFile(dateiname);
  } catch (const std::runtime_error& e) {
    io::cerr << e.what() << "\n";
    return nullptr;
  }
}

static inline std::string bestimmeZusiDatenpfad() {
  std::string result;
#ifdef _WIN32
  HKEY key;
  char buffer[MAX_PATH];
  DWORD len = MAX_PATH;
  if (((RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Zusi3", 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) ||
      (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Zusi3", 0, KEY_READ | KEY_WOW64_32KEY, &key) == ERROR_SUCCESS)) &&
       (RegGetValueA(key, nullptr, "DatenVerzeichnis", RRF_RT_REG_SZ, nullptr, (LPBYTE)buffer, &len) == ERROR_SUCCESS ||
        RegGetValueA(key, nullptr, "DatenVerzeichnisSteam", RRF_RT_REG_SZ, nullptr, (LPBYTE)buffer, &len) == ERROR_SUCCESS)) {
    result = std::string(buffer, len - 1);  // buffer ist nullterminiert
    RegCloseKey(key);
  }
#else
  const char* const datapath = getenv("ZUSI3_DATAPATH");
  if (datapath != nullptr) {
    result = std::string(datapath);
  }
#endif
  return result;
}

static inline std::string bestimmeZusiDatenpfadOffiziell() {
  std::string result;
#ifdef _WIN32
  HKEY key;
  char buffer[MAX_PATH];
  DWORD len = MAX_PATH;
  if (((RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Zusi3", 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) ||
      (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Zusi3", 0, KEY_READ | KEY_WOW64_32KEY, &key) == ERROR_SUCCESS)) &&
       (RegGetValueA(key, nullptr, "DatenVerzeichnisOffiziell", RRF_RT_REG_SZ, nullptr, (LPBYTE)buffer, &len) == ERROR_SUCCESS ||
        RegGetValueA(key, nullptr, "DatenVerzeichnisOffiziellSteam", RRF_RT_REG_SZ, nullptr, (LPBYTE)buffer, &len) == ERROR_SUCCESS)) {
    result = std::string(buffer, len - 1);  // buffer ist nullterminiert
    RegCloseKey(key);
  }
#else
  const char* datapath = getenv("ZUSI3_DATAPATH_OFFICIAL");
  if (datapath == nullptr) {
    datapath = getenv("ZUSI3_DATAPATH");
  }
  if (datapath != nullptr) {
    result = std::string(datapath);
  }
#endif
  return result;
}

inline const std::string& getZusiDatenpfad() {
  static const std::string zusiDatenpfad = bestimmeZusiDatenpfad();
  return zusiDatenpfad;
}

inline const std::string& getZusiDatenpfadOffiziell() {
  static const std::string zusiDatenpfadOffiziell = bestimmeZusiDatenpfadOffiziell();
  return zusiDatenpfadOffiziell;
}

constexpr char zusiSep = '\\';
#ifdef _WIN32
constexpr char osSep = '\\';
#else
constexpr char osSep = '/';
#endif

class ZusiPfad {
public:
  ZusiPfad(const ZusiPfad&) = default;
  ZusiPfad& operator=(const ZusiPfad&) = default;
  ZusiPfad(ZusiPfad&&) = default;
  ZusiPfad& operator=(ZusiPfad&&) = default;

  static ZusiPfad vonZusiPfad(std::string_view zusiPfad, const ZusiPfad& uebergeordnet) {
    if (zusiPfad.empty() || (zusiPfad.find('\\') != std::string_view::npos)) {
      return vonZusiPfad(zusiPfad);
    }

    const auto lastBackslashPos = uebergeordnet.m_pfad.find_last_of('\\');
    if (lastBackslashPos == std::string::npos) {
      return ZusiPfad(std::string(zusiPfad));
    } else {
      std::string pfad = uebergeordnet.m_pfad.substr(0, lastBackslashPos + 1);
      pfad += zusiPfad;
      return ZusiPfad(std::move(pfad));
    }
  }

  static ZusiPfad vonZusiPfad(std::string_view zusiPfad) {
    if (zusiPfad.size() >= 1 && zusiPfad.front() == '\\') {
      return ZusiPfad(std::string(zusiPfad.substr(1)));
    } else {
      return ZusiPfad(std::string(zusiPfad));
    }
  }

  static ZusiPfad vonOsPfad(std::string_view osPfad) {
    std::string relPfad = [&osPfad] {
      // Optimierung fuer (haeufige) einfache Faelle, bei denen ein Stringvergleich genuegt
      const std::string& datenpfadOffiziell = getZusiDatenpfadOffiziell();
      if (std::mismatch(datenpfadOffiziell.begin(), datenpfadOffiziell.end(), osPfad.begin(), osPfad.end()).first == datenpfadOffiziell.end()) {
        return std::string(osPfad.substr(datenpfadOffiziell.size()));
      }

      const std::string& datenpfad = getZusiDatenpfad();
      if (std::mismatch(datenpfad.begin(), datenpfad.end(), osPfad.begin(), osPfad.end()).first == datenpfad.end()) {
        return std::string(osPfad.substr(datenpfad.size()));
      }

      // Allgemeiner Fall
      const auto& lexically_proximate = [](const fs::path& p, const fs::path& base) -> fs::path {
        // Taken and adapted from boost::filesystem, src/path.cpp
        // Licensed under the Boost Software License.
        auto mm = std::mismatch(p.begin(), p.end(), base.begin(), base.end()
#ifdef _WIN32
            , [](const fs::path& lhs, const fs::path& rhs) {
              return strcmpi(lhs.string().c_str(), rhs.string().c_str()) == 0;
            }
#endif
        );
        if ((mm.first == p.begin()) && (mm.second == base.begin())) {
          return p;
        }
        if ((mm.first == p.end()) && (mm.second == base.end())) {
          return ".";
        }
        fs::path tmp;
        while (mm.second != base.end()) {
          tmp /= "..";
          ++mm.second;
        }
        while (mm.first != p.end()) {
          tmp /= *mm.first;
          ++mm.first;
        }
        return tmp;
      };

      const auto& proximate = [&lexically_proximate](const fs::path& p, const fs::path& base) -> fs::path {
#ifdef ZUSI_PARSER_USE_BOOST_FILESYSTEM
        boost::system::error_code ec;
#else
        std::error_code ec;
#endif
        const auto& pCanonical = fs::weakly_canonical(p, ec);
        if (ec) {
          return p;
        }
        const auto& baseCanonical = fs::weakly_canonical(base, ec);
        if (ec) {
          return p;
        }
        return lexically_proximate(pCanonical, baseCanonical);
      };

      const auto relToOffiziell = proximate(
          fs::path(osPfad.begin(), osPfad.end()),
          fs::path(datenpfadOffiziell.begin(), (!datenpfadOffiziell.empty() && datenpfadOffiziell.back() == osSep) ? std::prev(datenpfadOffiziell.end()) : datenpfadOffiziell.end()));
      if (relToOffiziell.is_relative() && (relToOffiziell.empty() || *relToOffiziell.begin() != "..")) {
        return relToOffiziell.string();
      }

      return proximate(
          fs::path(osPfad.begin(), osPfad.end()),
          fs::path(datenpfad.begin(), (!datenpfad.empty() && datenpfad.back() == osSep) ? std::prev(datenpfad.end()) : datenpfad.end())).string();
    }();

    std::replace(relPfad.begin(), relPfad.end(), '/', '\\');
    if (relPfad.back() == '.') {
      relPfad.erase(relPfad.size() - 1);
    }
    return ZusiPfad(std::move(relPfad));
  }

  std::string_view alsZusiPfad() const {
    return m_pfad;
  }

  std::string alsOsPfad() const {
    // Pruefe, ob in eigenem Datenverzeichnis existiert.
    // Wenn nein -> gib Pfad in offiziellem Datenverzeichnis zurueck (unabhaengig davon, ob er existiert)
    std::string resultEigenes = getZusiDatenpfad();
    if constexpr (osSep != zusiSep) {
      std::replace_copy(m_pfad.begin(), m_pfad.end(), std::back_inserter(resultEigenes), zusiSep, osSep);
    } else {
      resultEigenes += m_pfad;
    }

#ifdef _WIN32
    WIN32_FIND_DATAW findData;
#endif

    if (
#ifdef _WIN32
      FindFirstFileW(boost::nowide::widen(resultEigenes).c_str(), &findData) != INVALID_HANDLE_VALUE
#else
      euidaccess(resultEigenes.c_str(), F_OK) == 0
#endif
      ) {
      return resultEigenes;
    }

    std::string resultOffiziell = getZusiDatenpfadOffiziell();
    if constexpr (osSep != zusiSep) {
      std::replace_copy(m_pfad.begin(), m_pfad.end(), std::back_inserter(resultOffiziell), zusiSep, osSep);
    } else {
      resultOffiziell += m_pfad;
    }

    return resultOffiziell;
  }
private:
  // Pfad relativ zum Zusi-Datenverzeichnis; Separator: Backslash; kein fuehrender Backslash
  std::string m_pfad;

  explicit ZusiPfad(std::string pfad) : m_pfad(std::move(pfad)) {}
};

}  // namespace zusixml

#endif  // ZUSI_PARSER_UTILS_HPP_
