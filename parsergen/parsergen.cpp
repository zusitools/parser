#include "pugixml.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <experimental/filesystem>
#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::experimental::filesystem;

namespace {
  size_t align(size_t size, size_t alignment) {
    return size + (alignment - (size % alignment)) % alignment;
  }
}  // namespace

enum class AttributeType {
  Int32,
  Int64,
  Boolean,
  String,
  Float,
  DateTime,
  HexInt32,
  FaceIndexes,
  ArgbColor,
};

struct ElementType;

struct Thing {
  std::string name;
  std::string documentation;

  bool deprecated() const {
    return documentation.find("@deprecated") != std::string::npos;
  }
};

struct Attribute : Thing {
  AttributeType type;
};

struct Child : Thing {
  const ElementType* type;  // never null
  bool multiple;
};

struct ChildRaw : Thing {
  std::string type;
  bool multiple;
};

struct ElementType : Thing {
  const ElementType* base;  // may be null
  std::vector<Attribute> attributes;
  std::vector<Child> children;
};

struct ElementTypeRaw : Thing {
  std::string base;
  std::vector<Attribute> attributes;
  std::vector<ChildRaw> children;
};

/** Strategy to embed a single child or a collection of children into a parent struct. */
enum class ChildStrategy {
  UniquePtr,        ///<  std::unique_ptr<Child> member / std::vector<std::unique_ptr<Child>> children_Child
  Optional,         ///<  std::optional<Child> member
  Inline,           ///<  Child member / std::vector<Child> children_Child
};

class ParserGenerator {
 public:
  ParserGenerator(std::vector<std::unique_ptr<ElementType>>* elementTypes) : m_element_types(std::move(*elementTypes)) { }

  void GenerateTypeIncludes(std::ostream& out) {
    out << "#include \"boost/container/small_vector.hpp\"" << std::endl;
    out << "#include <vector>  // for std::vector" << std::endl;
    out << "#include <memory>  // for std::unique_ptr" << std::endl;
    out << "#include <optional>// for std::optional" << std::endl;
    out << "#include <ctime>   // for struct tm, strptime" << std::endl;
    out << "struct ArgbColor {\n";
    out << "  uint8_t a, r, g, b;\n";
    out << "};\n";
  }

  void GenerateTypeDeclarations(std::ostream& out) {
    for (const auto& elementType : m_element_types) {
      out << "struct " << elementType->name << ";" << std::endl;
    }
  }

  void GenerateTypeDefinitions(std::ostream& out) {
    // This map contains dependencies of type hierarchies.
    // K -> V is contained in the map if a type from tree V depends on a type from tree K,
    // i.e. uses it as a child type.
    std::multimap<const ElementType*, const ElementType*> dependencies;

    // Compute dependencies
    for (const auto& elementType : m_element_types) {
      if (elementType->base != nullptr) {
        dependencies.emplace(elementType->base, elementType.get());
      }
      for (const auto& child : elementType->children) {
        if (child.type != elementType.get()) {  // A type may depend on itself -> this needs a pointer anyway
          dependencies.emplace(child.type, elementType.get());
        }
      }
    }

    // Topologically sort types.
    std::vector<const ElementType*> elementTypesTopologicallySorted;

    std::vector<const ElementType*> workList;
    // Insert elements with no incoming edges into the work list
    for (const auto& elementType : m_element_types) {
      if (dependencies.find(elementType.get()) == std::end(dependencies)) {
        workList.push_back(elementType.get());
      }
    }

    while (workList.size() > 0) {
      const ElementType* t = workList.back();
      workList.pop_back();
      elementTypesTopologicallySorted.push_back(t);
      // Remove all outgoing edges of t
      for (auto it = dependencies.begin(); it != dependencies.end(); ) {
        if (it->second == t) {
          const ElementType* k = it->first;
          it = dependencies.erase(it);
          // If we removed the last incoming edge to k, add it to the work list.
          if (dependencies.find(k) == std::end(dependencies)) {
            workList.push_back(k);
          }
        } else {
          ++it;
        }
      }
    }

    if (dependencies.size() > 0) {
      std::cerr << "Cyclic dependencies\n";
      for (const auto& it : dependencies) {
        std::cerr << " - " << it.first->name << " <- " << it.second->name << "\n";
      }
    }

    for (auto it = std::rbegin(elementTypesTopologicallySorted); it != std::rend(elementTypesTopologicallySorted); ++it) {
      const ElementType* elementType = *it;

      if (!elementType->documentation.empty()) {
        out << "/** " << elementType->documentation << "*/" << std::endl;
      }
      out << "struct " << elementType->name;
      if (elementType->base) {
        out << " : " << elementType->base->name;
      }
      out << " {" << std::endl;

      size_t elementSize = 0;

      for (const auto& attribute : elementType->attributes) {
        if (attribute.deprecated()) {
          continue;
        }
        if (!attribute.documentation.empty()) {
          out << "  /** " << attribute.documentation << "*/" << std::endl;
        }
        out << "  ";
        switch (attribute.type) {
          case AttributeType::Int32:
            out << "int32_t";
            elementSize = align(elementSize, alignof(int32_t)) + sizeof(int32_t);
            break;
          case AttributeType::Int64:
            out << "int64_t";
            elementSize = align(elementSize, alignof(int64_t)) + sizeof(int64_t);
            break;
          case AttributeType::Boolean:
            out << "bool";
            elementSize = align(elementSize, alignof(bool)) + sizeof(bool);
            break;
          case AttributeType::String:
            out << "std::string";
            elementSize = align(elementSize, alignof(std::string)) + sizeof(std::string);
            break;
          case AttributeType::Float:
            out << "float";
            elementSize = align(elementSize, alignof(float)) + sizeof(float);
            break;
          case AttributeType::DateTime:
            out << "struct tm";
            elementSize = align(elementSize, alignof(struct tm)) + sizeof(struct tm);
            break;
          case AttributeType::HexInt32:
            out << "int32_t";
            elementSize = align(elementSize, alignof(int32_t)) + sizeof(int32_t);
            break;
          case AttributeType::FaceIndexes:
            out << "std::array<uint16_t, 3>";
            elementSize = align(elementSize, alignof(std::array<uint16_t, 3>)) + sizeof(std::array<uint16_t, 3>);
            break;
          case AttributeType::ArgbColor:
            out << "ArgbColor";
            elementSize = align(elementSize, alignof(uint32_t)) + sizeof(uint32_t);
            break;
        }
        out << " " << attribute.name << ";" << std::endl;
      }

      for (const auto& child : elementType->children) {
        if (child.deprecated()) {
          continue;
        }
        if (!child.documentation.empty()) {
          out << "  /** " << child.documentation << "*/" << std::endl;
        }
        auto childStrategy = GetChildStrategy(*elementType, child);
        if (child.multiple) {
          // Special treatment for children whose multiplicity is almost always between 1 and 2
          size_t smallVectorSize = SmallVectorSize(*elementType, child);
          if (smallVectorSize > 0) {
            if (childStrategy == ChildStrategy::Inline) {
              out << "  boost::container::small_vector<struct " << child.type->name << ", " << smallVectorSize << ">";
              elementSize = align(elementSize, alignof(std::vector<int>)) + smallVectorSize * m_element_type_sizes.at(child.type) + sizeof(size_t);
            } else {
              out << "  boost::container::small_vector<std::unique_ptr<struct " << child.type->name << ">, " << smallVectorSize << ">";
              elementSize = align(elementSize, alignof(std::vector<int>)) + smallVectorSize * sizeof(void*) + sizeof(size_t);
            }
          } else {
            if (childStrategy == ChildStrategy::Inline) {
              out << "  std::vector<struct " << child.type->name << ">";
            } else {
              out << "  std::vector<std::unique_ptr<struct " << child.type->name << ">>";
            }
            elementSize = align(elementSize, alignof(std::vector<int>)) + sizeof(std::vector<int>);
          }
          out << " children_" << child.name << ";" << std::endl;
        } else {
          if (childStrategy == ChildStrategy::Inline) {
            out << "  struct " << child.type->name << " " << child.name << "; // inlined: size = " << m_element_type_sizes.at(child.type) << std::endl;
            elementSize = align(elementSize, alignof(void*)) + m_element_type_sizes.at(child.type);
          } else if (childStrategy == ChildStrategy::Optional) {
            out << "  std::optional<struct " << child.type->name << "> " << child.name << ";" << std::endl;
            elementSize = align(elementSize + 1, alignof(void*)) + m_element_type_sizes.at(child.type);
          } else {
            out << "  std::unique_ptr<struct " << child.type->name << "> " << child.name << ";" << std::endl;
            elementSize = align(elementSize, alignof(std::unique_ptr<int>)) + sizeof(std::unique_ptr<int>);
          }
        }
      }

      m_element_type_sizes[elementType] = elementSize;

      out << "};" << std::endl;
    }
  }

  std::unordered_set<const ElementType*> GetConcreteTypes() {
    const auto& zusiElementType = std::find_if(std::begin(m_element_types), std::end(m_element_types), [](const auto& type) { return type->name == "Zusi"; });
    assert(zusiElementType != std::end(m_element_types));
    std::unordered_set<const ElementType*> result { zusiElementType->get() };
    for (const auto& elementType : m_element_types) {
      for (auto& child : elementType->children) {
        if (!child.deprecated()) {
          result.emplace(child.type);
        }
      }
    }
    return result;
  }

  void GenerateParseFunctionDeclarations(std::ostream& out, const std::unordered_set<const ElementType*>& typesToExport) {
    out << "namespace rapidxml {" << std::endl;
    for (const auto& elementType : m_element_types) {
      if (typesToExport.find(elementType.get()) == std::end(typesToExport)) {
        continue;
      }
      out << "  static void parse_element_" << elementType->name << "(Ch *& text, void* parseResult);" << std::endl;
      out << "  static void parse_node_attributes_" << elementType->name << "(Ch *& text, void* parseResult);" << std::endl;
      (void)elementType;
    }
    out << "}  // namespace rapidxml" << std::endl;
  }

  void GenerateParseFunctionDefinitions(std::ostream& out, const std::unordered_set<const ElementType*>& typesToExport) {
    out << "#include <array>" << std::endl;
    out << "#include <cstring>  // for memcmp" << std::endl;
    out << "#include <cfloat>   // Workaround for https://svn.boost.org/trac10/ticket/12642" << std::endl;
    out << "#include <string_view>" << std::endl;

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
};

namespace rapidxml {

template<Ch Quote>
static void parse_string(Ch*& text, std::string& result) {
  Ch* const value = text;
  skip<attribute_value_pure_pred<Quote>>(text);
  if (*text == Quote) {
    // No character refs in attribute value
    result = std::string(value, text - value);
  } else {
    Ch* first_ampersand = text;
    skip<attribute_value_pred<Quote>>(text);
    result.resize(text - value);
    // Copy characters until the first ampersand verbatim, use copy_and_expand_character_refs for the rest.
    memcpy(&result[0], value, first_ampersand - value);
    result.resize(first_ampersand - value + copy_and_expand_character_refs<attribute_value_pred<Quote>>(first_ampersand, &result[first_ampersand - value]));
  }
}

static void parse_float(Ch*& text, float& result) {
  const Ch* text_save = text;
  // Fast path for numbers of the form "-XXX.YYY" (enclosed in double quotes), where X and Y are both <= 7 characters long and the minus sign is optional
  bool neg = false;
  if (*text == Ch('-')) {
    neg = true;
    ++text;  // Skip "-"
  }
  Ch* const integer_start = text;
  for (size_t i = 0; i < 7; i++) {
    if (!digit_pred::test(*text)) {
      break;
    }
    ++text;
  }
  Ch* const dot_and_fractional_start = text;
  if (*text == Ch('.')) {
    ++text;  // skip "."
    for (size_t i = 0; i < 7; i++) {
      if (!digit_pred::test(*text)) {
        break;
      }
      ++text;
    }
  }

  if (*text == Ch('"')) {
    size_t len_integer = dot_and_fractional_start - integer_start;
    size_t len_dot_and_fractional = text - dot_and_fractional_start;

    uint32_t result_integer = 0;
    switch(len_integer) {
      case 7: result_integer += (*(integer_start + len_integer - 7) - '0') * 1000000; [[fallthrough]];
      case 6: result_integer += (*(integer_start + len_integer - 6) - '0') * 100000; [[fallthrough]];
      case 5: result_integer += (*(integer_start + len_integer - 5) - '0') * 10000; [[fallthrough]];
      case 4: result_integer += (*(integer_start + len_integer - 4) - '0') * 1000; [[fallthrough]];
      case 3: result_integer += (*(integer_start + len_integer - 3) - '0') * 100; [[fallthrough]];
      case 2: result_integer += (*(integer_start + len_integer - 2) - '0') * 10; [[fallthrough]];
      case 1: result_integer += (*(integer_start + len_integer - 1) - '0') * 1; [[fallthrough]];
      case 0: break;
    }

    if (len_dot_and_fractional > 0) {
      constexpr float scaleFactors[] = { 1E0, /* len_dot_and_fractional == 1 */ 1E0, /* == 2 etc. */ 1E1, 1E2, 1E3, 1E4, 1E5, 1E6, 1E7 };
      uint32_t result_fractional = 0;
      switch(len_dot_and_fractional - 1) {
        case 7: result_fractional += (*(dot_and_fractional_start + len_dot_and_fractional - 7) - '0') * 1000000; [[fallthrough]];
        case 6: result_fractional += (*(dot_and_fractional_start + len_dot_and_fractional - 6) - '0') * 100000; [[fallthrough]];
        case 5: result_fractional += (*(dot_and_fractional_start + len_dot_and_fractional - 5) - '0') * 10000; [[fallthrough]];
        case 4: result_fractional += (*(dot_and_fractional_start + len_dot_and_fractional - 4) - '0') * 1000; [[fallthrough]];
        case 3: result_fractional += (*(dot_and_fractional_start + len_dot_and_fractional - 3) - '0') * 100; [[fallthrough]];
        case 2: result_fractional += (*(dot_and_fractional_start + len_dot_and_fractional - 2) - '0') * 10; [[fallthrough]];
        case 1: result_fractional += (*(dot_and_fractional_start + len_dot_and_fractional - 1) - '0') * 1; [[fallthrough]];
        case 0: break;
      }
      result = result_integer + result_fractional / scaleFactors[len_dot_and_fractional];
    } else {
      result = result_integer;
    }
    if (neg) {
      result = -result;
    }
    return;
  }

  // Slow path for everything else
  text = text_save;
  // std::cerr << "Delegating to slow float parser: " << std::string_view(text, 20) << "\n";
  boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::real_parser<float, decimal_comma_real_policies<float> >(), result);
}

)"" << std::endl;

    out << "#define RAPIDXML_PARSE_ERROR(what, where) throw parse_error(what, where)" << std::endl;

    for (const auto& elementType : m_element_types) {
      if (typesToExport.find(elementType.get()) == std::end(typesToExport)) {
        continue;
      }

      auto allChildren = GetAllChildren(*elementType.get());
      auto allAttributes = GetAllAttributes(*elementType.get());

      std::ostringstream parse_children;
      parse_children << "if (false) { (void)parseResultTyped; }" << std::endl;

      for (const auto& child : allChildren) {
        parse_children << "else if (name_size == " << child.name.size() << " && !memcmp(name, \"" << child.name << "\", " << child.name.size() << ")) {" << std::endl;
        if (child.deprecated()) {
          parse_children << "  // deprecated" << std::endl;
          parse_children << "  skip_element(text);" << std::endl;
          parse_children << "}" << std::endl;
          continue;
        }
        auto childStrategy = GetChildStrategy(*elementType, child);
        if (child.multiple) {
          if (childStrategy == ChildStrategy::Inline) {
            parse_children << "  parse_element_" << child.type->name << "(text, &parseResultTyped->children_" << child.name << ".emplace_back());" << std::endl;
          } else {
            parse_children << "  parse_element_" << child.type->name << "(text, parseResultTyped->children_" << child.name << ".emplace_back(new " << child.type->name << "()).get());" << std::endl;
          }
        } else {
          if (childStrategy == ChildStrategy::Inline) {
            parse_children << "  parse_element_" << child.type->name << "(text, &parseResultTyped->" << child.name << ");" << std::endl;
          } else if (childStrategy == ChildStrategy::Optional) {
            parse_children << "  parseResultTyped->" << child.name << ".emplace();" << std::endl;
            parse_children << "  parse_element_" << child.type->name << "(text, &parseResultTyped->" << child.name << ");" << std::endl;
          } else {
            parse_children << "  std::unique_ptr<" << child.type->name << "> childResult(new " << child.type->name << "());" << std::endl;
            parse_children << "  parseResultTyped->" << child.name << ".swap(childResult);" << std::endl;
#if 0
            parse_children << "  if (childResult) { RAPIDXML_PARSE_ERROR(\"Unexpected multiplicity: Child " << child.name << " of node " << typeName << "\", text); }" << std::endl;
#endif
            parse_children << "  parse_element_" << child.type->name << "(text, parseResultTyped->" << child.name << ".get());" << std::endl;
          }
        }
        parse_children << "}" << std::endl;
      }

      parse_children << "else {" << std::endl;
      parse_children << "  std::cerr << \"Unexpected child of node " << elementType->name << ": '\" << std::string_view(name, name_size) << \"'\\n\";" << std::endl;
      parse_children << "  skip_element(text);" << std::endl;
      parse_children << "}" << std::endl;

      out << R""(  static void parse_element_)"" << elementType->name << R""((Ch *& text, void* parseResult) {

    // Parse attributes, if any
    parse_node_attributes_)"" << elementType->name << R""((text, parseResult);

    // Determine ending type
    if (*text == Ch('>'))
    {
        ++text;
        parse_node_contents(text, [](Ch *&text, void* parseResult) {
            )"" << elementType->name << R""(* parseResultTyped = static_cast<)"" << elementType->name << R""(*>(parseResult);
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

      bool startWhitespaceSkip = std::none_of(std::begin(allAttributes), std::end(allAttributes), [](const auto& attr) {
          return attr.type == AttributeType::String || attr.type == AttributeType::FaceIndexes;
      });
      if (startWhitespaceSkip) {
        parse_attributes << "        skip_unlikely<whitespace_pred>(text);" << std::endl;
      }

      parse_attributes << "        if (false) { (void)parseResultTyped; }" << std::endl;

      // Special treatment for types with WXYZ as attributes
      if (elementType->name == "Vec2") {
        parse_attributes << R""(        if (name_size == 1 && *name >= 'X' && *name <= 'Y') {
          std::array<float Vec2::*, 2> members = { &Vec2::X, &Vec2::Y };
          parse_float(text, parseResultTyped->*members[*name - 'X']);
          skip_unlikely<whitespace_pred>(text);
        })"" << std::endl;
        allAttributes.clear();
      } else if (elementType->name == "Vec3") {
        parse_attributes << R""(        if (name_size == 1 && *name >= 'X' && *name <= 'Z') {
          std::array<float Vec3::*, 3> members = { &Vec3::X, &Vec3::Y, &Vec3::Z };
          parse_float(text, parseResultTyped->*members[*name - 'X']);
          skip_unlikely<whitespace_pred>(text);
        })"" << std::endl;
        allAttributes.clear();
      } else if (elementType->name == "Quaternion") {
        parse_attributes << R""(        if (name_size == 1 && *name >= 'W' && *name <= 'Z') {
          std::array<float Quaternion::*, 4> members = { &Quaternion::W, &Quaternion::X, &Quaternion::Y, &Quaternion::Z };
          parse_float(text, parseResultTyped->*members[*name - 'W']);
          skip_unlikely<whitespace_pred>(text);
        })"" << std::endl;
        allAttributes.clear();
      }

      for (const auto& attr : allAttributes) {
        parse_attributes << "        else if (name_size == " << attr.name.size() << " && !memcmp(name, \"" << attr.name << "\", " << attr.name.size() << ")) {" << std::endl;
        if (attr.deprecated()) {
          if (attr.name == "C" || attr.name == "CA" || attr.name == "CE") {
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            }
            parse_attributes << "          uint32_t tmp;" << std::endl;
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::int_parser<uint32_t, 16, 1, 9>(), tmp);" << std::endl;
            parse_attributes << "          parseResultTyped->";
            if (attr.name == "C") {
              parse_attributes << "Cd";
            } else if (attr.name == "CA") {
              parse_attributes << "Ca";
            } else if (attr.name == "CE") {
              parse_attributes << "Ce";
            }
            parse_attributes << " = ArgbColor { static_cast<uint8_t>((tmp >> 24) & 0xFF), static_cast<uint8_t>(tmp & 0xFF), static_cast<uint8_t>((tmp >> 8) & 0xFF), static_cast<uint8_t>((tmp >> 16) & 0xFF) };" << std::endl;
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
          } else {
            parse_attributes << "          // deprecated" << std::endl;
            parse_attributes << "          if (unlikely(quote == Ch('\\'')))" << std::endl;
            parse_attributes << "            skip<attribute_value_pred<Ch('\\\'')>>(text);" << std::endl;
            parse_attributes << "          else" << std::endl;
            parse_attributes << "            skip<attribute_value_pred<Ch('\\\"')>>(text);" << std::endl;
          }
          parse_attributes << "        }" << std::endl;
          continue;
        }
        switch (attr.type) {
          case AttributeType::Int32:
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            }
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::int_, parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::Int64:
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            }
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::long_long, parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::Boolean:
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            }
            parse_attributes << "          parseResultTyped->" << attr.name << " = (text[0] == '1');" << std::endl;
            parse_attributes << "          ++text;" << std::endl;
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::String:
            parse_attributes << "          if (unlikely(quote == Ch('\\\'')))" << std::endl;
            parse_attributes << "            parse_string<Ch('\\\'')>(text, parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          else" << std::endl;
            parse_attributes << "            parse_string<Ch('\"')>(text, parseResultTyped->" << attr.name << ");" << std::endl;
            break;
          case AttributeType::Float:
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            }
            parse_attributes << "          parse_float(text, parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::DateTime:
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            }
            parse_attributes << "          Ch* value = text;" << std::endl;
            parse_attributes << "          text = strptime(text, \"%Y-%m-%d\", &parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          if (text == nullptr) { text = value; }" << std::endl;
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            parse_attributes << "          value = text;" << std::endl;
            parse_attributes << "          text = strptime(text, \"%H:%M:%S\", &parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          if (text == nullptr) { text = value; }" << std::endl;
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::HexInt32:
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            }
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::int_parser<uint32_t, 16, 1, 9>(), parseResultTyped->" << attr.name << ");" << std::endl;
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::ArgbColor:
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            }
            parse_attributes << "          uint32_t tmp;" << std::endl;
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::int_parser<uint32_t, 16, 1, 9>(), tmp);" << std::endl;
            parse_attributes << "          parseResultTyped->" << attr.name << " = ArgbColor { static_cast<uint8_t>((tmp >> 24) & 0xFF), static_cast<uint8_t>((tmp >> 16) & 0xFF), static_cast<uint8_t>((tmp >> 8) & 0xFF), static_cast<uint8_t>(tmp & 0xFF) };" << std::endl;
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);" << std::endl;
            break;
          case AttributeType::FaceIndexes:
            // no whitespace skipping here, Zusi doesn't do that either
            parse_attributes << "          Ch* values[4];" << std::endl;
            parse_attributes << "          values[0] = text;" << std::endl;
            parse_attributes << "          while (*text >= '0' && *text <= '9') ++text;" << std::endl;
            parse_attributes << "          if (*text != ';') RAPIDXML_PARSE_ERROR(\"expected ';'\", text);" << std::endl;
            parse_attributes << "          ++text;" << std::endl;
            parse_attributes << "          values[1] = text;" << std::endl;
            parse_attributes << "          while (*text >= '0' && *text <= '9') ++text;" << std::endl;
            parse_attributes << "          if (*text != ';') RAPIDXML_PARSE_ERROR(\"expected ';'\", text);" << std::endl;
            parse_attributes << "          ++text;" << std::endl;
            parse_attributes << "          values[2] = text;" << std::endl;
            parse_attributes << "          while (*text >= '0' && *text <= '9') ++text;" << std::endl;
            parse_attributes << "          values[3] = text + 1;" << std::endl;
            parse_attributes << "          for (size_t i = 0; i < 3; i++) {" << std::endl;
            parse_attributes << "            uint16_t result = 0;" << std::endl;
            parse_attributes << "            if (values[i+1] != values[i]) {" << std::endl;
            parse_attributes << "              size_t len = values[i+1] - 1 - values[i];" << std::endl;
            // Adapted from https://tombarta.wordpress.com/2008/04/23/specializing-atoi/
            parse_attributes << "              switch(len) {  // 16 bit short - max. 5 characters" << std::endl;
            parse_attributes << "                case 5: result += (*(values[i] + (len-5)) - '0') * 10000; [[fallthrough]];" << std::endl;
            parse_attributes << "                case 4: result += (*(values[i] + (len-4)) - '0') * 1000; [[fallthrough]];" << std::endl;
            parse_attributes << "                case 3: result += (*(values[i] + (len-3)) - '0') * 100; [[fallthrough]];" << std::endl;
            parse_attributes << "                case 2: result += (*(values[i] + (len-2)) - '0') * 10; [[fallthrough]];" << std::endl;
            parse_attributes << "                case 1: result += (*(values[i] + (len-1)) - '0') * 1; [[fallthrough]];" << std::endl;
            parse_attributes << "                case 0: break;" << std::endl;
            parse_attributes << "                default: RAPIDXML_PARSE_ERROR(\"value too long\", text);" << std::endl;
            parse_attributes << "              }" << std::endl;
            parse_attributes << "            }" << std::endl;
            parse_attributes << "            parseResultTyped->" << attr.name << "[i] = result;" << std::endl;
            parse_attributes << "          }" << std::endl;
            break;
        }
        parse_attributes << "        }" << std::endl;
      }

      parse_attributes << "        else {" << std::endl;
      parse_attributes << "          std::cerr << \"Unexpected attribute of node " << elementType->name << ": '\" << std::string_view(name, name_size) << \"'\\n\";" << std::endl;
      parse_attributes << "          if (unlikely(quote == Ch('\\'')))" << std::endl;
      parse_attributes << "            skip<attribute_value_pred<Ch('\\\'')>>(text);" << std::endl;
      parse_attributes << "          else" << std::endl;
      parse_attributes << "            skip<attribute_value_pred<Ch('\\\"')>>(text);" << std::endl;
      parse_attributes << "        }" << std::endl;

      out << R""(  static void parse_node_attributes_)"" << elementType->name << R""((Ch *& text, void* parseResult) {
        )"" << elementType->name << R""(* parseResultTyped = static_cast<)"" << elementType->name << R""(*>(parseResult);
        // For all attributes 
        while (attribute_name_pred::test(*text))
        {
            // Extract attribute name
            Ch *name = text;
            ++text;     // Skip first character of attribute name
            skip<attribute_name_pred>(text);
            size_t name_size = text - name;

            // Skip whitespace after attribute name
            skip_unlikely<whitespace_pred>(text);

            // Skip =
            if (*text != Ch('='))
                RAPIDXML_PARSE_ERROR("expected =", text);
            ++text;

            // Skip whitespace after =
            skip_unlikely<whitespace_pred>(text);

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
  const std::vector<std::unique_ptr<ElementType>> m_element_types = {};
  std::unordered_map<const ElementType*, size_t> m_element_type_sizes;

  /** Returns a size > 0 if a small vector of this size can be used to hold @p child
   * inside @p parentType. This is the case if the collection will rarely contain more than size elements,
   * but also rarely significantly less. */
  size_t SmallVectorSize(const ElementType& parentType, const Child& child) const {
    assert(child.multiple);
    if (child.type->name == "NachfolgerSelbesModul" || child.type->name == "NachfolgerAnderesModul") {
      return 2;
    }
    return 0;
  }

  /** Returns the strategy to use when embedding the given @p child into the given @parentType as a member. */
  ChildStrategy GetChildStrategy(const ElementType& parentType, const Child& child) const {
    if (child.type != &parentType) {
      if (child.multiple && SmallVectorSize(parentType, child) > 0) {
        return ChildStrategy::Inline;
      }
      if (!child.multiple && m_element_type_sizes.at(child.type) <= 40) {
        return ChildStrategy::Inline;
      }
      if (!child.multiple && child.type->name == "StreckenelementRichtungsInfo") {
        return ChildStrategy::Optional;
      }
    }
    return ChildStrategy::UniquePtr;
  }

  /** Returns the base type, i.e. the least derived parent type, of the given element type. */
  const ElementType* GetBaseType(const ElementType* type) {
    while (type->base != nullptr) {
      type = type->base;
    }
    return type;
  }

  std::vector<Child> GetAllChildren(const ElementType& elementType) {
    const ElementType* curElementType = &elementType;
    std::vector<Child> result(curElementType->children);
    while (curElementType->base) {
      curElementType = curElementType->base;
      std::copy(std::begin(curElementType->children), std::end(curElementType->children), std::back_inserter(result));
    }
    return result;
  }

  std::vector<Attribute> GetAllAttributes(const ElementType& elementType) {
    const ElementType* curElementType = &elementType;
    std::vector<Attribute> result(curElementType->attributes);
    while (curElementType->base) {
      curElementType = curElementType->base;
      std::copy(std::begin(curElementType->attributes), std::end(curElementType->attributes), std::back_inserter(result));
    }
    return result;
  }
};

class ParserGeneratorBuilder {
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

  ParserGenerator Build() {
    std::vector<std::unique_ptr<ElementType>> elementTypes;
    for (const auto& [ typeName, elementTypeRaw] : m_element_types) {
      elementTypes.push_back(std::unique_ptr<ElementType>(new ElementType {
          elementTypeRaw.name,
          elementTypeRaw.documentation,
          nullptr,
          elementTypeRaw.attributes,
          std::vector<Child>()}));
    }
    for (auto& elementType : elementTypes) {
      const auto& elementTypeRaw = m_element_types.at(elementType->name);

      if (elementTypeRaw.base.size() > 0) {
        const auto& baseType = std::find_if(std::begin(elementTypes), std::end(elementTypes), [&elementTypeRaw](const auto& type) { return type->name == elementTypeRaw.base; });
        assert(baseType != std::end(elementTypes));
        elementType->base = baseType->get();
      }

      for (const auto& childRaw : elementTypeRaw.children) {
        const auto& childType = std::find_if(std::begin(elementTypes), std::end(elementTypes), [&childRaw](const auto& type) { return type->name == childRaw.type; });
        assert(childType != std::end(elementTypes));
        elementType->children.emplace_back(Child { childRaw.name, childRaw.documentation, childType->get(), childRaw.multiple });
      }
    }
    return ParserGenerator(&elementTypes);
  }

 private:
  std::unordered_map<std::string, std::unique_ptr<pugi::xml_document>> m_docs_by_name = {};
  std::unordered_map<std::string, ElementTypeRaw> m_element_types;

  std::string GetDocumentation(pugi::xml_node node) {
    return node.select_node("xs:annotation/xs:documentation").node().text().get();
  }

  void FindChildTypes(ElementTypeRaw* elementType, pugi::xml_node node) {
    for (const auto& child : node.children()) {
      if (child.name() == std::string("xs:element")) {
        bool multiple = child.attribute("maxOccurs") &&
            (child.attribute("maxOccurs").as_string() == std::string("unbounded") || child.attribute("maxOccurs").as_int() > 1);
        if (child.attribute("ref")) {
          std::string childTypeName = child.attribute("ref").as_string();
          elementType->children.push_back(ChildRaw { childTypeName, GetDocumentation(child), childTypeName, multiple });
        } else {
          elementType->children.push_back(ChildRaw { child.attribute("name").as_string(), GetDocumentation(child), child.attribute("type").as_string(), multiple });
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

    auto [ it, inserted ] = m_element_types.emplace(std::make_pair(typeName, ElementTypeRaw()));
    auto& elementType = it->second;
    if (!inserted) {
      std::cerr << "Type " << typeName << " defined twice" << std::endl;
      return;
    }

    elementType.name = typeName;
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
      } else if (attributeTypeString == "faceIndexes") {
        attributeType = AttributeType::FaceIndexes;
      } else if (attributeTypeString == "argbColor") {
        attributeType = AttributeType::ArgbColor;
      } else {
        std::cerr << "Unknown attribute type '" << attributeTypeString << "' of attribute '" << child.attribute("name").as_string() << "' of complex type '" << typeName << "'" << std::endl;
        continue;
      }

      elementType.attributes.push_back(Attribute { child.attribute("name").as_string(), GetDocumentation(child), attributeType });
    }
  }
};

int main(int argc, char** argv) {
  assert(argc >= 2);

  ParserGeneratorBuilder builder;
  builder.AddXsdFile(argv[1]);

  ParserGenerator generator = builder.Build();

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
