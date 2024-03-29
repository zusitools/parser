#include "pugixml.hpp"

// #define ZUSIXML_SCHEMA_XML_MODE

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#ifdef ZUSI_PARSER_USE_BOOST_FILESYSTEM
  #include <boost/filesystem.hpp>
  namespace fs = boost::filesystem;
  using ofstream = fs::ofstream;
#else
  #include <filesystem>
  namespace fs = std::filesystem;
  using ofstream = std::ofstream;
#endif
#include <boost/program_options.hpp>
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

namespace po = boost::program_options;

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
  std::string cppName;  // name including namespace and "struct " prefix
  const ElementType* base;  // may be null
  std::vector<Attribute> attributes;
  std::vector<Child> children;
};

struct ElementTypeRaw : Thing {
  std::string base;
  std::vector<Attribute> attributes;
  std::vector<ChildRaw> children;
};

struct Config {
  std::unordered_map<std::string, std::unordered_set<std::string>> whitelist;
  bool ignore_unknown { false };
  bool use_glm { false };
};

/** Returns a size > 0 if a small vector of this size can be used to hold @p child
 * inside @p parentType. This is the case if the collection will rarely contain more than size elements,
 * but also rarely significantly fewer. */
size_t SmallVectorSize(const ElementType& /*parentType*/, const Child& child) {
  assert(child.multiple);
  if (child.type->name == "NachfolgerSelbesModul" || child.type->name == "NachfolgerAnderesModul") {
    return 2;
  }
  return 0;
}

/** Strategy to embed a single child or a collection of children into a parent struct. */
class ChildStrategy {
 public:
  virtual std::string GetMemberDeclaration(const ElementType& elementType, const Child& child) = 0;
  virtual std::string GetParseMemberCode(const ElementType& elementType, const Child& child) = 0;
  virtual std::size_t UpdateElementSize(const ElementType& elementType, const Child& child, std::size_t elementSize, std::size_t childElementSize) = 0;
  virtual ~ChildStrategy() = default;
};

class UniquePtrChildStrategy : public ChildStrategy {
 public:
  std::string GetMemberDeclaration(const ElementType& elementType, const Child& child) override {
    std::ostringstream out;
    std::string unique_ptr_type = std::string("std::unique_ptr<") + child.type->cppName + ", zusixml::deleter<" + child.type->cppName + ">>";

    if (child.multiple) {
      size_t smallVectorSize = SmallVectorSize(elementType, child);
      if (smallVectorSize > 0) {
        out << "  boost::container::small_vector<" << unique_ptr_type << ", " << smallVectorSize << ", zusixml::allocator<" << unique_ptr_type << ">>";
      } else {
        out << "  std::vector<" << unique_ptr_type << ", zusixml::allocator<" << unique_ptr_type << ">>";
      }
      out << " children_" << child.name << ";\n";
    } else {
      out << "  " << unique_ptr_type << " " << child.name << ";\n";
    }

    return out.str();
  }

  std::string GetParseMemberCode(const ElementType& /*elementType*/, const Child& child) override {
    std::ostringstream out;

    if (child.multiple) {
      if (child.type->name == "StrElement" || child.type->name == "ReferenzElement") {
        out << "  std::unique_ptr<" << child.type->name << "> childResult(new " << child.type->name << "());\n";
        out << "  parse_element_" << child.type->name << "(text, childResult.get());\n";
        out << "  size_t index = childResult->";
        if (child.type->name == "StrElement") {
          out << "Nr";
        } else if (child.type->name == "ReferenzElement") {
          out << "ReferenzNr";
        }
        out << ";\n";
        out << "  if (index == parseResult->children_" << child.name << ".size()) {\n";  // contiguous elements
        out << "    parseResult->children_" << child.name << ".push_back(std::move(childResult));\n";
        out << "  } else {\n";
        out << "    if (index > parseResult->children_" << child.name << ".size()) {\n";
        out << "      parseResult->children_" << child.name << ".resize(index + 1);\n";
        out << "    } else if (parseResult->children_" << child.name << "[index]) {\n";
        out << "      std::cerr << \"Ignoriere doppelten Eintrag fuer " << child.name << ": \" << index << \"\\n\";\n";
        out << "    }\n";
        out << "    parseResult->children_" << child.name << "[index] = std::move(childResult);\n";
        out << "  }\n";
      } else {
        out << "  parse_element_" << child.type->name << "(text, parseResult->children_" << child.name << ".emplace_back(new " << child.type->cppName << "()).get());\n";
      }
    } else {
      out << "  std::unique_ptr<" << child.type->cppName << ", zusixml::deleter<" << child.type->cppName << ">> childResult(new " << child.type->cppName << "());\n";
      out << "  parseResult->" << child.name << ".swap(childResult);\n";
#if 0
      out << "  if (childResult) { RAPIDXML_PARSE_ERROR(\"Unexpected multiplicity: Child " << child.name << " of node " << typeName << "\", text); }\n";
#endif
      out << "  parse_element_" << child.type->name << "(text, parseResult->" << child.name << ".get());\n";
    }
    return out.str();
  }

  std::size_t UpdateElementSize(const ElementType& elementType, const Child& child, std::size_t elementSize, std::size_t /*childElementSize*/) override {
    if (child.multiple) {
      size_t smallVectorSize = SmallVectorSize(elementType, child);
      if (smallVectorSize > 0) {
        return align(elementSize, alignof(std::vector<int>)) + smallVectorSize * sizeof(void*) + sizeof(size_t);
      } else {
        return align(elementSize, alignof(std::vector<int>)) + sizeof(std::vector<int>);
      }
    } else {
      return align(elementSize, alignof(std::unique_ptr<int>)) + sizeof(std::unique_ptr<int>);
    }
  }
};

class OptionalChildStrategy : public ChildStrategy {
 public:
  std::string GetMemberDeclaration(const ElementType& /*elementType*/, const Child& child) override {
    assert(!child.multiple);
    std::ostringstream out;
    out << "  std::optional<" << child.type->cppName << "> " << child.name << ";\n";
    return out.str();
  }

  std::string GetParseMemberCode(const ElementType& /*elementType*/, const Child& child) override {
    assert(!child.multiple);
    std::ostringstream out;
    out << "  parseResult->" << child.name << ".emplace();\n";
    out << "  parse_element_" << child.type->name << "(text, &*parseResult->" << child.name << ");\n";
    return out.str();
  }

  std::size_t UpdateElementSize(const ElementType& /*elementType*/, const Child& /*child*/, std::size_t elementSize, std::size_t childElementSize) override {
    return align(elementSize + 1, alignof(void*)) + childElementSize;
  }
};

class InlineChildStrategy : public ChildStrategy {
 public:
  std::string GetMemberDeclaration(const ElementType& elementType, const Child& child) override {
    std::ostringstream out;

    if (child.multiple) {
      size_t smallVectorSize = SmallVectorSize(elementType, child);
      if (smallVectorSize > 0) {
        out << "  boost::container::small_vector<" << child.type->cppName << ", " << smallVectorSize << ">";
      } else {
        out << "  std::vector<" << child.type->cppName << ">";
      }
      out << " children_" << child.name << ";\n";
    } else {
      out << " " << child.type->cppName << " " << child.name << "; // inlined\n";
    }

    return out.str();
  }

  std::string GetParseMemberCode(const ElementType& elementType, const Child& child) override {
    std::ostringstream out;

    if (child.multiple) {
      size_t smallVectorSize = SmallVectorSize(elementType, child);
      if (smallVectorSize > 0) {
        // Boost < 1.62 (as used in MXE) does not return an iterator to the emplaced element
        out << "#if BOOST_VERSION < 106200\n";
        out << "  parseResult->children_" << child.name << ".emplace_back();\n";
        out << "  parse_element_" << child.type->name << "(text, &parseResult->children_" << child.name << ".back());\n";
        out << "#else\n";
      }
      out << "  parse_element_" << child.type->name << "(text, &parseResult->children_" << child.name << ".emplace_back());\n";
      if (smallVectorSize > 0) {
        out << "#endif\n";
      }
    } else {
      out << "  parse_element_" << child.type->name << "(text, &parseResult->" << child.name << ");\n";
    }

    return out.str();
  }

  std::size_t UpdateElementSize(const ElementType& elementType, const Child& child, std::size_t elementSize, std::size_t childElementSize) override {
    if (child.multiple) {
      size_t smallVectorSize = SmallVectorSize(elementType, child);
      if (smallVectorSize > 0) {
        return align(elementSize, alignof(std::vector<int>)) + smallVectorSize * childElementSize + sizeof(size_t);
      } else {
        return align(elementSize, alignof(std::vector<int>)) + sizeof(std::vector<int>);
      }
    } else {
      return align(elementSize, alignof(void*)) + childElementSize;
    }
  }
};

class ParserGenerator {
 public:
  ParserGenerator(std::vector<std::unique_ptr<ElementType>>* elementTypes, Config config)
    : m_element_types(std::move(*elementTypes)), m_config(std::move(config)),
      m_used_element_types(GetUsedElementTypes()), m_concrete_element_types(GetConcreteElementTypes()) { }

  void GenerateTypeIncludes(std::ostream& out) {
    out << "#pragma once\n";
    out << "#include \"zusi_parser/zusi_types_fwd.hpp\"\n";
    out << "#include \"boost/container/small_vector.hpp\"\n";
    out << "#include <array>   // for std::array\n";
    out << "#include <vector>  // for std::vector\n";
    out << "#include <memory>  // for std::unique_ptr\n";
    out << "#include <optional>// for std::optional\n";
    out << "#include <string>  // for std::string\n";
    out << "#include <ctime>   // for struct tm\n";
    out << "struct ArgbColor {\n";
    out << "  uint8_t a, r, g, b;\n";
    out << "};\n";
    out << "namespace zusixml {\n";
    out << "  template <typename T>\n";
    out << "  using allocator = std::allocator<T>;\n";
    out << "  template <typename T>\n";
    out << "  using deleter = std::default_delete<T>;\n";
    out << "}\n";
  }

  void GenerateTypeDeclarations(std::ostream& out) {
    out << "#pragma once\n";
    if (m_config.use_glm) {
      out << "#include <glm/glm.hpp>\n";
      out << "#include <glm/gtx/quaternion.hpp>\n";
    }
    for (const auto& elementType : m_element_types) {
      if (m_used_element_types.find(elementType.get()) == std::end(m_used_element_types)) {
        continue;
      }
      if (m_config.use_glm) {
        if (elementType->name == "Vec2") {
          out << "using Vec2 = glm::tvec2<float>;\n";
          continue;
        } else if (elementType->name == "Vec3") {
          out << "using Vec3 = glm::tvec3<float>;\n";
          continue;
        } else if (elementType->name == "Quaternion") {
          out << "using Quaternion = glm::tquat<float>;\n";
          continue;
        }
      }

      out << "struct " << elementType->name << ";\n";
    }
  }

  void GenerateTypeDefinitions(std::ostream& out) {
    struct compareElementTypesByName {
      bool operator()(const ElementType* lhs, const ElementType* rhs) const {
        assert(lhs != nullptr);
        assert(rhs != nullptr);
        return lhs->name < rhs->name;
      }
    };

    // This map contains dependencies of type hierarchies.
    // K -> V is contained in the map if a type from tree V depends on a type from tree K,
    // i.e. uses it as a child type.
    std::multimap<const ElementType*, const ElementType*, compareElementTypesByName> dependencies;

    // Compute dependencies
    for (const auto& elementType : m_used_element_types) {
      if (elementType->base != nullptr) {
        dependencies.emplace(elementType->base, elementType);
      }
      for (const auto& child : elementType->children) {
        if (child.type != elementType && IsOnWhitelist(*elementType, child)) {  // A type may depend on itself -> this needs a pointer anyway
          dependencies.emplace(child.type, elementType);
        }
      }
    }

    // Topologically sort types so that the definition of a child element's type comes before the definition of the parent element's type.
    std::vector<const ElementType*> elementTypesTopologicallySorted;

    std::vector<const ElementType*> workList;
    // Insert elements with no incoming edges into the work list
    for (const auto& elementType : m_element_types) {
      if (m_used_element_types.find(elementType.get()) == std::end(m_used_element_types)) {
        continue;
      }
      if (dependencies.find(elementType.get()) == std::end(dependencies)) {
        workList.push_back(elementType.get());
      }
    }

    while (!workList.empty()) {
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

    if (!dependencies.empty()) {
      std::cerr << "Cyclic dependencies\n";
      for (const auto& it : dependencies) {
        std::cerr << " - " << it.first->name << " <- " << it.second->name << "\n";
      }
    }

    for (auto it = std::rbegin(elementTypesTopologicallySorted); it != std::rend(elementTypesTopologicallySorted); ++it) {
      const ElementType* elementType = *it;

      if (m_config.use_glm && ((elementType->name == "Vec2") || (elementType->name == "Vec3") || (elementType->name == "Quaternion"))) {
        continue;
      }

      if (!elementType->documentation.empty()) {
        out << "/** " << elementType->documentation << "*/\n";
      }
      out << "struct " << elementType->name;
      if (elementType->base) {
        out << " : " << elementType->base->name;
      }
      out << " {\n";

      size_t elementSize = 0;

      std::ostringstream attrs;
      for (const auto& attribute : elementType->attributes) {
        if (!IsOnWhitelist(*elementType, attribute)) {
          continue;
        }
        if (!attribute.documentation.empty()) {
          attrs << "  /** " << attribute.documentation << "*/\n";
        }
        attrs << "  ";
        switch (attribute.type) {
          case AttributeType::Int32:
            attrs << "int32_t";
            elementSize = align(elementSize, alignof(int32_t)) + sizeof(int32_t);
            break;
          case AttributeType::Int64:
            attrs << "int64_t";
            elementSize = align(elementSize, alignof(int64_t)) + sizeof(int64_t);
            break;
          case AttributeType::Boolean:
            attrs << "bool";
            elementSize = align(elementSize, alignof(bool)) + sizeof(bool);
            break;
          case AttributeType::String:
            attrs << "std::basic_string<char, std::char_traits<char>, zusixml::allocator<char>>";
            elementSize = align(elementSize, alignof(std::string)) + sizeof(std::string);
            break;
          case AttributeType::Float:
            attrs << "float";
            elementSize = align(elementSize, alignof(float)) + sizeof(float);
            break;
          case AttributeType::DateTime:
            attrs << "struct tm";
            elementSize = align(elementSize, alignof(struct tm)) + sizeof(struct tm);
            break;
          case AttributeType::HexInt32:
            attrs << "int32_t";
            elementSize = align(elementSize, alignof(int32_t)) + sizeof(int32_t);
            break;
          case AttributeType::FaceIndexes:
            attrs << "std::array<uint16_t, 3>";
            elementSize = align(elementSize, alignof(std::array<uint16_t, 3>)) + sizeof(std::array<uint16_t, 3>);
            break;
          case AttributeType::ArgbColor:
            attrs << "ArgbColor";
            elementSize = align(elementSize, alignof(uint32_t)) + sizeof(uint32_t);
            break;
        }
        attrs << " " << attribute.name << ";\n";
      }

      std::ostringstream children;
      for (const auto& child : elementType->children) {
        if (!IsOnWhitelist(*elementType, child)) {
          continue;
        }
        if (!child.documentation.empty()) {
          children << "  /** " << child.documentation << "*/\n";
        }
        const auto& childStrategy = GetChildStrategy(*elementType, child);
        children << childStrategy->GetMemberDeclaration(*elementType, child);

        const auto& childSizeIt = m_element_type_sizes.find(child.type);
        elementSize = childStrategy->UpdateElementSize(*elementType, child, elementSize, childSizeIt == std::end(m_element_type_sizes) ? 9999 : childSizeIt->second);
      }

      m_element_type_sizes[elementType] = elementSize;

      if (elementType->name == "Vertex") {
        // make compatible with you-know-what
        out << children.str() << attrs.str();
      } else {
        out << attrs.str() << children.str();
      }
      out << "};\n";
    }
  }

  void GenerateParseFunctionDeclarations(std::ostream& out) {
    out << "#pragma once\n";
    out << "#include \"zusi_parser/zusi_types_fwd.hpp\"\n";
    out << "namespace zusixml {\n";
    for (const auto& elementType : m_element_types) {
      if (m_used_element_types.find(elementType.get()) == std::end(m_used_element_types)) {
        continue;
      }
      if (m_concrete_element_types.find(elementType.get()) == std::end(m_concrete_element_types)) {
        continue;
      }
      out << "  static void parse_element_" << elementType->name << "(const Ch *&, " << elementType->name << "*);\n";
    }
    out << "}  // namespace zusixml\n";
  }

  void GenerateParseFunctionDefinitions(std::ostream& out) {
    out << "#pragma once\n";
    out << "#include \"zusixml.hpp\"\n";
    out << "#include \"zusi_parser/zusi_types.hpp\"\n";

    out << "#include <array>\n";
    out << "#include <cstring>  // for memcmp\n";
    out << "#include <cfloat>   // Workaround for https://svn.boost.org/trac10/ticket/12642\n";
    out << "#include <iostream> // for cerr\n";
    out << "#include <string_view>\n";

    out << "#include <boost/spirit/include/qi_real.hpp>\n";
    out << "#include <boost/spirit/include/qi_int.hpp>\n";
    out << "#include <boost/version.hpp>\n";

    out << R""(template <typename T>
struct decimal_comma_real_policies : boost::spirit::qi::real_policies<T>
{
    template <typename Iterator> static bool parse_dot(Iterator& first, Iterator const& last)
    {
        if (first == last) {
            return false;
        }

        const auto ch = *first;
        if (ch != ',' && ch != '.') {
            return false;
        }

        ++first;
        return true;
    }
};

namespace zusixml {

static void parse_string(const Ch*& text, std::string& result, Ch quote) {
  const Ch* const value = text;
  skip_attribute_value_pure(text, quote);
  if (*text == quote) {
    // No character refs in attribute value, copy the string verbatim
    result = std::string(value, text - value);
  } else if (*text == Ch('&')) {
    const Ch* first_ampersand = text;
    skip_attribute_value(text, quote);
    result.resize(text - value);
    // Copy characters until the first ampersand verbatim, use copy_and_expand_character_refs for the rest.
    memcpy(&result[0], value, first_ampersand - value);
    result.resize(first_ampersand - value + copy_and_expand_character_refs(first_ampersand, &result[first_ampersand - value], quote));
  }
  // else: *text == '\0'
}

static void parse_float(const Ch*& text, float& result) {
  const Ch* text_save = text;
  // Fast path for numbers of the form "-XXX.YYY" (enclosed in double quotes), where X and Y are both <= 7 characters long and the minus sign is optional
  bool neg = false;
  if (*text == Ch('-')) {
    neg = true;
    ++text;  // Skip "-"
  }
  const Ch* const integer_start = text;
  for (size_t i = 0; i < 7; i++) {
    if (!digit_pred::test(*text)) {
      break;
    }
    ++text;
  }
  const Ch* const dot_and_fractional_start = text;
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

template<Ch Quote>
static bool parse_datetime(const Ch*& text, struct tm& result) {
  // Delphi (and Zusi) accept a very wide range of things here,
  // e.g. two-digit years, times that don't specify seconds or minutes, etc.
  // We are more restrictive: we parse a date yyyy-mm-dd, or a time hh:nn:ss,
  // or both separated by a blank,

  const Ch* prev = text;
  skip_max<digit_pred, 4>(text);

  if (*text == Ch('-')) {
    // date
    const size_t year_len = text - prev;
    int year = 0;
    switch (year_len) {
      case 4: year += (*(prev + (year_len-4)) - '0') * 1000; [[fallthrough]];
      case 3: year += (*(prev + (year_len-3)) - '0') * 100; [[fallthrough]];
      case 2: year += (*(prev + (year_len-2)) - '0') * 10; [[fallthrough]];
      case 1: year += (*(prev + (year_len-1)) - '0') * 1; break;
      default: return false;
    }

    result.tm_year = year - 1900;

    ++text;
    prev = text;
    skip_max<digit_pred, 2>(text);

    if (*text != Ch('-')) {
      return false;
    }

    const size_t month_len = text - prev;
    int month = 0;
    switch (month_len) {
      case 2: month += (*(prev + (month_len-2)) - '0') * 10; [[fallthrough]];
      case 1: month += (*(prev + (month_len-1)) - '0') * 1; break;
      default: return false;
    }

    result.tm_mon = month;

    ++text;
    prev = text;
    skip_max<digit_pred, 2>(text);

    const size_t day_len = text - prev;
    int day = 0;
    switch (day_len) {
      case 2: day += (*(prev + (day_len-2)) - '0') * 10; [[fallthrough]];
      case 1: day += (*(prev + (day_len-1)) - '0') * 1; break;
      default: return false;
    }

    result.tm_mday = day;

    if (*text == Quote) {
      return true;
    } else if (*text == Ch(' ')) {
      ++text;
      prev = text;
      skip_max<digit_pred, 2>(text);
    }
  }

  if (*text == Ch(':')) {
    const size_t hour_len = text - prev;
    int hour = 0;
    switch (hour_len) {
      case 4: hour += (*(prev + (hour_len-4)) - '0') * 1000; [[fallthrough]];
      case 3: hour += (*(prev + (hour_len-3)) - '0') * 100; [[fallthrough]];
      case 2: hour += (*(prev + (hour_len-2)) - '0') * 10; [[fallthrough]];
      case 1: hour += (*(prev + (hour_len-1)) - '0') * 1; break;
      default: return false;
    }

    result.tm_hour = hour;

    ++text;
    prev = text;
    skip_max<digit_pred, 2>(text);

    if (*text != Ch(':')) {
      return false;
    }

    const size_t minute_len = text - prev;
    int minute = 0;
    switch (minute_len) {
      case 2: minute += (*(prev + (minute_len-2)) - '0') * 10; [[fallthrough]];
      case 1: minute += (*(prev + (minute_len-1)) - '0') * 1; break;
      default: return false;
    }

    result.tm_min = minute;

    ++text;
    prev = text;
    skip_max<digit_pred, 2>(text);

    const size_t second_len = text - prev;
    int second = 0;
    switch (second_len) {
      case 2: second += (*(prev + (second_len-2)) - '0') * 10; [[fallthrough]];
      case 1: second += (*(prev + (second_len-1)) - '0') * 1; break;
      default: return false;
    }

    result.tm_sec = second;
  }

  return true;
}

#define RAPIDXML_PARSE_ERROR(what, where) throw parse_error(what, where)

[[noreturn]] static void parse_error_expected_semicolon(const Ch*& text) {
  RAPIDXML_PARSE_ERROR("expected ';'", text);
}
[[noreturn]] static void parse_error_expected_equals(const Ch*& text) {
  RAPIDXML_PARSE_ERROR("expected '='", text);
}
[[noreturn]] static void parse_error_expected_quote(const Ch*& text) {
  RAPIDXML_PARSE_ERROR("expected ' or \"", text);
}
[[noreturn]] static void parse_error_expected_tag_end(const Ch*& text) {
  RAPIDXML_PARSE_ERROR("expected > or />", text);
}
[[noreturn]] static void parse_error_expected_element_name(const Ch*& text) {
  RAPIDXML_PARSE_ERROR("expected element name", text);
}
[[noreturn]] static void parse_error_value_too_long(const Ch*& text) {
  RAPIDXML_PARSE_ERROR("value too long", text);
}

)"";

#ifdef ZUSIXML_SCHEMA_XML_MODE
    out << R""(void expect(const char* expected, const char*& text) {
  if (memcmp(text, expected, strlen(expected)) != 0) {
    RAPIDXML_PARSE_ERROR("Wrong data type", text);
  }
  text += strlen(expected);
}
\n)"";
#endif

    for (const auto& elementType : m_element_types) {
      if (m_used_element_types.find(elementType.get()) == std::end(m_used_element_types)) {
        continue;
      }
      if (m_concrete_element_types.find(elementType.get()) == std::end(m_concrete_element_types)) {
        continue;
      }

      auto allChildren = GetAllChildren(*elementType);
      auto allAttributes = GetAllAttributes(*elementType);

      std::ostringstream parse_children;
      parse_children << "if (false) { (void)parseResult; }\n";

      for (const auto& [curParent, child] : allChildren) {
        if (!IsOnWhitelist(*curParent, child) && m_config.ignore_unknown) {
          continue;
        }

        parse_children << "            else if (name_size == " << child.name.size() << " && !memcmp(name, \"" << child.name << "\", " << child.name.size() << ")) {\n";
        if (!IsOnWhitelist(*curParent, child)) {
          if (child.deprecated()) {
            parse_children << "                // deprecated\n";
          }
          parse_children << "                skip_element(text);\n";
          parse_children << "            }\n";
          continue;
        }
        parse_children << "                " << GetChildStrategy(*curParent, child)->GetParseMemberCode(*curParent, child);
        parse_children << "            }\n";
      }

      parse_children << "            else {\n";
      if (!m_config.ignore_unknown) {
        parse_children << "              std::cerr << \"Unexpected child of node " << elementType->name << ": '\" << std::string_view(name, name_size) << \"'\\n\";\n";
      }
      parse_children << "              skip_element(text);\n";
      parse_children << "            }\n";

      // Generate attribute parsing code
      std::ostringstream parse_attributes;

      bool startWhitespaceSkip = std::none_of(std::begin(allAttributes), std::end(allAttributes), [](const auto& attr) {
          return attr.second.type == AttributeType::String || attr.second.type == AttributeType::FaceIndexes;
      });
      if (startWhitespaceSkip) {
        parse_attributes << "        skip_unlikely<whitespace_pred>(text);\n";
      }

      parse_attributes << "        if (false) { (void)parseResult; }\n";

#ifndef ZUSIXML_SCHEMA_XML_MODE
      // Special treatment for types with WXYZ as attributes
      if (elementType->name == "Vec2") {
        parse_attributes << "        if (name_size == 1 && *name >= 'X' && *name <= 'Y') {\n";
        if (m_config.use_glm) {
          parse_attributes << "          std::array<float Vec2::*, 2> members = {{ &Vec2::x, &Vec2::y }};\n";
        } else {
          parse_attributes << "          std::array<float Vec2::*, 2> members = {{ &Vec2::X, &Vec2::Y }};\n";
        }
        parse_attributes << R""(          parse_float(text, parseResult->*members[*name - 'X']);
          skip_unlikely<whitespace_pred>(text);
        })"" << "\n";
        allAttributes.clear();
      } else if (elementType->name == "Vec3") {
        parse_attributes << "        if (name_size == 1 && *name >= 'X' && *name <= 'Z') {\n";
        if (m_config.use_glm) {
          parse_attributes << "          std::array<float Vec3::*, 3> members = {{ &Vec3::x, &Vec3::y, &Vec3::z }};\n";
        } else {
          parse_attributes << "          std::array<float Vec3::*, 3> members = {{ &Vec3::X, &Vec3::Y, &Vec3::Z }};\n";;
        }
        parse_attributes << R""(          parse_float(text, parseResult->*members[*name - 'X']);
          skip_unlikely<whitespace_pred>(text);
        })"" << "\n";
        allAttributes.clear();
      } else if (elementType->name == "Quaternion") {
        parse_attributes << "        if (name_size == 1 && *name >= 'W' && *name <= 'Z') {\n";
        if (m_config.use_glm) {
          parse_attributes << "          std::array<float Quaternion::*, 4> members = {{ &Quaternion::w, &Quaternion::x, &Quaternion::y, &Quaternion::z }};\n";
        } else {
          parse_attributes << "          std::array<float Quaternion::*, 4> members = {{ &Quaternion::W, &Quaternion::X, &Quaternion::Y, &Quaternion::Z }};\n";
        }
        parse_attributes << R""(          parse_float(text, parseResult->*members[*name - 'W']);
          skip_unlikely<whitespace_pred>(text);
        })"" << "\n";
        allAttributes.clear();
      }
#endif

      for (const auto& [curParent, attr] : allAttributes) {
        if (!IsOnWhitelist(*curParent, attr) && m_config.ignore_unknown) {
          continue;
        }

        parse_attributes << "        else if (name_size == " << attr.name.size() << " && !memcmp(name, \"" << attr.name << "\", " << attr.name.size() << ")) {\n";

        // Skip deprecated attributes and attributes that are not on the whitelist.
        if (!IsOnWhitelist(*curParent, attr)) {
          if (attr.deprecated()) {
            parse_attributes << "          // deprecated\n";
          }
          parse_attributes << "          skip_attribute_value(text, quote);\n";
          parse_attributes << "        }\n";
          continue;
        } else if ((attr.name == "C" || attr.name == "CA" || attr.name == "E")) {
          // Convert deprecated old form of color attributes to new form.
          if (!startWhitespaceSkip) {
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
          }
          parse_attributes << "          uint32_t tmp;\n";
          parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::int_parser<uint32_t, 16, 1, 9>(), tmp);\n";
          parse_attributes << "          parseResult->";
          if (attr.name == "C") {
            parse_attributes << "Cd";
          } else if (attr.name == "CA") {
            parse_attributes << "Ca";
          } else if (attr.name == "E") {
            parse_attributes << "Ce";
          }
          parse_attributes << " = ArgbColor { static_cast<uint8_t>((tmp >> 24) & 0xFF), static_cast<uint8_t>(tmp & 0xFF), static_cast<uint8_t>((tmp >> 8) & 0xFF), static_cast<uint8_t>((tmp >> 16) & 0xFF) };\n";
          parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
          parse_attributes << "        }\n";
          continue;
        }

        switch (attr.type) {
          case AttributeType::Int32:
#ifdef ZUSIXML_SCHEMA_XML_MODE
            parse_attributes << "          expect(\"integer\", text);\n";
            parse_attributes << "          const char* enum_str = \" enum\";\n";
            parse_attributes << "          if (!memcmp(enum_str, text, strlen(enum_str))) {\n";
            parse_attributes << "            text += strlen(enum_str);\n";
            parse_attributes << "          }\n";
#else
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
            }
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::int_, parseResult->" << attr.name << ");\n";
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
#endif
            break;
          case AttributeType::Int64:
#ifdef ZUSIXML_SCHEMA_XML_MODE
            parse_attributes << "          expect(\"integer 64bit\", text);\n";
#else
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
            }
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::long_long, parseResult->" << attr.name << ");\n";
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
#endif
            break;
          case AttributeType::Boolean:
#ifdef ZUSIXML_SCHEMA_XML_MODE
            parse_attributes << "          expect(\"bool\", text);\n";
#else
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
            }
            parse_attributes << "          parseResult->" << attr.name << " = (text[0] == '1');\n";
            parse_attributes << "          ++text;\n";
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
#endif
            break;
          case AttributeType::String:
#ifdef ZUSIXML_SCHEMA_XML_MODE
            parse_attributes << "          expect(\"string\", text);\n";
#else
            parse_attributes << "          parse_string(text, parseResult->" << attr.name << ", quote);\n";
#endif
            break;
          case AttributeType::Float:
#ifdef ZUSIXML_SCHEMA_XML_MODE
            parse_attributes << "          expect(\"single\", text);\n";
            parse_attributes << "          const char* decimal_places_str = \", 6 decimal places\";\n";
            parse_attributes << "          if (!memcmp(decimal_places_str, text, strlen(decimal_places_str))) {\n";
            parse_attributes << "            text += strlen(decimal_places_str);\n";
            parse_attributes << "          }\n";
#else
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
            }
            parse_attributes << "          parse_float(text, parseResult->" << attr.name << ");\n";
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
#endif
            break;
          case AttributeType::DateTime:
#ifdef ZUSIXML_SCHEMA_XML_MODE
            parse_attributes << "          expect(\"date,time\", text);\n";
#else
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
            }
            parse_attributes << "          [[maybe_unused]] bool result = (unlikely(quote == Ch('\\\''))) ?\n";
            parse_attributes << "            parse_datetime<Ch('\\\'')>(text, parseResult->" << attr.name << ") :\n";
            parse_attributes << "            parse_datetime<Ch('\"')>(text, parseResult->" << attr.name << ");\n";
#endif
            break;
          case AttributeType::HexInt32:
#ifdef ZUSIXML_SCHEMA_XML_MODE
            parse_attributes << "          expect(\"D3DColor\", text);\n";
#else
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
            }
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::int_parser<uint32_t, 16, 1, 9>(), parseResult->" << attr.name << ");\n";
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
#endif
            break;
          case AttributeType::ArgbColor:
#ifdef ZUSIXML_SCHEMA_XML_MODE
            parse_attributes << "          expect(\"D3DColor\", text);\n";
#else
            if (!startWhitespaceSkip) {
              parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
            }
            parse_attributes << "          uint32_t tmp;\n";
            parse_attributes << "          boost::spirit::qi::parse(text, static_cast<const char*>(nullptr), boost::spirit::qi::int_parser<uint32_t, 16, 1, 9>(), tmp);\n";
            parse_attributes << "          parseResult->" << attr.name << " = ArgbColor { static_cast<uint8_t>((tmp >> 24) & 0xFF), static_cast<uint8_t>((tmp >> 16) & 0xFF), static_cast<uint8_t>((tmp >> 8) & 0xFF), static_cast<uint8_t>(tmp & 0xFF) };\n";
            parse_attributes << "          skip_unlikely<whitespace_pred>(text);\n";
#endif
            break;
          case AttributeType::FaceIndexes:
#ifdef ZUSIXML_SCHEMA_XML_MODE
            parse_attributes << "          expect(\"string\", text);\n";
#else
            // no whitespace skipping here, Zusi doesn't do that either
            parse_attributes << "          const Ch* values[4];\n";
            parse_attributes << "          values[0] = text;\n";
            parse_attributes << "          while (*text >= '0' && *text <= '9') ++text;\n";
            parse_attributes << "          if (*text != ';') parse_error_expected_semicolon(text);\n";
            parse_attributes << "          ++text;\n";
            parse_attributes << "          values[1] = text;\n";
            parse_attributes << "          while (*text >= '0' && *text <= '9') ++text;\n";
            parse_attributes << "          if (*text != ';') parse_error_expected_semicolon(text);\n";
            parse_attributes << "          ++text;\n";
            parse_attributes << "          values[2] = text;\n";
            parse_attributes << "          while (*text >= '0' && *text <= '9') ++text;\n";
            parse_attributes << "          values[3] = text + 1;\n";
            parse_attributes << "          for (size_t i = 0; i < 3; i++) {\n";
            parse_attributes << "            uint16_t result = 0;\n";
            parse_attributes << "            if (values[i+1] != values[i]) {\n";
            parse_attributes << "              size_t len = values[i+1] - 1 - values[i];\n";
            // Adapted from https://tombarta.wordpress.com/2008/04/23/specializing-atoi/
            parse_attributes << "              switch(len) {  // 16 bit short - max. 5 characters\n";
            parse_attributes << "                case 5: result += (*(values[i] + (len-5)) - '0') * 10000; [[fallthrough]];\n";
            parse_attributes << "                case 4: result += (*(values[i] + (len-4)) - '0') * 1000; [[fallthrough]];\n";
            parse_attributes << "                case 3: result += (*(values[i] + (len-3)) - '0') * 100; [[fallthrough]];\n";
            parse_attributes << "                case 2: result += (*(values[i] + (len-2)) - '0') * 10; [[fallthrough]];\n";
            parse_attributes << "                case 1: result += (*(values[i] + (len-1)) - '0') * 1; [[fallthrough]];\n";
            parse_attributes << "                case 0: break;\n";
            parse_attributes << "                default: parse_error_value_too_long(text);\n";
            parse_attributes << "              }\n";
            parse_attributes << "            }\n";
            parse_attributes << "            parseResult->" << attr.name << "[i] = result;\n";
            parse_attributes << "          }\n";
            parse_attributes << "          if (*text == ';') ++text;\n";
#endif
            break;
        }
        parse_attributes << "        }\n";
      }

      parse_attributes << "        else {\n";
      if (!m_config.ignore_unknown) {
        parse_attributes << "          std::cerr << \"Unexpected attribute of node " << elementType->name << ": '\" << std::string_view(name, name_size) << \"'\\n\";\n";
      }
      parse_attributes << "          skip_attribute_value(text, quote);\n";
      parse_attributes << "        }\n";

      // Generate code for parsing method
      out << R""(  static void parse_element_)"" << elementType->name << "(const Ch *& text, " << elementType->cppName << R""(* parseResult) {

      // For all attributes
      while (attribute_name_pred::test(*text))
      {
          // Extract attribute name
          const Ch *name = text;
          ++text;     // Skip first character of attribute name
          skip<attribute_name_pred>(text);
          const size_t name_size [[maybe_unused]] = text - name;

          // Skip whitespace after attribute name
          skip_unlikely<whitespace_pred>(text);

          // Skip =
          if (*text != Ch('='))
              parse_error_expected_equals(text);
          ++text;

          // Skip whitespace after =
          skip_unlikely<whitespace_pred>(text);

          // Skip quote and remember if it was ' or "
          Ch quote = *text;
          if (quote != Ch('\'') && quote != Ch('"'))
              parse_error_expected_quote(text);
          ++text;

          // Extract attribute value
          )"" << parse_attributes.str() << R""(

          // Make sure that end quote is present
          if (*text != quote)
              parse_error_expected_quote(text);
          ++text;     // Skip quote

          // Skip whitespace after attribute value
          skip<whitespace_pred>(text);
      }

      // Determine ending type
      if ()"" << (allChildren.empty() ? "unlikely(*text == Ch('>'))" : "*text == Ch('>')") << R""()
      {
          ++text;
          parse_node_contents(text, [](const Ch *&text, void* parseResultUntyped) {
              )"" << elementType->cppName << R""(* parseResult = static_cast<)"" << elementType->cppName << R""(*>(parseResultUntyped);
              // Extract element name
              const Ch *name = text;
              skip<node_name_pred>(text);
              if (text == name)
                  parse_error_expected_element_name(text);
              const size_t name_size [[maybe_unused]] = text - name;

              // Skip whitespace between element name and attributes or >
              skip<whitespace_pred>(text);

              )"" << parse_children.str() << R""(
          }, parseResult);
      }
      else if ((text[0] == Ch('/')) && (text[1] == Ch('>')))
      {
          text += 2;
      }
      else
          parse_error_expected_tag_end(text);
    })"" << "\n";

    }
    out << "}  // namespace zusixml\n";
  }

  void ValidateWhitelist() {
    for (const auto& [elementName, whitelistEntries] : m_config.whitelist) {
      const auto it = std::find_if(m_element_types.begin(), m_element_types.end(),
          [&elementName = elementName](const auto& elementTypePtr) { return elementTypePtr->name == elementName; });
      if (it == m_element_types.end()) {
        std::cerr << "Warning: Invalid whitelist entry: " << elementName << " is not an element type name.\n";
        continue;
      }
      for (const auto& entry : whitelistEntries) {
        const auto& children = (*it)->children;
        const auto itChild = std::find_if(children.begin(), children.end(),
            [&entry](const auto& child) { return child.name == entry; });

        const auto& attrs = (*it)->attributes;
        const auto itAttr = std::find_if(attrs.begin(), attrs.end(),
            [&entry](const auto& attr) { return attr.name == entry; });

        if ((itChild == children.end()) && (itAttr == attrs.end())) {
          std::cerr << "Warning: Invalid whitelist entry: " << entry << " is not a child or attribute of " << elementName << "\n";
          continue;
        }

        std::cout << "Whitelist entry: " << elementName << "::" << entry << "\n";
      }
    }
  }

 private:
  const std::vector<std::unique_ptr<ElementType>> m_element_types;
  Config m_config;
  const std::unordered_set<const ElementType*> m_used_element_types;
  const std::unordered_set<const ElementType*> m_concrete_element_types;
  std::unordered_map<const ElementType*, size_t> m_element_type_sizes;

  /** Returns the strategy to use when embedding the given @p child into the given @parentType as a member. */
  std::unique_ptr<ChildStrategy> GetChildStrategy(const ElementType& parentType, const Child& child) const {
    if (child.type != &parentType) {
      if (!child.multiple && child.type->name == "StreckenelementRichtungsInfo") {
        return std::make_unique<OptionalChildStrategy>();
      }
      if (child.type->name == "Vertex"
          || child.type->name == "Face"
          || child.type->name == "Vec2"
          || child.type->name == "Vec3"
          || child.type->name == "Quaternion"
          || child.type->name == "Dateiverknuepfung"
          || child.type->name == "Tastaturzuordnung"
          || child.type->name == "Bremsgewicht"
          || child.type->name == "MatrixEintrag") {
        return std::make_unique<InlineChildStrategy>();
      }
      if (child.multiple && SmallVectorSize(parentType, child) > 0) {
        return std::make_unique<InlineChildStrategy>();
      }
    }
    return std::make_unique<UniquePtrChildStrategy>();
  }

  /** Returns the base type, i.e. the least derived parent type, of the given element type. */
  const ElementType* GetBaseType(const ElementType* type) const {
    while (type->base != nullptr) {
      type = type->base;
    }
    return type;
  }

  std::vector<std::pair<const ElementType*, Child>> GetAllChildren(const ElementType& elementType) const {
    const ElementType* curElementType = &elementType;
    std::vector<std::pair<const ElementType*, Child>> result;
    do {
      for (auto it = std::crbegin(curElementType->children); it != std::crend(curElementType->children); ++it) {
        result.emplace(result.begin(), curElementType, *it);
      }
      curElementType = curElementType->base;
    } while (curElementType);
    return result;
  }

  std::vector<std::pair<const ElementType*, Attribute>> GetAllAttributes(const ElementType& elementType) const {
    const ElementType* curElementType = &elementType;
    std::vector<std::pair<const ElementType*, Attribute>> result;
    do {
      for (const auto& child : curElementType->attributes) {
        result.emplace_back(curElementType, child);
      }
      curElementType = curElementType->base;
    } while (curElementType);
    return result;
  }

  std::unordered_set<const ElementType*> GetUsedElementTypes() const {
    std::unordered_set<const ElementType*> result;
    for (const auto& elementType : m_element_types) {
      if (elementType->name == "Zusi") {
        result.insert(elementType.get());
        assert(elementType->base == nullptr);
      }
      for (const auto& child : elementType->children) {
        if (IsOnWhitelist(*elementType, child)) {
          auto type = child.type;
          do {
            result.insert(type);
            type = type->base;
          } while (type != nullptr);
        }
      }
    }
    return result;
  }

  std::unordered_set<const ElementType*> GetConcreteElementTypes() const {
    std::unordered_set<const ElementType*> result;
    for (const auto& elementType : m_element_types) {
      if (elementType->name == "Zusi") {
        result.insert(elementType.get());
      }
      for (const auto& child : elementType->children) {
        if (IsOnWhitelist(*elementType, child)) {
          result.emplace(child.type);
        }
      }
    }
    return result;
  }

  bool IsOnWhitelist(const ElementType& parentType, const Thing& thing) const {
    if (m_config.whitelist.empty()) {
      return !thing.deprecated() || thing.name == "C" || thing.name == "CA" || thing.name == "E";
    }
    if (parentType.name == "StrElement" && thing.name == "Nr") {  // needed for indexing
      return true;
    }
    if (parentType.name == "ReferenzElemente" && thing.name == "Nr") {  // needed for indexing
      return true;
    }
    if (const auto& it = m_config.whitelist.find(parentType.name); it != m_config.whitelist.end()) {
      return it->second.find(thing.name) != it->second.end();
    }
    return false;
  }
};

class ParserGeneratorBuilder {
 public:
  void AddXsdFile(const fs::path& fileName) {
    fs::path fileNameCanonical = fs::canonical(fileName);

    if (m_docs_by_name.find(fileNameCanonical.string()) != std::end(m_docs_by_name)) {
      return;
    }

    std::cerr << "Loading XSD file: " << fileNameCanonical << std::endl;
    auto document = std::make_unique<pugi::xml_document>();
    auto parse_result = document->load_file(fileNameCanonical.c_str());

    if (!parse_result) {
      std::cerr << "Error loading XSD file: " << fileNameCanonical << "\n";
      return;
    }

    // Load <xs:include>d files and save them for later
    std::vector<fs::path> includes;
    for (const auto& include : document->select_nodes("//xs:include")) {
      fs::path include_path(include.node().attribute("schemaLocation").as_string());
      if (!include_path.is_absolute()) {
        include_path = fileNameCanonical.parent_path() / include_path;
      }
      includes.push_back(include_path);
    }

    // Parse complex types
    for (const auto& complexType : document->select_nodes("//xs:complexType")) {
      ParseComplexType(complexType.node());
    }

    m_docs_by_name.emplace(fileNameCanonical.string(), std::move(document));

    for (const auto& includeFileName : includes) {
      AddXsdFile(includeFileName);
    }
  }

  ParserGenerator Build(Config config) {
    using namespace std::string_literals;
    std::vector<std::unique_ptr<ElementType>> elementTypes;
    for (const auto& [ typeName, elementTypeRaw] : m_element_types) {
      const auto nameCpp = [&config, typeName = typeName]() -> std::string {
        if (config.use_glm && ((typeName == "Vec2") || (typeName == "Vec3") || (typeName == "Quaternion"))) {
          return typeName;
        }
        return "struct "s + typeName;
      }();
      elementTypes.push_back(std::unique_ptr<ElementType>(new ElementType {
          { elementTypeRaw.name, elementTypeRaw.documentation },
          nameCpp,
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
        elementType->children.emplace_back(Child { { childRaw.name, childRaw.documentation }, childType->get(), childRaw.multiple });
      }
    }
    return ParserGenerator(&elementTypes, std::move(config));
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
          elementType->children.push_back(ChildRaw { { childTypeName, GetDocumentation(child) }, childTypeName, multiple });
        } else {
          elementType->children.push_back(ChildRaw { { child.attribute("name").as_string(), GetDocumentation(child) }, child.attribute("type").as_string(), multiple });
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
      std::cerr << "Type " << typeName << " defined twice\n";
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
        std::cerr << "Unknown attribute type '" << attributeTypeString << "' of attribute '" << child.attribute("name").as_string() << "' of complex type '" << typeName << "'\n";
        continue;
      }

      elementType.attributes.push_back(Attribute { { child.attribute("name").as_string(), GetDocumentation(child) }, attributeType });
    }
  }
};

int main(int argc, char** argv) {
  Config config;
  std::vector<std::string> whitelist;
  std::string xsd;
  std::string out_dir;

  po::options_description desc;
  desc.add_options()
    ("help", "show help message")
    ("out-dir", po::value<std::string>(&out_dir), "Root XSD file to process")
    ("ignore-unknown", po::bool_switch(&config.ignore_unknown), "Do not produce an error message on encountering unknown element or attribute names. This speeds up parsing.")
    ("use-glm", po::bool_switch(&config.use_glm), "Use glm::tvec2, glm::tvec3 and glm::tquat for vector and quaternion types.")
    ("whitelist", po::value<std::vector<std::string>>(&whitelist), "Whitelist an element or attribute name to parse, in the form ParentName::Name. Can be specified multiple times. If no whitelist is specified, all known elements and attributes are parsed.")
    ("xsd", po::value<std::string>(&xsd), "Root XSD file to process")
    ;

  po::positional_options_description p;
  p.add("xsd", 1);

  po::variables_map vars;
  po::store(po::command_line_parser(argc, argv). options(desc).positional(p).run(), vars);
  po::notify(vars);

  if (vars.count("help")) {
    std::cerr << desc << "\n";
    return 1;
  }

  for (const auto& entry : whitelist) {
    auto colon_pos = entry.find("::");
    decltype(colon_pos) start = 0;
    if (colon_pos == std::string::npos) {
      std::cerr << "Invalid whitelist entry: \"" << entry << "\"\n";
      continue;
    }
    do {
      const auto next_start = colon_pos + 2;
      const auto next_colon_pos = entry.find("::", next_start);
      config.whitelist[entry.substr(start, colon_pos - start)].insert(entry.substr(next_start, next_colon_pos - next_start));
      start = next_start;
      colon_pos = next_colon_pos;
    } while (colon_pos != std::string::npos);
  }

  ParserGeneratorBuilder builder;
  builder.AddXsdFile(xsd);

  ParserGenerator generator = builder.Build(config);

  generator.ValidateWhitelist();

  ofstream out_types_fwd(fs::path(out_dir) / "zusi_types_fwd.hpp");
  generator.GenerateTypeDeclarations(out_types_fwd);

  ofstream out_types(fs::path(out_dir) / "zusi_types.hpp");
  generator.GenerateTypeIncludes(out_types);
  generator.GenerateTypeDefinitions(out_types);

  ofstream out_parser_fwd(fs::path(out_dir) / "zusi_parser_fwd.hpp");
  generator.GenerateParseFunctionDeclarations(out_parser_fwd);

  ofstream out_parser(fs::path(out_dir) / "zusi_parser.hpp");
  generator.GenerateParseFunctionDefinitions(out_parser);
}
