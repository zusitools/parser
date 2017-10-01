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

struct Child {
  std::string name;
  std::string type;
  bool multiple;
};

struct ElementType {
  std::vector<Attribute> attributes;
  std::vector<Child> children;
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
      ParseComplexType(complexType.node());
    }

    m_docs_by_name.emplace(fileNameCanonical, std::move(document));

    for (const auto& includeFileName : includes) {
      AddXsdFile(includeFileName);
    }
  }

  void GenerateIncludes() {
    std::cout << "#include <vector>  // for std::vector" << std::endl;
    std::cout << "#include <memory>  // for std::unique_ptr" << std::endl;
  }

  void GenerateTypeDeclarations() {
    for (auto&& [ typeName, elementType ] : m_element_types) {
      std::cout << "struct " << typeName << ";" << std::endl;
    }
  }

  void GenerateTypeDefinitions() {
    for (auto&& [ typeName, elementType ] : m_element_types) {
      std::cout << "struct " << typeName << " {" << std::endl;

      for (const auto& child : elementType.children) {
        if (child.multiple) {
          std::cout << "  std::vector<std::unique_ptr<struct " << child.type << ">> children_" << child.name << ";" << std::endl;
        } else {
          std::cout << "  std::unique_ptr<struct " << child.type << "> " << child.name << ";" << std::endl;
        }
      }

      std::cout << "};" << std::endl;
    }
  }

 private:
  std::unordered_map<std::string, std::unique_ptr<pugi::xml_document>> m_docs_by_name = {};
  std::map<std::string, ElementType> m_element_types = {};

  void FindChildTypes(ElementType* elementType, pugi::xml_node node) {
    for (const auto& child : node.children()) {
      if (child.name() == std::string("xs:element")) {
        bool multiple = child.attribute("maxOccurs") &&
            (child.attribute("maxOccurs").as_string() == std::string("unbounded") || child.attribute("maxOccurs").as_int() > 1);
        if (child.attribute("ref")) {
          std::string childTypeName = child.attribute("ref").as_string();
          elementType->children.push_back(Child { childTypeName, childTypeName, multiple });
        } else {
          elementType->children.push_back(Child { child.attribute("name").as_string(), child.attribute("type").as_string(), multiple });
        }
      } else {
        FindChildTypes(elementType, child);
      }
    }
  }

  void ParseComplexType(pugi::xml_node complexTypeNode) {
    auto nameAttribute = complexTypeNode.attribute("name");
    if (!nameAttribute) {
      const auto& parentNode = complexTypeNode.parent();
      assert(parentNode.name() == std::string("xs:element"));
      nameAttribute = parentNode.attribute("name");
    }
    assert(nameAttribute);
    std::string typeName = nameAttribute.as_string();

    auto [ it, inserted ] = m_element_types.emplace(std::make_pair(typeName, ElementType()));
    auto& elementType = it->second;
    if (!inserted) {
      std::cerr << "Type " << typeName << " defined twice" << std::endl;
      return;
    }

    FindChildTypes(&elementType, complexTypeNode);
  }
};

int main(int argc, char** argv) {
  ParserGenerator generator;
  assert(argc >= 2);
  generator.AddXsdFile(argv[1]);
  generator.GenerateIncludes();
  generator.GenerateTypeDeclarations();
  generator.GenerateTypeDefinitions();
}
