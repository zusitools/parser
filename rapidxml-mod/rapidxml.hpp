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
    #include <new>          // For placement new
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
        template<class Ch>
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

///////////////////////////////////////////////////////////////////////////
// Pool sizes

#ifndef RAPIDXML_STATIC_POOL_SIZE
    // Size of static memory block of memory_pool.
    // Define RAPIDXML_STATIC_POOL_SIZE before including rapidxml.hpp if you want to override the default value.
    // No dynamic memory allocations are performed by memory_pool until static memory is exhausted.
    #define RAPIDXML_STATIC_POOL_SIZE (64 * 1024)
#endif

#ifndef RAPIDXML_DYNAMIC_POOL_SIZE
    // Size of dynamic memory block of memory_pool.
    // Define RAPIDXML_DYNAMIC_POOL_SIZE before including rapidxml.hpp if you want to override the default value.
    // After the static block is exhausted, dynamic blocks with approximately this size are allocated by memory_pool.
    #define RAPIDXML_DYNAMIC_POOL_SIZE (64 * 1024)
#endif

#ifndef RAPIDXML_ALIGNMENT
    // Memory allocation alignment.
    // Define RAPIDXML_ALIGNMENT before including rapidxml.hpp if you want to override the default value, which is the size of pointer.
    // All memory allocations for nodes, attributes and strings will be aligned to this value.
    // This must be a power of 2 and at least 1, otherwise memory_pool will not work.
    #define RAPIDXML_ALIGNMENT sizeof(void *)
#endif

namespace rapidxml
{
    // Forward declarations
    template<class Ch> class xml_document;
    
    //! Enumeration listing all node types produced by the parser.
    enum node_type
    {
        node_document,      //!< A document node. Name and value are empty.
        node_element,       //!< An element node. Name contains element name. Value contains text of first data node.
        node_data,          //!< A data node. Name is empty. Value contains data text.
        node_cdata,         //!< A CDATA node. Name is empty. Value contains data text.
        node_comment,       //!< A comment node. Name is empty. Value contains comment text.
        node_declaration,   //!< A declaration node. Name and value are empty. Declaration parameters (version, encoding and standalone) are in node attributes.
        node_doctype,       //!< A DOCTYPE node. Name is empty. Value contains DOCTYPE text.
        node_pi             //!< A PI node. Name contains target. Value contains instructions.
    };

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
        template<class Ch>
        inline std::size_t measure(const Ch *p)
        {
            const Ch *tmp = p;
            while (*tmp) 
                ++tmp;
            return tmp - p;
        }

        // Compare strings for equality
        template<class Ch>
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

    ///////////////////////////////////////////////////////////////////////////
    // XML base

    //! Base class for xml_node and xml_attribute implementing common functions: 
    //! name(), name_size(), value(), value_size() and parent().
    //! \param Ch Character type to use
    template<class Ch = char>
    class xml_base
    {

    public:
        
        ///////////////////////////////////////////////////////////////////////////
        // Construction & destruction
    
        // Construct a base with empty name, value and parent
        xml_base()
            : m_name(0)
            , m_value(0)
        {
        }

        ///////////////////////////////////////////////////////////////////////////
        // Node data access
    
        //! Gets name of the node. 
        //! Interpretation of name depends on type of node.
        //! Note that name will not be zero-terminated.
        //! <br><br>
        //! Use name_size() function to determine length of the name.
        //! \return Name of node, or empty string if node has no name.
        Ch *name() const
        {
            return m_name ? m_name : nullstr();
        }

        //! Gets size of node name, not including terminator character.
        //! This function works correctly irrespective of whether name is or is not zero terminated.
        //! \return Size of node name, in characters.
        std::size_t name_size() const
        {
            return m_name ? m_name_size : 0;
        }

        //! Gets value of node. 
        //! Interpretation of value depends on type of node.
        //! Note that value will not be zero-terminated.
        //! <br><br>
        //! Use value_size() function to determine length of the value.
        //! \return Value of node, or empty string if node has no value.
        Ch *value() const
        {
            return m_value ? m_value : nullstr();
        }

        //! Gets size of node value, not including terminator character.
        //! This function works correctly irrespective of whether value is or is not zero terminated.
        //! \return Size of node value, in characters.
        std::size_t value_size() const
        {
            return m_value ? m_value_size : 0;
        }

        ///////////////////////////////////////////////////////////////////////////
        // Node modification
    
        //! Sets name of node to a non zero-terminated string.
        //! See \ref ownership_of_strings.
        //! <br><br>
        //! Note that node does not own its name or value, it only stores a pointer to it. 
        //! It will not delete or otherwise free the pointer on destruction.
        //! It is reponsibility of the user to properly manage lifetime of the string.
        //! The easiest way to achieve it is to use memory_pool of the document to allocate the string -
        //! on destruction of the document the string will be automatically freed.
        //! <br><br>
        //! Size of name must be specified separately, because name does not have to be zero terminated.
        //! Use name(const Ch *) function to have the length automatically calculated (string must be zero terminated).
        //! \param name Name of node to set. Does not have to be zero terminated.
        //! \param size Size of name, in characters. This does not include zero terminator, if one is present.
        void name(const Ch *name, std::size_t size)
        {
            m_name = const_cast<Ch *>(name);
            m_name_size = size;
        }

        //! Sets name of node to a zero-terminated string.
        //! See also \ref ownership_of_strings and xml_node::name(const Ch *, std::size_t).
        //! \param name Name of node to set. Must be zero terminated.
        void name(const Ch *name)
        {
            this->name(name, internal::measure(name));
        }

        //! Sets value of node to a non zero-terminated string.
        //! See \ref ownership_of_strings.
        //! <br><br>
        //! Note that node does not own its name or value, it only stores a pointer to it. 
        //! It will not delete or otherwise free the pointer on destruction.
        //! It is reponsibility of the user to properly manage lifetime of the string.
        //! The easiest way to achieve it is to use memory_pool of the document to allocate the string -
        //! on destruction of the document the string will be automatically freed.
        //! <br><br>
        //! Size of value must be specified separately, because it does not have to be zero terminated.
        //! Use value(const Ch *) function to have the length automatically calculated (string must be zero terminated).
        //! <br><br>
        //! If an element has a child node of type node_data, it will take precedence over element value when printing.
        //! If you want to manipulate data of elements using values, use parser flag rapidxml::parse_no_data_nodes to prevent creation of data nodes by the parser.
        //! \param value value of node to set. Does not have to be zero terminated.
        //! \param size Size of value, in characters. This does not include zero terminator, if one is present.
        void value(const Ch *value, std::size_t size)
        {
            m_value = const_cast<Ch *>(value);
            m_value_size = size;
        }

        //! Sets value of node to a zero-terminated string.
        //! See also \ref ownership_of_strings and xml_node::value(const Ch *, std::size_t).
        //! \param value Vame of node to set. Must be zero terminated.
        void value(const Ch *value)
        {
            this->value(value, internal::measure(value));
        }

    protected:

        // Return empty string
        static Ch *nullstr()
        {
            static Ch zero = Ch('\0');
            return &zero;
        }

        Ch *m_name;                         // Name of node, or 0 if no name
        Ch *m_value;                        // Value of node, or 0 if no value
        std::size_t m_name_size;            // Length of node name, or undefined of no name
        std::size_t m_value_size;           // Length of node value, or undefined if no value

    };

    ///////////////////////////////////////////////////////////////////////////
    // XML document
    
    //! This class represents root of the DOM hierarchy. 
    //! Use parse() function to build a DOM tree from a zero-terminated XML text string.
    //! parse() function allocates memory for nodes and attributes by using functions of xml_document, 
    //! which are inherited from memory_pool.
    //! \param Ch Character type to use.
    template<class Ch = char>
    class xml_document
    {
    
    public:

        //! Constructs empty XML document
        xml_document()
        {
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
        void parse(Ch *text)
        {
            assert(text);
            
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
                    parse_node(text);  // TODO: no node allocation
                }
                else
                    RAPIDXML_PARSE_ERROR("expected <", text);
            }

        }
        
    private:

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
        static void insert_coded_character(Ch *&text, unsigned long code)
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
        template<class StopPred, class StopPredPure, int Flags>
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

        ///////////////////////////////////////////////////////////////////////
        // Internal parsing functions
        
        // Parse BOM, if any
        void parse_bom(Ch *&text)
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
        void parse_xml_declaration(Ch *&text)
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
        void parse_comment(Ch *&text)
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
        void parse_doctype(Ch *&text)
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
        void parse_pi(Ch *&text)
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
        Ch parse_and_append_data(Ch *&text, Ch *contents_start)
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
        void parse_cdata(Ch *&text)
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
        void parse_element(Ch *&text)
        {
            // Extract element name
            Ch *name = text;
            skip<node_name_pred>(text);
            if (text == name)
                RAPIDXML_PARSE_ERROR("expected element name", text);
            // TODO: no node allocation
            
            // Skip whitespace between element name and attributes or >
            skip<whitespace_pred>(text);

            // Parse attributes, if any
            parse_node_attributes(text);

            // Determine ending type
            if (*text == Ch('>'))
            {
                ++text;
                parse_node_contents(text);
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

        // Determine node type, and parse it
        void parse_node(Ch *&text)
        {
            // Parse proper node type
            switch (text[0])
            {

            // <...
            default: 
                // Parse and append element node
                parse_element(text);
                return;

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
                    return;
                }
                else
                {
                    // Parse PI
                    parse_pi(text);
                    return;
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
                        return;
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
                        return;
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
                        return;
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
                return;     // No node recognized

            }
        }

        // Parse contents of the node - children, data etc.
        void parse_node_contents(Ch *&text)
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
                        parse_node(text);
                        // TODO: no node allocation
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
        void parse_node_attributes(Ch *&text)
        {
            // For all attributes 
            while (attribute_name_pred::test(*text))
            {
                // Extract attribute name
                Ch *name = text;
                ++text;     // Skip first character of attribute name
                skip<attribute_name_pred>(text);
                if (text == name)
                    RAPIDXML_PARSE_ERROR("expected attribute name", name);

                // Create new attribute
                // TODO: no node allocation
                // attribute->name(name, text - name);

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

                // Extract attribute value and expand char refs in it
                Ch *value = text;
                if (quote == Ch('\''))
                    skip<attribute_value_pred<Ch('\'')>>(text);
                else
                    skip<attribute_value_pred<Ch('"')>>(text);
                
                // Set attribute value
                // TODO: no node allocation
                // attribute->value(value, text - value);
                
                // Make sure that end quote is present
                if (*text != quote)
                    RAPIDXML_PARSE_ERROR("expected ' or \"", text);
                ++text;     // Skip quote

                // Skip whitespace after attribute value
                skip<whitespace_pred>(text);
            }
        }

    };

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
