#include "pugixml.hpp"

#include <cassert>
#include <cstring>
#include <experimental/filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::experimental::filesystem;

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

struct ElementType {
  std::vector<pugi::xml_node> children;
  std::vector<Attribute> attributes;
};

class ParserGenerator {
 public:
  void AddXsdFile(const fs::path& fileName) {
    fs::path fileNameCanonical = fs::canonical(fileName);

    if (m_docs_by_name.find(fileNameCanonical) != std::end(m_docs_by_name)) {
      return;
    }

    std::cerr << "Loading XSD file: " << fileNameCanonical << std::endl;
    auto document = std::make_unique<pugi::xml_document>();
    document->load_file(fileNameCanonical.c_str());

    // Load <xs:include>d files and save them for later
    std::vector<fs::path> includes;
    for (const auto& include : document->select_nodes("//xs:include")) {
      includes.push_back(fs::canonical(include.node().attribute("schemaLocation").as_string(), fileNameCanonical.parent_path()));
    }

    // Parse complex types
    for (const auto& complexType : document->select_nodes("//xs:complexType")) {
      const auto& complexTypeNode = complexType.node();
      auto nameAttribute = complexTypeNode.attribute("name");
      if (!nameAttribute) {
        const auto& parentNode = complexTypeNode.parent();
        assert(!strcmp(parentNode.name(), "xs:element"));
        nameAttribute = parentNode.attribute("name");
      }
      assert(nameAttribute);
      std::string typeName = nameAttribute.as_string();

      const auto [ elementType, inserted ] = m_element_types.emplace(std::make_pair(typeName, ElementType()));
      if (!inserted) {
        std::cerr << "Type " << typeName << " defined twice" << std::endl;
      }
    }

    m_docs_by_name.emplace(fileNameCanonical, std::move(document));

    for (const auto& includeFileName : includes) {
      AddXsdFile(includeFileName);
    }
  }


  void GenerateTypes() {
    for (auto&& [ typeName, elementType ] : m_element_types) {
      std::cout << "struct " << typeName << ";" << std::endl;
    }
  }

 private:
  std::unordered_map<std::string, std::unique_ptr<pugi::xml_document>> m_docs_by_name = {};
  std::map<std::string, ElementType> m_element_types = {};
};

int main(int argc, char** argv) {
  ParserGenerator generator;
  assert(argc >= 2);
  generator.AddXsdFile(argv[1]);
  generator.GenerateTypes();
}
