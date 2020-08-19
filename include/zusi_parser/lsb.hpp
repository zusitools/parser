#ifndef ZUSI_PARSER_LSB_HPP_
#define ZUSI_PARSER_LSB_HPP_

#include "zusi_parser/zusi_types.hpp"

#include <string>
#include <fstream>
#include <type_traits>

bool readLsb(Landschaft* ls3_datei, const zusixml::ZusiPfad& dateiname) {
  if (!ls3_datei->lsb.Dateiname.empty()) {
    std::string lsb_pfad = zusixml::ZusiPfad::vonZusiPfad(ls3_datei->lsb.Dateiname, dateiname).alsOsPfad();

    std::ifstream lsb_stream;
    lsb_stream.exceptions(std::ifstream::failbit | std::ifstream::eofbit | std::ifstream::badbit);
    try {
      lsb_stream.open(lsb_pfad, std::ios::in | std::ios::binary);
    } catch (const std::ifstream::failure& e) {
      std::cerr << lsb_pfad << ": open() failed: " << e.what() << "\n";
      return false;
    }

    try {
      for (const auto& mesh_subset : ls3_datei->children_SubSet) {
        static_assert(sizeof(Vertex) == 40, "Wrong size of Vertex");
        static_assert(offsetof(Vertex, p) == 0, "Wrong offset of Vertex::p");
        static_assert(offsetof(Vertex, n) == 12, "Wrong offset of Vertex::n");
        static_assert(offsetof(Vertex, U) == 24, "Wrong offset of Vertex::U");
        static_assert(offsetof(Vertex, V) == 28, "Wrong offset of Vertex::V");
        static_assert(offsetof(Vertex, U2) == 32, "Wrong offset of Vertex::U2");
        static_assert(offsetof(Vertex, V2) == 36, "Wrong offset of Vertex::V2");
        mesh_subset->children_Vertex.resize(mesh_subset->MeshV);
        lsb_stream.read(reinterpret_cast<char*>(mesh_subset->children_Vertex.data()), mesh_subset->children_Vertex.size() * sizeof(Vertex));

        static_assert(sizeof(Face) == 6, "Wrong size of Face");
        assert(mesh_subset->MeshI % 3 == 0);
        mesh_subset->children_Face.resize(mesh_subset->MeshI / 3);
        lsb_stream.read(reinterpret_cast<char*>(mesh_subset->children_Face.data()), mesh_subset->children_Face.size() * sizeof(Face));
      }
    } catch (const std::ifstream::failure& e) {
      std::cerr << lsb_pfad << ": read() failed: " << e.what() << "\n";
      return false;
    }

    lsb_stream.exceptions(std::ios_base::iostate());
    lsb_stream.peek();
    assert(lsb_stream.eof());
  }

  return true;
}

#endif  // ZUSI_PARSER_LSB_HPP_
