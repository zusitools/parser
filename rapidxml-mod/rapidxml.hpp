#ifndef RAPIDXML_HPP_INCLUDED
#define RAPIDXML_HPP_INCLUDED

// Copyright (C) 2006, 2009 Marcin Kalicinski
// Version 1.13
// Revision $DateTime: 2009/05/13 01:46:17 $
//! \file rapidxml.hpp This file contains rapidxml parser and DOM implementation

// If standard library is disabled, user must provide implementations of required functions and typedefs
#if !defined(RAPIDXML_NO_STDLIB)
    #include <cstdlib>      // For std::size_t
    #include <cassert>      // For assert
    #include <memory>       // For std::unique_ptr
#endif

// On MSVC, disable "conditional expression is constant" warning (level 4). 
// This warning is almost impossible to avoid with certain types of templated code
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable:4127)   // Conditional expression is constant
#endif

///////////////////////////////////////////////////////////////////////////
// RAPIDXML_PARSE_ERROR
    
#if defined(RAPIDXML_NO_EXCEPTIONS)

#define RAPIDXML_PARSE_ERROR(what, where) { parse_error_handler(what, where); assert(0); }

namespace rapidxml
{
    //! When exceptions are disabled by defining RAPIDXML_NO_EXCEPTIONS, 
    //! this function is called to notify user about the error.
    //! It must be defined by the user.
    //! <br><br>
    //! This function cannot return. If it does, the results are undefined.
    //! <br><br>
    //! A very simple definition might look like that:
    //! <pre>
    //! void %rapidxml::%parse_error_handler(const char *what, void *where)
    //! {
    //!     std::cout << "Parse error: " << what << "\n";
    //!     std::abort();
    //! }
    //! </pre>
    //! \param what Human readable description of the error.
    //! \param where Pointer to character data where error was detected.
    void parse_error_handler(const char *what, void *where);
}

#else
    
#include <exception>    // For std::exception

#define RAPIDXML_PARSE_ERROR(what, where) throw parse_error(what, where)

namespace rapidxml
{
    using Ch = const char;
    using parse_function = void (*)(Ch *&);

    //! Parse error exception. 
    //! This exception is thrown by the parser when an error occurs. 
    //! Use what() function to get human-readable error message. 
    //! Use where() function to get a pointer to position within source text where error was detected.
    //! <br><br>
    //! If throwing exceptions by the parser is undesirable, 
    //! it can be disabled by defining RAPIDXML_NO_EXCEPTIONS macro before rapidxml.hpp is included.
    //! This will cause the parser to call rapidxml::parse_error_handler() function instead of throwing an exception.
    //! This function must be defined by the user.
    //! <br><br>
    //! This class derives from <code>std::exception</code> class.
    class parse_error: public std::exception
    {
    
    public:
    
        //! Constructs parse error
        parse_error(const char *what, const void *where)
            : m_what(what)
            , m_where(where)
        {
        }

        //! Gets human readable description of error.
        //! \return Pointer to null terminated description of the error.
        virtual const char *what() const throw()
        {
            return m_what;
        }

        //! Gets pointer to character data where error happened.
        //! Ch should be the same as char type of xml_document that produced the error.
        //! \return Pointer to location within the parsed string where error occured.
        Ch *where() const
        {
            return reinterpret_cast<Ch *>(m_where);
        }

    private:  

        const char *m_what;
        const void *m_where;

    };
}

#endif

namespace rapidxml
{
    ///////////////////////////////////////////////////////////////////////
    // Internals

    //! \cond internal
    namespace internal
    {

        // Struct that contains lookup tables for the parser
        // It must be a template to allow correct linking (because it has static data members, which are defined in a header file).
        template<int Dummy>
        struct lookup_tables
        {
            static const unsigned char lookup_whitespace[256];              // Whitespace table
            static const unsigned char lookup_node_name[256];               // Node name table
            static const unsigned char lookup_text[256];                    // Text table
            static const unsigned char lookup_text_pure_no_ws[256];         // Text table
            static const unsigned char lookup_text_pure_with_ws[256];       // Text table
            static const unsigned char lookup_attribute_name[256];          // Attribute name table
            static const unsigned char lookup_attribute_data_1[256];        // Attribute data table with single quote
            static const unsigned char lookup_attribute_data_1_pure[256];   // Attribute data table with single quote
            static const unsigned char lookup_attribute_data_2[256];        // Attribute data table with double quotes
            static const unsigned char lookup_attribute_data_2_pure[256];   // Attribute data table with double quotes
            static const unsigned char lookup_digits[256];                  // Digits
            static const unsigned char lookup_upcase[256];                  // To uppercase conversion table for ASCII characters
        };

        // Find length of the string
        inline std::size_t measure(const Ch *p)
        {
            const Ch *tmp = p;
            while (*tmp) 
                ++tmp;
            return tmp - p;
        }

        // Compare strings for equality
        inline bool compare(const Ch *p1, std::size_t size1, const Ch *p2, std::size_t size2, bool case_sensitive)
        {
            if (size1 != size2)
                return false;
            if (case_sensitive)
            {
                for (const Ch *end = p1 + size1; p1 < end; ++p1, ++p2)
                    if (*p1 != *p2)
                        return false;
            }
            else
            {
                for (const Ch *end = p1 + size1; p1 < end; ++p1, ++p2)
                    if (lookup_tables<0>::lookup_upcase[static_cast<unsigned char>(*p1)] != lookup_tables<0>::lookup_upcase[static_cast<unsigned char>(*p2)])
                        return false;
            }
            return true;
        }
    }
    //! \endcond
    
    // Forward declarations.
    static void parse_bom(Ch *&text);
    static void parse_xml_declaration(Ch *&text);
    static void parse_comment(Ch *&text);
    static void parse_doctype(Ch *&text);
    static void parse_pi(Ch *&text);
    static void parse_cdata(Ch *&text);

    template<typename Result>
    static void parse_element(Ch *&text);

    static void parse_node_contents(Ch *&text, parse_function parse_element_function, void* result);

    template<typename Result>
    static void parse_node_attributes(Ch *&text);
    
    ///////////////////////////////////////////////////////////////////////
    // Internal character utility functions
    
    // Detect whitespace character
    struct whitespace_pred
    {
        static unsigned char test(Ch ch)
        {
            return internal::lookup_tables<0>::lookup_whitespace[static_cast<unsigned char>(ch)];
        }
    };

    // Detect node name character
    struct node_name_pred
    {
        static unsigned char test(Ch ch)
        {
            return internal::lookup_tables<0>::lookup_node_name[static_cast<unsigned char>(ch)];
        }
    };

    // Detect attribute name character
    struct attribute_name_pred
    {
        static unsigned char test(Ch ch)
        {
            return internal::lookup_tables<0>::lookup_attribute_name[static_cast<unsigned char>(ch)];
        }
    };

    // Detect text character (PCDATA)
    struct text_pred
    {
        static unsigned char test(Ch ch)
        {
            return internal::lookup_tables<0>::lookup_text[static_cast<unsigned char>(ch)];
        }
    };

    // Detect text character (PCDATA) that does not require processing
    struct text_pure_no_ws_pred
    {
        static unsigned char test(Ch ch)
        {
            return internal::lookup_tables<0>::lookup_text_pure_no_ws[static_cast<unsigned char>(ch)];
        }
    };

    // Detect text character (PCDATA) that does not require processing
    struct text_pure_with_ws_pred
    {
        static unsigned char test(Ch ch)
        {
            return internal::lookup_tables<0>::lookup_text_pure_with_ws[static_cast<unsigned char>(ch)];
        }
    };

    // Detect attribute value character
    template<Ch Quote>
    struct attribute_value_pred
    {
        static unsigned char test(Ch ch)
        {
            if (Quote == Ch('\''))
                return internal::lookup_tables<0>::lookup_attribute_data_1[static_cast<unsigned char>(ch)];
            if (Quote == Ch('\"'))
                return internal::lookup_tables<0>::lookup_attribute_data_2[static_cast<unsigned char>(ch)];
            return 0;       // Should never be executed, to avoid warnings on Comeau
        }
    };

    // Detect attribute value character
    template<Ch Quote>
    struct attribute_value_pure_pred
    {
        static unsigned char test(Ch ch)
        {
            if (Quote == Ch('\''))
                return internal::lookup_tables<0>::lookup_attribute_data_1_pure[static_cast<unsigned char>(ch)];
            if (Quote == Ch('\"'))
                return internal::lookup_tables<0>::lookup_attribute_data_2_pure[static_cast<unsigned char>(ch)];
            return 0;       // Should never be executed, to avoid warnings on Comeau
        }
    };

    // Insert coded character, using UTF8 or 8-bit ASCII
    static void insert_coded_character(std::remove_const_t<Ch> *&text, unsigned long code)
    {
        // Insert UTF8 sequence
        if (code < 0x80)    // 1 byte sequence
        {
                text[0] = static_cast<unsigned char>(code);
            text += 1;
        }
        else if (code < 0x800)  // 2 byte sequence
        {
                text[1] = static_cast<unsigned char>((code | 0x80) & 0xBF); code >>= 6;
                text[0] = static_cast<unsigned char>(code | 0xC0);
            text += 2;
        }
            else if (code < 0x10000)    // 3 byte sequence
        {
                text[2] = static_cast<unsigned char>((code | 0x80) & 0xBF); code >>= 6;
                text[1] = static_cast<unsigned char>((code | 0x80) & 0xBF); code >>= 6;
                text[0] = static_cast<unsigned char>(code | 0xE0);
            text += 3;
        }
            else if (code < 0x110000)   // 4 byte sequence
        {
                text[3] = static_cast<unsigned char>((code | 0x80) & 0xBF); code >>= 6;
                text[2] = static_cast<unsigned char>((code | 0x80) & 0xBF); code >>= 6;
                text[1] = static_cast<unsigned char>((code | 0x80) & 0xBF); code >>= 6;
                text[0] = static_cast<unsigned char>(code | 0xF0);
            text += 4;
        }
        else    // Invalid, only codes up to 0x10FFFF are allowed in Unicode
        {
            RAPIDXML_PARSE_ERROR("invalid numeric character entity", text);
        }
    }

    // Skip characters until predicate evaluates to true
    template<class StopPred>
    static void skip(Ch *&text)
    {
        Ch *tmp = text;
        while (StopPred::test(*tmp))
            ++tmp;
        text = tmp;
    }

    // Skip characters until predicate evaluates to true while
    // replacing XML character entity references with proper characters (&apos; &amp; &quot; &lt; &gt; &#...;)
    template<class StopPred, class StopPredPure>
    static size_t copy_and_expand_character_refs(Ch *&src, std::remove_const_t<Ch> *dest)
    {
        Ch *dest_start = dest;

        // Use simple skip until first modification is detected
        while (StopPredPure::test(*src))
            *dest++ = *src++;

        // Use translation skip
        while (StopPred::test(*src))
        {
            // Test if replacement is needed
            if (src[0] == Ch('&'))
            {
                switch (src[1])
                {

                // &amp; &apos;
                case Ch('a'): 
                    if (src[2] == Ch('m') && src[3] == Ch('p') && src[4] == Ch(';'))
                    {
                        *dest = Ch('&');
                        ++dest;
                        src += 5;
                        continue;
                    }
                    if (src[2] == Ch('p') && src[3] == Ch('o') && src[4] == Ch('s') && src[5] == Ch(';'))
                    {
                        *dest = Ch('\'');
                        ++dest;
                        src += 6;
                        continue;
                    }
                    break;

                // &quot;
                case Ch('q'): 
                    if (src[2] == Ch('u') && src[3] == Ch('o') && src[4] == Ch('t') && src[5] == Ch(';'))
                    {
                        *dest = Ch('"');
                        ++dest;
                        src += 6;
                        continue;
                    }
                    break;

                // &gt;
                case Ch('g'): 
                    if (src[2] == Ch('t') && src[3] == Ch(';'))
                    {
                        *dest = Ch('>');
                        ++dest;
                        src += 4;
                        continue;
                    }
                    break;

                // &lt;
                case Ch('l'): 
                    if (src[2] == Ch('t') && src[3] == Ch(';'))
                    {
                        *dest = Ch('<');
                        ++dest;
                        src += 4;
                        continue;
                    }
                    break;

                // &#...; - assumes ASCII
                case Ch('#'): 
                    if (src[2] == Ch('x'))
                    {
                        unsigned long code = 0;
                        src += 3;   // Skip &#x
                        while (1)
                        {
                            unsigned char digit = internal::lookup_tables<0>::lookup_digits[static_cast<unsigned char>(*src)];
                            if (digit == 0xFF)
                                break;
                            code = code * 16 + digit;
                            ++src;
                        }
                        insert_coded_character(dest, code);    // Put character in output
                    }
                    else
                    {
                        unsigned long code = 0;
                        src += 2;   // Skip &#
                        while (1)
                        {
                            unsigned char digit = internal::lookup_tables<0>::lookup_digits[static_cast<unsigned char>(*src)];
                            if (digit == 0xFF)
                                break;
                            code = code * 10 + digit;
                            ++src;
                        }
                        insert_coded_character(dest, code);    // Put character in output
                    }
                    if (*src == Ch(';'))
                        ++src;
                    else
                        RAPIDXML_PARSE_ERROR("expected ;", src);
                    continue;

                // Something else
                default:
                    // Ignore, just copy '&' verbatim
                    break;

                }
            }

            // No replacement, only copy character
            *dest++ = *src++;

        }

        return dest - dest_start;
    }

    //! Parses zero-terminated XML string according to given flags.
    //! Passed string will be modified by the parser, unless rapidxml::parse_non_destructive flag is used.
    //! The string must persist for the lifetime of the document.
    //! In case of error, rapidxml::parse_error exception will be thrown.
    //! <br><br>
    //! If you want to parse contents of a file, you must first load the file into the memory, and pass pointer to its beginning.
    //! Make sure that data is zero-terminated.
    //! <br><br>
    //! Document can be parsed into multiple times. 
    //! Each new call to parse removes previous nodes and attributes (if any), but does not clear memory pool.
    //! \param text XML data to parse; pointer is non-const to denote fact that this data may be modified by the parser.
    template<typename Result>
    static std::unique_ptr<Result> parse(Ch *text)
    {
        assert(text);
        std::unique_ptr<Result> result { nullptr };
        
        // Parse BOM, if any
        parse_bom(text);
        
        // Parse children
        while (1)
        {
            // Skip whitespace before node
            skip<whitespace_pred>(text);
            if (*text == 0)
                break;

            // Parse and append new child
            if (*text == Ch('<'))
            {
                ++text;     // Skip '<'
                result.reset(new Result());

                // BEGIN INLINING parse_node
                // Parse proper node type
                switch (text[0])
                {

                // <...
                default:
                {
                    // Parse and append element node
                    // BEGIN INLINING parse_element_function
                    // Extract element name
                    Ch *name = text;
                    skip<node_name_pred>(text);
                    if (text == name)
                        RAPIDXML_PARSE_ERROR("expected element name", text);

                    // Skip whitespace between element name and attributes or >
                    skip<whitespace_pred>(text);

                    size_t name_size = text - name;
                    switch (name_size) {
                      /* TODO: autogenerated code here */
                      default: {
                        parse_element<Result>(text);
                        break;
                      }
                    }
                    // END INLINING parse_element_function
                    break;
                }

                // <?...
                case Ch('?'): 
                    ++text;     // Skip ?
                    if ((text[0] == Ch('x') || text[0] == Ch('X')) &&
                        (text[1] == Ch('m') || text[1] == Ch('M')) && 
                        (text[2] == Ch('l') || text[2] == Ch('L')) &&
                        whitespace_pred::test(text[3]))
                    {
                        // '<?xml ' - xml declaration
                        text += 4;      // Skip 'xml '
                        parse_xml_declaration(text);
                        break;
                    }
                    else
                    {
                        // Parse PI
                        parse_pi(text);
                        break;
                    }

                // <!...
                case Ch('!'): 

                    // Parse proper subset of <! node
                    switch (text[1])    
                    {

                    // <!-
                    case Ch('-'):
                        if (text[2] == Ch('-'))
                        {
                            // '<!--' - xml comment
                            text += 3;     // Skip '!--'
                            parse_comment(text);
                            break;
                        }
                        break;

                    // <![
                    case Ch('['):
                        if (text[2] == Ch('C') && text[3] == Ch('D') && text[4] == Ch('A') && 
                            text[5] == Ch('T') && text[6] == Ch('A') && text[7] == Ch('['))
                        {
                            // '<![CDATA[' - cdata
                            text += 8;     // Skip '![CDATA['
                            parse_cdata(text);
                            break;
                        }
                        break;

                    // <!D
                    case Ch('D'):
                        if (text[2] == Ch('O') && text[3] == Ch('C') && text[4] == Ch('T') && 
                            text[5] == Ch('Y') && text[6] == Ch('P') && text[7] == Ch('E') && 
                            whitespace_pred::test(text[8]))
                        {
                            // '<!DOCTYPE ' - doctype
                            text += 9;      // skip '!DOCTYPE '
                            parse_doctype(text);
                            break;
                        }

                    }   // switch

                    // Attempt to skip other, unrecognized node types starting with <!
                    ++text;     // Skip !
                    while (*text != Ch('>'))
                    {
                        if (*text == 0)
                            RAPIDXML_PARSE_ERROR("unexpected end of data", text);
                        ++text;
                    }
                    ++text;     // Skip '>'
                    break;     // No node recognized

                }
                // END INLINING parse_node
            }
            else
                RAPIDXML_PARSE_ERROR("expected <", text);
        }

        return result;
    }

    ///////////////////////////////////////////////////////////////////////
    // Internal parsing functions
    
    // Parse BOM, if any
    static void parse_bom(Ch *&text)
    {
        // UTF-8?
        if (static_cast<unsigned char>(text[0]) == 0xEF && 
            static_cast<unsigned char>(text[1]) == 0xBB && 
            static_cast<unsigned char>(text[2]) == 0xBF)
        {
            text += 3;      // Skup utf-8 bom
        }
    }

    // Parse XML declaration (<?xml...)
    static void parse_xml_declaration(Ch *&text)
    {
        // Skip until end of declaration
        while (text[0] != Ch('?') || text[1] != Ch('>'))
        {
            if (!text[0])
                RAPIDXML_PARSE_ERROR("unexpected end of data", text);
            ++text;
        }
        text += 2;    // Skip '?>'
    }

    // Parse XML comment (<!--...)
    static void parse_comment(Ch *&text)
    {
        // Skip until end of comment
        while (text[0] != Ch('-') || text[1] != Ch('-') || text[2] != Ch('>'))
        {
            if (!text[0])
                RAPIDXML_PARSE_ERROR("unexpected end of data", text);
            ++text;
        }
        text += 3;     // Skip '-->'
    }

    // Parse DOCTYPE
    static void parse_doctype(Ch *&text)
    {
        // Remember value start
        Ch *value = text;

        // Skip to >
        while (*text != Ch('>'))
        {
            // Determine character type
            switch (*text)
            {
            
            // If '[' encountered, scan for matching ending ']' using naive algorithm with depth
            // This works for all W3C test files except for 2 most wicked
            case Ch('['):
            {
                ++text;     // Skip '['
                int depth = 1;
                while (depth > 0)
                {
                    switch (*text)
                    {
                        case Ch('['): ++depth; break;
                        case Ch(']'): --depth; break;
                        case 0: RAPIDXML_PARSE_ERROR("unexpected end of data", text);
                    }
                    ++text;
                }
                break;
            }
            
            // Error on end of text
            case Ch('\0'):
                RAPIDXML_PARSE_ERROR("unexpected end of data", text);
            
            // Other character, skip it
            default:
                ++text;

            }
        }
        
        text += 1;      // skip '>'
    }

    // Parse PI
    static void parse_pi(Ch *&text)
    {
        // Skip to '?>'
        while (text[0] != Ch('?') || text[1] != Ch('>'))
        {
            if (*text == Ch('\0'))
                RAPIDXML_PARSE_ERROR("unexpected end of data", text);
            ++text;
        }
        text += 2;    // Skip '?>'
    }

    // Parse and append data
    // Return character that ends data.
    // This is necessary because this character might have been overwritten by a terminating 0
    static Ch parse_and_append_data(Ch *&text, Ch *contents_start)
    {
        // Backup to contents start if whitespace trimming is disabled
        text = contents_start;     
        
        // Skip until end of data
        Ch *value = text;
        skip<text_pred>(text);

        // Return character that ends data
        return *text;
    }

    // Parse CDATA
    static void parse_cdata(Ch *&text)
    {
        // Skip until end of cdata
        while (text[0] != Ch(']') || text[1] != Ch(']') || text[2] != Ch('>'))
        {
            if (!text[0])
                RAPIDXML_PARSE_ERROR("unexpected end of data", text);
            ++text;
        }
        text += 3;      // Skip ]]>
    }
    
    // Parse element node
    template <typename Result>
    static void parse_element(Ch *&text)
    {
        // Parse attributes, if any
        parse_node_attributes<Result>(text);

        // Determine ending type
        if (*text == Ch('>'))
        {
            ++text;
            parse_node_contents(text, [](Ch *&text) {
              // Extract element name
              Ch *name = text;
              skip<node_name_pred>(text);
              if (text == name)
                  RAPIDXML_PARSE_ERROR("expected element name", text);

              // Skip whitespace between element name and attributes or >
              skip<whitespace_pred>(text);

              size_t name_size = text - name;
              switch (name_size) {
                /* TODO: autogenerated code here */
                default: {
                  parse_element<void>(text);
                  break;
                }
              }
            });
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
    }

    // Parse contents of the node - children, data etc.
    static void parse_node_contents(Ch *&text, parse_function parse_element_function)
    {
        // For all children and text
        while (1)
        {
            // Skip whitespace between > and node contents
            Ch *contents_start = text;      // Store start of node contents before whitespace is skipped
            skip<whitespace_pred>(text);
            std::remove_const_t<Ch> next_char = *text;

        // After data nodes, instead of continuing the loop, control jumps here.
        // This is because zero termination inside parse_and_append_data() function
        // would wreak havoc with the above code.
        // Also, skipping whitespace after data nodes is unnecessary.
        after_data_node:    
            
            // Determine what comes next: node closing, child node, data node, or 0?
            switch (next_char)
            {
            
            // Node closing or child node
            case Ch('<'):
                if (text[1] == Ch('/'))
                {
                    // Node closing
                    text += 2;      // Skip '</'
                    // No validation, just skip name
                    skip<node_name_pred>(text);
                    // Skip remaining whitespace after node name
                    skip<whitespace_pred>(text);
                    if (*text != Ch('>'))
                        RAPIDXML_PARSE_ERROR("expected >", text);
                    ++text;     // Skip '>'
                    return;     // Node closed, finished parsing contents
                }
                else
                {
                    // Child node
                    ++text;     // Skip '<'

                    // BEGIN INLINING parse_node
                    // Parse proper node type
                    switch (text[0])
                    {

                    // <...
                    default: 
                    {
                        // Parse and append element node
                        // BEGIN INLINING parse_element_function
                        // Extract element name
                        Ch *name = text;
                        skip<node_name_pred>(text);
                        if (text == name)
                            RAPIDXML_PARSE_ERROR("expected element name", text);

                        // Skip whitespace between element name and attributes or >
                        skip<whitespace_pred>(text);

                        size_t name_size = text - name;
                        switch (name_size) {
                          /* TODO: autogenerated code here */
                          default: {
                            parse_element<void>(text);
                            break;
                          }
                        }
                        // END INLINING parse_element_function
                        continue;
                    }

                    // <?...
                    case Ch('?'): 
                        ++text;     // Skip ?
                        if ((text[0] == Ch('x') || text[0] == Ch('X')) &&
                            (text[1] == Ch('m') || text[1] == Ch('M')) && 
                            (text[2] == Ch('l') || text[2] == Ch('L')) &&
                            whitespace_pred::test(text[3]))
                        {
                            // '<?xml ' - xml declaration
                            text += 4;      // Skip 'xml '
                            parse_xml_declaration(text);
                            continue;
                        }
                        else
                        {
                            // Parse PI
                            parse_pi(text);
                            continue;
                        }

                    // <!...
                    case Ch('!'): 

                        // Parse proper subset of <! node
                        switch (text[1])    
                        {

                        // <!-
                        case Ch('-'):
                            if (text[2] == Ch('-'))
                            {
                                // '<!--' - xml comment
                                text += 3;     // Skip '!--'
                                parse_comment(text);
                                continue;
                            }
                            break;

                        // <![
                        case Ch('['):
                            if (text[2] == Ch('C') && text[3] == Ch('D') && text[4] == Ch('A') && 
                                text[5] == Ch('T') && text[6] == Ch('A') && text[7] == Ch('['))
                            {
                                // '<![CDATA[' - cdata
                                text += 8;     // Skip '![CDATA['
                                parse_cdata(text);
                                continue;
                            }
                            break;

                        // <!D
                        case Ch('D'):
                            if (text[2] == Ch('O') && text[3] == Ch('C') && text[4] == Ch('T') && 
                                text[5] == Ch('Y') && text[6] == Ch('P') && text[7] == Ch('E') && 
                                whitespace_pred::test(text[8]))
                            {
                                // '<!DOCTYPE ' - doctype
                                text += 9;      // skip '!DOCTYPE '
                                parse_doctype(text);
                                continue;
                            }

                        }   // switch

                        // Attempt to skip other, unrecognized node types starting with <!
                        ++text;     // Skip !
                        while (*text != Ch('>'))
                        {
                            if (*text == 0)
                                RAPIDXML_PARSE_ERROR("unexpected end of data", text);
                            ++text;
                        }
                        ++text;     // Skip '>'
                        continue;   // No node recognized

                    }
                    // END INLINING parse_node
                }
                break;

            // End of data - error
            case Ch('\0'):
                RAPIDXML_PARSE_ERROR("unexpected end of data", text);

            // Data node
            default:
                next_char = parse_and_append_data(text, contents_start);
                goto after_data_node;   // Bypass regular processing after data nodes

            }
        }
    }
    
    // Parse XML attributes of the node
    template <typename Result>
    static void parse_node_attributes(Ch *&text)
    {
        // For all attributes 
        while (attribute_name_pred::test(*text))
        {
            // Extract attribute name
            Ch *name = text;
            ++text;     // Skip first character of attribute name
            skip<attribute_name_pred>(text);
            size_t attribute_size = text - name;

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
            switch (attribute_size) {
              /* TODO autogenerated code here */
              default: {
                if (quote == Ch('\''))
                    skip<attribute_value_pred<Ch('\'')>>(text);
                else
                    skip<attribute_value_pred<Ch('"')>>(text);
                break;
              }
            }
            
            // Make sure that end quote is present
            if (*text != quote)
                RAPIDXML_PARSE_ERROR("expected ' or \"", text);
            ++text;     // Skip quote

            // Skip whitespace after attribute value
            skip<whitespace_pred>(text);
        }
    }

    //! \cond internal
    namespace internal
    {

        // Whitespace (space \n \r \t)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_whitespace[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  0,  0,  1,  0,  0,  // 0
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 1
             1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 2
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 3
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 4
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 5
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 6
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 7
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 8
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 9
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // A
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // B
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // C
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // D
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // E
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0   // F
        };

        // Node name (anything but space \n \r \t / > ? \0)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_node_name[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  1,  1,  0,  1,  1,  // 0
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
             0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  // 2
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  // 3
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 8
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 9
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // A
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // B
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // C
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // D
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // E
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1   // F
        };

        // Text (i.e. PCDATA) (anything but < \0)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_text[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 0
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 2
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  1,  1,  1,  // 3
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 8
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 9
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // A
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // B
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // C
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // D
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // E
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1   // F
        };

        // Text (i.e. PCDATA) that does not require processing when ws normalization is disabled 
        // (anything but < \0 &)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_text_pure_no_ws[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 0
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
             1,  1,  1,  1,  1,  1,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 2
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  1,  1,  1,  // 3
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 8
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 9
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // A
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // B
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // C
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // D
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // E
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1   // F
        };

        // Text (i.e. PCDATA) that does not require processing when ws normalizationis is enabled
        // (anything but < \0 & space \n \r \t)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_text_pure_with_ws[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  1,  1,  0,  1,  1,  // 0
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
             0,  1,  1,  1,  1,  1,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 2
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  1,  1,  1,  // 3
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 8
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 9
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // A
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // B
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // C
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // D
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // E
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1   // F
        };

        // Attribute name (anything but space \n \r \t / < > = ? ! \0)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_attribute_name[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  1,  1,  0,  1,  1,  // 0
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
             0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  // 2
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  // 3
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 8
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 9
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // A
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // B
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // C
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // D
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // E
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1   // F
        };

        // Attribute data with single quote (anything but ' \0)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_attribute_data_1[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 0
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
             1,  1,  1,  1,  1,  1,  1,  0,  1,  1,  1,  1,  1,  1,  1,  1,  // 2
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 3
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 8
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 9
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // A
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // B
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // C
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // D
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // E
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1   // F
        };

        // Attribute data with single quote that does not require processing (anything but ' \0 &)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_attribute_data_1_pure[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 0
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
             1,  1,  1,  1,  1,  1,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  // 2
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 3
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 8
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 9
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // A
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // B
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // C
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // D
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // E
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1   // F
        };

        // Attribute data with double quote (anything but " \0)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_attribute_data_2[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 0
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
             1,  1,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 2
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 3
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 8
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 9
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // A
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // B
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // C
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // D
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // E
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1   // F
        };

        // Attribute data with double quote that does not require processing (anything but " \0 &)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_attribute_data_2_pure[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 0
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 1
             1,  1,  0,  1,  1,  1,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 2
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 3
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 4
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 5
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 6
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 7
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 8
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 9
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // A
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // B
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // C
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // D
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // E
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1   // F
        };

        // Digits (dec and hex, 255 denotes end of numeric character reference)
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_digits[256] = 
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // 0
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // 1
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // 2
             0,  1,  2,  3,  4,  5,  6,  7,  8,  9,255,255,255,255,255,255,  // 3
           255, 10, 11, 12, 13, 14, 15,255,255,255,255,255,255,255,255,255,  // 4
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // 5
           255, 10, 11, 12, 13, 14, 15,255,255,255,255,255,255,255,255,255,  // 6
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // 7
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // 8
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // 9
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // A
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // B
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // C
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // D
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  // E
           255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255   // F
        };
    
        // Upper case conversion
        template<int Dummy>
        const unsigned char lookup_tables<Dummy>::lookup_upcase[256] = 
        {
          // 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  A   B   C   D   E   F
           0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,   // 0
           16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,   // 1
           32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,   // 2
           48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,   // 3
           64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,   // 4
           80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,   // 5
           96, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,   // 6
           80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 123,124,125,126,127,  // 7
           128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,  // 8
           144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,  // 9
           160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,  // A
           176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,  // B
           192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,  // C
           208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,  // D
           224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,  // E
           240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255   // F
        };
    }
    //! \endcond

}

// Undefine internal macros
#undef RAPIDXML_PARSE_ERROR

// On MSVC, restore warnings state
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#endif
