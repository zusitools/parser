#include "pugixml.hpp"

#include <cassert>
#include <cstring>
#include <experimental/filesystem>
#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <sstream>
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

  bool deprecated() const {
    return documentation.find("@deprecated") != std::string::npos;
  }
};

struct Child {
  std::string name;
  std::string type;
  bool multiple;
  std::string documentation;

  bool deprecated() const {
    return documentation.find("@deprecated") != std::string::npos;
  }
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

  void GenerateTypeIncludes(std::ostream& out) {
    out << "#include <vector>  // for std::vector" << std::endl;
    out << "#include <memory>  // for std::unique_ptr" << std::endl;
    out << "#include <ctime>   // for struct tm, strptime" << std::endl;
  }

  void GenerateTypeDeclarations(std::ostream& out) {
    for (auto&& [ typeName, elementType ] : m_element_types) {
      out << "struct " << typeName << ";" << std::endl;
      (void)elementType;
    }
  }

  void GenerateTypeDefinitions(std::ostream& out) {
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
          out << "/** " << elementType.documentation << "*/" << std::endl;
        }
        out << "struct " << typeName;
        if (elementType.base.size() > 0) {
          out << " : " << elementType.base;
        }
        out << " {" << std::endl;

        for (const auto& attribute : elementType.attributes) {
          if (attribute.deprecated()) {
            continue;
          }
          if (!attribute.documentation.empty()) {
            out << "  /** " << attribute.documentation << "*/" << std::endl;
          }
          out << "  ";
          switch (attribute.type) {
            case AttributeType::Int32: out << "int32_t"; break;
            case AttributeType::Int64: out << "int64_t"; break;
            case AttributeType::Boolean: out << "bool"; break;
            case AttributeType::String: out << "std::string"; break;
            case AttributeType::Float: out << "float"; break;
            case AttributeType::DateTime: out << "struct tm"; break;
            case AttributeType::HexInt32: out << "int32_t"; break;
          }
          out << " " << attribute.name << ";" << std::endl;
        }

        for (const auto& child : elementType.children) {
          if (child.deprecated()) {
            continue;
          }
          if (!child.documentation.empty()) {
            out << "  /** " << child.documentation << "*/" << std::endl;
          }
          if (child.multiple) {
            out << "  std::vector<std::unique_ptr<struct " << child.type << ">> children_" << child.name << ";" << std::endl;
          } else {
            out << "  std::unique_ptr<struct " << child.type << "> " << child.name << ";" << std::endl;
          }
        }

        out << "};" << std::endl;
      }
    } while (changed);

    if (done.size() != m_element_types.size()) {
      std::cerr << "Circular type dependency" << std::endl;
    }
  }

  std::unordered_set<std::string> GetConcreteTypes() {
    std::unordered_set<std::string> result = { "Zusi" };
    for (auto&& [ typeName, elementType ] : m_element_types) {
      for (const auto& child : elementType.children) {
        if (!child.deprecated()) {
          result.emplace(child.type);
        }
      }
      (void)typeName;
    }
    return result;
  }

  void GenerateParseFunctionDeclarations(std::ostream& out, const std::unordered_set<std::string>& typesToExport) {
    out << "namespace rapidxml {" << std::endl;
    for (auto&& [ typeName, elementType ] : m_element_types) {
      if (typesToExport.find(typeName) == std::end(typesToExport)) {
        continue;
      }
      out << "  template<> void parse_element<" << typeName << ">(Ch *& text, void* parseResult);" << std::endl;
      out << "  template<> void parse_node_attributes<" << typeName << ">(Ch *& text, void* parseResult);" << std::endl;
      (void)elementType;
    }
    out << "}  // namespace rapidxml" << std::endl;
  }

  void GenerateParseFunctionDefinitions(std::ostream& out, const std::unordered_set<std::string>& typesToExport) {
    out << "#include <cstring>  // for memcmp" << std::endl;
    out << "#include <cfloat>   // Workaround for https://svn.boost.org/trac10/ticket/12642" << std::endl;

    out << "#include <boost/spirit/include/qi_real.hpp>" << std::endl;
    out << "#include <boost/spirit/include/qi_int.hpp>" << std::endl;

    out << R""(template <typename T>
struct decimal_comma_real_policies : boost::spirit::qi::real_policies<T>
{
    template <typename Iterator> static bool parse_dot(Iterator& first, Iterator const& last)
    {
        if (first == last || (*first != ',' && *first != '.'))
            return false;
        ++first;
        return true;
    }
};)"" << std::endl;

    out << "#define RAPIDXML_PARSE_ERROR(what, where) throw parse_error(what, where)" << std::endl;

    out << "namespace rapidxml {" << std::endl;
    for (auto&& [ typeName, elementType ] : m_element_types) {
      if (typesToExport.find(typeName) == std::end(typesToExport)) {
        continue;
      }

      auto allChildren = GetAllChildren(elementType);
      auto allAttributes = GetAllAttributes(elementType);

      std::ostringstream parse_children;
      parse_children << "if (false) { (void)parseResultTyped; }" << std::endl;

      for (const auto& child : allChildren) {
        parse_children << "else if (name_size == " << child.name.size() << " && !memcmp(name, \"" << child.name << "\", " << child.name.size() << ")) {" << std::endl;
        if (child.deprecated()) {
          parse_children << "  // deprecated" << std::endl;
          parse_children << "  parse_element<void>(text, nullptr);" << std::endl;
          parse_children << "}" << std::endl;
          continue;
        }
        parse_children << "  std::unique_ptr<" << child.type << "> childResult(new " << child.type << "());" << std::endl;
        parse_children << "  " << child.type << "* childResultRaw = childResult.get();" << std::endl;
        if (child.multiple) {
          parse_children << "  parseResultTyped->children_" << child.name << ".push_back(std::move(childResult));" << std::endl;
        } else {
#if 0
          parse_children << "  if (parseResultTyped->" << child.name << ") { RAPIDXML_PARSE_ERROR(\"Unexpected multiplicity: Child " << child.name << " of node " << typeName << "\", text); }" << std::endl;
#endif
          parse_children << "  parseResultTyped->" << child.name << " = std::move(childResult);" << std::endl;
        }
        parse_children << "  parse_element<" << child.type << ">(text, childResultRaw);" << std::endl;
        parse_children << "}" << std::endl;
      }

      parse_children << "else {" << std::endl;
      parse_children << "  std::cerr << \"Unexpected child of node " << typeName << ": '\" << std::string(name, name_size) << \"'\" << std::endl;" << std::endl;
      parse_children << "  parse_element<void>(text, nullptr);" << std::endl;
      parse_children << "}" << std::endl;

#if 0
      parse_children << R""(switch (name_size) {
                  /* TODO: autogenerated code here */
                  default: {
                    parse_element<void>(text, nullptr);
                    break;
                  }
                })"";
#endif

      out << R""(  template<> void parse_element<)"" << typeName << R""(>(Ch *& text, void* parseResult) {

    // Parse attributes, if any
    parse_node_attributes<)"" << typeName << R""(>(text, parseResult);

    // Determine ending type
    if (*text == Ch('>'))
    {
        ++text;
        parse_node_contents(text, [](Ch *&text, void* parseResult) {
            )"" << typeName << R""(* parseResultTyped = static_cast<)"" << typeName << R""(*>(parseResult);
            (void)parseResult;
            // Extract element name
            Ch *name = text;
            skip<node_name_pred>(text);
            if (text == name)
                RAPIDXML_PARSE_ERROR("expected element name", text);
            size_t name_size = text - name;

            // Skip whitespace between element name and attributes or >
            skip<whitespace_pred>(text);

            )"" << parse_children.str() << R""(
        }, parseResult);
    }
    else if (*text == Ch('/'))
    {
        ++text;
        if (*text != Ch('>'))
            RAPIDXML_PARSE_ERROR("expected >", text);
        ++text;
    }
    else
        RAPIDXML_PARSE_ERROR("expected >", text);
  })"" << std::endl;

      std::ostringstream parse_attributes;
      parse_attributes << "        if (false) { (void)parseResultTyped; }" << std::endl;

      for (const auto& attr : allAttributes) {
        parse_attributes << "        else if (name_size == " << attr.name.size() << " && !memcmp(name, \"" << attr.name << "\", " << attr.name.size() << ")) {" << std::endl;
        if (attr.deprecated()) {
          parse_attributes << "          // deprecated" << std::endl;
          parse_attributes << "          if (quote == Ch('\\''))" << std::endl;
          parse_attributes << "            skip<attribute_value_pred<Ch('\\\'')>>(text);" << std::endl;
          parse_attributes << "          else" << std::endl;
          parse_attributes << "            skip<attribute_value_pred<Ch('\\\"')>>(text);" << std::endl;
          parse_attributes << "        }" << std::endl;
          continue;
        }
        switch (attr.type) {
          case AttributeType::Int32:
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::int_, parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::Int64:
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::long_long, parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::Boolean:
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            parse_attributes << "          parseResultTyped->" << attr.name << " = (text[0] == '1');" << std::endl;
            parse_attributes << "          ++text;" << std::endl;
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::String:
            parse_attributes << "          Ch* const value = text;" << std::endl;
            parse_attributes << "          if (quote == Ch('\\\''))" << std::endl;
            parse_attributes << "            skip<attribute_value_pure_pred<Ch('\\\'')>>(text);" << std::endl;
            parse_attributes << "          else" << std::endl;
            parse_attributes << "            skip<attribute_value_pure_pred<Ch('\"')>>(text);" << std::endl;
            parse_attributes << "          if (*text == quote) {" << std::endl;
            parse_attributes << "            // No character refs in attribute value" << std::endl;
            parse_attributes << "            parseResultTyped->" << attr.name << " = std::string(value, text - value);" << std::endl;
            parse_attributes << "          } else {" << std::endl;
            parse_attributes << "            Ch* first_ampersand = text;" << std::endl;
            parse_attributes << "            if (quote == Ch('\\\''))" << std::endl;
            parse_attributes << "              skip<attribute_value_pred<Ch('\\\'')>>(text);" << std::endl;
            parse_attributes << "            else" << std::endl;
            parse_attributes << "              skip<attribute_value_pred<Ch('\"')>>(text);" << std::endl;
            parse_attributes << "            parseResultTyped->" << attr.name << ".resize(text - value);" << std::endl;
            parse_attributes << "            // Copy characters until the first ampersand verbatim, use copy_and_expand_character_refs for the rest." << std::endl;
            parse_attributes << "            memcpy(&parseResultTyped->" << attr.name << "[0], value, first_ampersand - value);" << std::endl;
            parse_attributes << "            parseResultTyped->" << attr.name << ".resize(first_ampersand - value + (quote == Ch('\\\'') ? " << std::endl;
            parse_attributes << "              copy_and_expand_character_refs<attribute_value_pred<Ch('\\\'')>>(first_ampersand, &parseResultTyped->" << attr.name << "[first_ampersand - value]) :" << std::endl;
            parse_attributes << "              copy_and_expand_character_refs<attribute_value_pred<Ch('\"')>>(first_ampersand, &parseResultTyped->" << attr.name << "[first_ampersand - value])" << std::endl;
            parse_attributes << "            ));" << std::endl;
            parse_attributes << "          }" << std::endl;
            break;
          case AttributeType::Float:
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::real_parser<float, decimal_comma_real_policies<float> >(), parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::DateTime:
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            parse_attributes << "          Ch* value = text;" << std::endl;
            parse_attributes << "          text = strptime(text, \"%Y-%m-%d\", &parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          if (text == nullptr) { text = value; }" << std::endl;
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            parse_attributes << "          value = text;" << std::endl;
            parse_attributes << "          text = strptime(text, \"%H:%M:%S\", &parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          if (text == nullptr) { text = value; }" << std::endl;
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::HexInt32:
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::int_parser<uint32_t, 16, 1, 9>(), parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          skip<whitespace_pred>(text);" << std::endl;
            break;
        }
        parse_attributes << "        }" << std::endl;
      }

      parse_attributes << "        else {" << std::endl;
      parse_attributes << "          std::cerr << \"Unexpected attribute of node " << typeName << ": '\" << std::string(name, name_size) << \"'\" << std::endl;" << std::endl;
      parse_attributes << "          if (quote == Ch('\\''))" << std::endl;
      parse_attributes << "            skip<attribute_value_pred<Ch('\\\'')>>(text);" << std::endl;
      parse_attributes << "          else" << std::endl;
      parse_attributes << "            skip<attribute_value_pred<Ch('\\\"')>>(text);" << std::endl;
      parse_attributes << "        }" << std::endl;

      out << R""(  template<> void parse_node_attributes<)"" << typeName << R""(>(Ch *& text, void* parseResult) {
        )"" << typeName << R""(* parseResultTyped = static_cast<)"" << typeName << R""(*>(parseResult);
        // For all attributes 
        while (attribute_name_pred::test(*text))
        {
            // Extract attribute name
            Ch *name = text;
            ++text;     // Skip first character of attribute name
            skip<attribute_name_pred>(text);
            size_t name_size = text - name;

            // Skip whitespace after attribute name
            skip<whitespace_pred>(text);

            // Skip =
            if (*text != Ch('='))
                RAPIDXML_PARSE_ERROR("expected =", text);
            ++text;

            // Skip whitespace after =
            skip<whitespace_pred>(text);

            // Skip quote and remember if it was ' or "
            Ch quote = *text;
            if (quote != Ch('\'') && quote != Ch('"'))
                RAPIDXML_PARSE_ERROR("expected ' or \"", text);
            ++text;

            // Extract attribute value
            )"" << parse_attributes.str() << R""(

            // Make sure that end quote is present
            if (*text != quote)
                RAPIDXML_PARSE_ERROR("expected ' or \"", text);
            ++text;     // Skip quote

            // Skip whitespace after attribute value
            skip<whitespace_pred>(text);
        }
  })"" << std::endl;

      (void)elementType;
    }
    out << "}  // namespace rapidxml" << std::endl;
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

  std::vector<Child> GetAllChildren(const ElementType& elementType) {
    const ElementType* curElementType = &elementType;
    std::vector<Child> result(curElementType->children);
    while (curElementType->base.size() > 0) {
      curElementType = &m_element_types[curElementType->base];
      std::copy(std::begin(curElementType->children), std::end(curElementType->children), std::back_inserter(result));
    }
    return result;
  }

  std::vector<Attribute> GetAllAttributes(const ElementType& elementType) {
    const ElementType* curElementType = &elementType;
    std::vector<Attribute> result(curElementType->attributes);
    while (curElementType->base.size() > 0) {
      curElementType = &m_element_types[curElementType->base];
      std::copy(std::begin(curElementType->attributes), std::end(curElementType->attributes), std::back_inserter(result));
    }
    return result;
  }
};

int main(int argc, char** argv) {
  ParserGenerator generator;
  assert(argc >= 2);
  generator.AddXsdFile(argv[1]);

  std::ofstream out_types(fs::path(argv[2]) / "zusi_types.hpp");
  generator.GenerateTypeIncludes(out_types);
  generator.GenerateTypeDeclarations(out_types);
  generator.GenerateTypeDefinitions(out_types);

  const auto& concreteTypes = generator.GetConcreteTypes();

  std::ofstream out_parser_fwd(fs::path(argv[2]) / "zusi_parser_fwd.hpp");
  generator.GenerateParseFunctionDeclarations(out_parser_fwd, concreteTypes);

  std::ofstream out_parser(fs::path(argv[2]) / "zusi_parser.hpp");
  generator.GenerateParseFunctionDefinitions(out_parser, concreteTypes);
}
