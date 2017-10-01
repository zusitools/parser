#include "pugixml.hpp"

#include <cassert>

struct ElementType {

};

enum class AttributeType {
  Int32,
  Int64,
  Boolean,
  String,
  Float,
  DateTime,
  HexInt32,
};

struct Attribute {
  std::string name;
  AttributeType type;
};

class ParserGenerator {
 public:
  void AddXsdFile(std::string_view fileName) {

  }
};

int main(int argc, char** argv) {
  ParserGenerator generator;
  assert(argc >= 2);
  generator.AddXsdFile(argv[1]);
}
