#include "pugixml.hpp"

#include <cassert>
#include <cstring>
#include <experimental/filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
  std::string documentation;
};

struct Child {
  std::string name;
  std::string type;
  bool multiple;
  std::string documentation;
};

struct ElementType {
  std::string base;
  std::vector<Attribute> attributes;
  std::vector<Child> children;
  std::string documentation;
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
      (void)elementType;
    }
  }

  void GenerateTypeDefinitions() {
    // poor man's topological sort
    std::unordered_set<std::string> done;
    bool changed;
    do {
      changed = false;

      for (auto&& [ typeName, elementType ] : m_element_types) {
        if (done.find(typeName) != std::end(done) || (elementType.base.size() > 0 && done.find(elementType.base) == std::end(done))) {
          continue;
        }

        done.insert(typeName);
        changed = true;

        if (!elementType.documentation.empty()) {
          std::cout << "/** " << elementType.documentation << "*/" << std::endl;
        }
        std::cout << "struct " << typeName;
        if (elementType.base.size() > 0) {
          std::cout << " : " << elementType.base;
        }
        std::cout << " {" << std::endl;

        for (const auto& attribute : elementType.attributes) {
          if (!attribute.documentation.empty()) {
            std::cout << "  /** " << attribute.documentation << "*/" << std::endl;
          }
          std::cout << "  ";
          switch (attribute.type) {
            case AttributeType::Int32: std::cout << "int32_t"; break;
            case AttributeType::Int64: std::cout << "int64_t"; break;
            case AttributeType::Boolean: std::cout << "bool"; break;
            case AttributeType::String: std::cout << "std::string"; break;
            case AttributeType::Float: std::cout << "float"; break;
            case AttributeType::DateTime: std::cout << "std::string"; break;  // TODO
            case AttributeType::HexInt32: std::cout << "int32_t"; break;
          }
          std::cout << " " << attribute.name << ";" << std::endl;
        }

        for (const auto& child : elementType.children) {
          if (!child.documentation.empty()) {
            std::cout << "  /** " << child.documentation << "*/" << std::endl;
          }
          if (child.multiple) {
            std::cout << "  std::vector<std::unique_ptr<struct " << child.type << ">> children_" << child.name << ";" << std::endl;
          } else {
            std::cout << "  std::unique_ptr<struct " << child.type << "> " << child.name << ";" << std::endl;
          }
        }

        std::cout << "};" << std::endl;
      }
    } while (changed);

    if (done.size() != m_element_types.size()) {
      std::cerr << "Circular type dependency" << std::endl;
    }
  }

 private:
  std::unordered_map<std::string, std::unique_ptr<pugi::xml_document>> m_docs_by_name = {};
  std::map<std::string, ElementType> m_element_types = {};

  std::string GetDocumentation(pugi::xml_node node) {
    return node.select_node("xs:annotation/xs:documentation").node().text().get();
  }

  void FindChildTypes(ElementType* elementType, pugi::xml_node node) {
    for (const auto& child : node.children()) {
      if (child.name() == std::string("xs:element")) {
        bool multiple = child.attribute("maxOccurs") &&
            (child.attribute("maxOccurs").as_string() == std::string("unbounded") || child.attribute("maxOccurs").as_int() > 1);
        if (child.attribute("ref")) {
          std::string childTypeName = child.attribute("ref").as_string();
          elementType->children.push_back(Child { childTypeName, childTypeName, multiple, GetDocumentation(child) });
        } else {
          elementType->children.push_back(Child { child.attribute("name").as_string(), child.attribute("type").as_string(), multiple, GetDocumentation(child) });
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

    elementType.documentation = GetDocumentation(complexTypeNode);
    FindChildTypes(&elementType, complexTypeNode);

    auto complexContent = complexTypeNode.child("xs:complexContent");
    if (complexContent) {
      complexTypeNode = complexContent;
    }

    auto extension = complexTypeNode.child("xs:extension");
    if (extension) {
      elementType.base = extension.attribute("base").as_string();
      complexTypeNode = extension;
    }

    for (const auto& child : complexTypeNode.children()) {
      if (child.name() != std::string("xs:attribute")) {
        continue;
      }

      AttributeType attributeType;
      std::string attributeTypeString = child.attribute("type").as_string();
      if (attributeTypeString == "xs:int") {
        attributeType = AttributeType::Int32;
      } else if (attributeTypeString == "xs:long") {
        attributeType = AttributeType::Int64;
      } else if (attributeTypeString == "xs:float") {
        attributeType = AttributeType::Float;
      } else if (attributeTypeString == "xs:string") {
        attributeType = AttributeType::String;
      } else if (attributeTypeString == "boolean") {
        attributeType = AttributeType::Boolean;
      } else if (attributeTypeString == "xs:hexBinary") {
        attributeType = AttributeType::HexInt32;
      } else if (attributeTypeString == "dateTime") {
        attributeType = AttributeType::DateTime;
      } else {
        std::cerr << "Unknown attribute type '" << attributeTypeString << "' of attribute '" << child.attribute("name").as_string() << "' of complex type '" << typeName << "'" << std::endl;
        continue;
      }

      elementType.attributes.push_back(Attribute { child.attribute("name").as_string(), attributeType, GetDocumentation(child) });
    }
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
