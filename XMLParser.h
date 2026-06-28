// -------------------------------------------------------------------------------------------------------------
//
// Written by Daniel J. Hobson of Australia, 2026
//
// XMLParser.h - Standards-Compliant XML 1.0/1.1 Parser, DOM Builder, SAX Interface, and XPath Engine
//
// Purpose: Provides a fully standards-compliant XML parser with support for:
//          - DOM-style tree building, querying and manipulation (DOM Level 2 Core)
//          - SAX2-style event-driven parsing via callback interface
//          - XPath 1.0 query engine (axes, predicates, wildcards, unions)
//          - XML Namespaces 1.0/1.1 (prefix declarations, URI resolution)
//          - Entity resolution: built-in (&amp; &lt; &gt; &apos; &quot;), character refs, user-defined
//          - CDATA sections, Processing Instructions, Comments
//          - Character encoding detection: UTF-8, UTF-16 LE/BE (BOM), ISO-8859-1
//          - DOCTYPE parsing with internal subset (ENTITY, ATTLIST, ELEMENT, NOTATION decls)
//          - Well-formedness validation and depth/expansion limits (XXE protection)
//          - XML document building via factory methods (CreateElement, CreateTextNode, etc.)
//          - XML Serialization with pretty-printing and configurable options
//          - Thread-safe document operations
//
// Features:
// - Full XML 1.0 Specification compliance (W3C REC-xml-20081126)
// - XML Namespaces 1.0 (W3C REC-xml-names-19990114)
// - XPath 1.0 query support: axes, node tests, predicates, union operator
// - SAX2-style event callbacks with namespace awareness
// - DOM Level 2 Core API: createElement, appendChild, insertBefore, replaceChild, etc.
// - UTF-8 primary encoding; UTF-16 BOM detection and conversion
// - Configurable whitespace handling (PRESERVE / NORMALIZE / TRIM)
// - Entity expansion limits for XXE attack prevention
// - Serialization with optional pretty-printing, sort-attributes, custom indent
// - Zero external dependencies - pure C++17
// -------------------------------------------------------------------------------------------------------------
#pragma once

#include "Includes.h"
#include "Debug.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <mutex>
#include <cstdint>
#include <stdexcept>

// Forward declarations
extern Debug debug;

// Undefine Windows macros that collide with XMLNode method names.
// <windowsx.h> defines GetFirstChild(hwnd) and GetNextSibling(hwnd) as
// function-like macros, which corrupt the XMLNode declarations below.
#ifdef GetFirstChild
    #undef GetFirstChild
#endif
#ifdef GetLastChild
    #undef GetLastChild
#endif
#ifdef GetNextSibling
    #undef GetNextSibling
#endif
#ifdef GetPreviousSibling
    #undef GetPreviousSibling
#endif

//==============================================================================
// Constants
//==============================================================================
constexpr int    XML_MAX_DEPTH         = 1000;                          // Maximum element nesting depth
constexpr int    XML_MAX_ENTITY_EXPAND = 10000;                         // Maximum entity expansion iterations (XXE guard)
constexpr size_t XML_MAX_ATTR_COUNT    = 65536;                         // Maximum attributes per element
constexpr int    XML_DEFAULT_INDENT    = 4;                             // Default pretty-print indent width in spaces

//==============================================================================
// Enumerations
//==============================================================================

// XML node types (aligned with DOM Level 2 Core constants)
enum class XMLNodeType : uint8_t
{
    DOCUMENT                = 0,                                        // Root document container node
    ELEMENT                 = 1,                                        // Element node (<tag ...>)
    ATTRIBUTE               = 2,                                        // Attribute (stored inside XMLNode::attributes)
    TEXT                    = 3,                                        // Text content node (character data)
    CDATA_SECTION           = 4,                                        // CDATA section (<![CDATA[...]]>)
    ENTITY_REFERENCE        = 5,                                        // Resolved entity reference node
    PROCESSING_INSTRUCTION  = 6,                                        // Processing instruction (<?target data?>)
    COMMENT                 = 7,                                        // Comment node (<!-- ... -->)
    DOCUMENT_TYPE           = 8                                         // DOCTYPE declaration node
};

// Parse error codes returned in XMLParseResult
enum class XMLParseError : uint16_t
{
    NONE                    = 0,                                        // Success - no error
    UNEXPECTED_EOF          = 1,                                        // Unexpected end of input
    INVALID_CHARACTER       = 2,                                        // Illegal character in this context
    MALFORMED_TAG           = 3,                                        // Tag or attribute syntax error
    MISMATCHED_TAG          = 4,                                        // Close tag does not match open tag
    INVALID_ATTRIBUTE       = 5,                                        // Attribute syntax is malformed
    DUPLICATE_ATTRIBUTE     = 6,                                        // Same attribute name appears twice on one element
    INVALID_ENTITY          = 7,                                        // Unknown or malformed entity reference
    ENTITY_EXPANSION_LIMIT  = 8,                                        // Entity expansion limit exceeded (XXE protection)
    INVALID_ENCODING        = 9,                                        // Unsupported or mismatched character encoding
    INVALID_DOCTYPE         = 10,                                       // Malformed DOCTYPE declaration
    INVALID_CDATA           = 11,                                       // Malformed CDATA section
    INVALID_COMMENT         = 12,                                       // Double-dash (--) inside comment body
    INVALID_PI              = 13,                                       // Malformed processing instruction
    INVALID_XML_DECLARATION = 14,                                       // Malformed or misplaced <?xml ...?> declaration
    NAMESPACE_ERROR         = 15,                                       // Namespace prefix used but not declared
    MAX_DEPTH_EXCEEDED      = 16,                                       // Nesting depth exceeded XML_MAX_DEPTH
    FILE_NOT_FOUND          = 17,                                       // Source file could not be opened
    IO_ERROR                = 18,                                       // File read or write error
    XPATH_SYNTAX_ERROR      = 19,                                       // XPath expression has invalid syntax
    XPATH_TYPE_ERROR        = 20,                                       // Type mismatch in XPath evaluation
    SERIALIZATION_ERROR     = 21                                        // Error during XML serialization
};

// Character encoding detected or declared
enum class XMLEncoding : uint8_t
{
    UNKNOWN                 = 0,                                        // Not yet determined
    UTF_8                   = 1,                                        // UTF-8 (default for XML 1.0)
    UTF_16_LE               = 2,                                        // UTF-16 Little Endian (FF FE BOM)
    UTF_16_BE               = 3,                                        // UTF-16 Big Endian (FE FF BOM)
    ISO_8859_1              = 4,                                        // Latin-1 / ISO-8859-1
    US_ASCII                = 5                                         // 7-bit ASCII (strict subset of UTF-8)
};

// XPath axis identifiers
enum class XMLXPathAxis : uint8_t
{
    CHILD                   = 0,                                        // Direct child nodes
    DESCENDANT              = 1,                                        // All descendant nodes
    DESCENDANT_OR_SELF      = 2,                                        // Self plus all descendants
    PARENT                  = 3,                                        // Immediate parent node
    ANCESTOR                = 4,                                        // All ancestor nodes
    ANCESTOR_OR_SELF        = 5,                                        // Self plus all ancestors
    SELF                    = 6,                                        // The context node itself
    ATTRIBUTE               = 7,                                        // Attributes of the context node
    FOLLOWING_SIBLING       = 8,                                        // Siblings that follow the context node
    PRECEDING_SIBLING       = 9                                         // Siblings that precede the context node
};

// Whitespace handling modes applied to text content
enum class XMLWhitespace : uint8_t
{
    PRESERVE                = 0,                                        // Keep all whitespace exactly as-is
    NORMALIZE               = 1,                                        // Collapse whitespace runs to single space
    TRIM                    = 2                                         // Strip leading and trailing whitespace
};

//==============================================================================
// XMLParseResult - Returned by all parse and serialization operations
//==============================================================================
struct XMLParseResult
{
    XMLParseError   error       = XMLParseError::NONE;                  // Error code (NONE on success)
    int             line        = 0;                                    // 1-based source line of error
    int             column      = 0;                                    // 1-based source column of error
    std::string     description;                                        // Human-readable error description

    bool            success()   const { return error == XMLParseError::NONE; }
    explicit        operator bool()   const { return success(); }
};

//==============================================================================
// XMLAttribute - Single attribute name/value pair on an element
//==============================================================================
struct XMLAttribute
{
    std::string name;                                                   // Qualified name (prefix:local or local)
    std::string value;                                                  // Decoded attribute value (entities expanded)
    std::string prefix;                                                 // Namespace prefix ("" if none)
    std::string localName;                                              // Local part of the qualified name
    std::string namespaceURI;                                           // Resolved namespace URI ("" if none)

    XMLAttribute() = default;
    XMLAttribute(const std::string& n, const std::string& v) : name(n), value(v), localName(n) {}
    XMLAttribute(const std::string& n, const std::string& v, const std::string& ns)
        : name(n), value(v), localName(n), namespaceURI(ns) {}
};

//==============================================================================
// XMLNode - DOM Level 2 Core Node
//==============================================================================
class XMLNode : public std::enable_shared_from_this<XMLNode>
{
public:
    //--------------------------------------------------------------------------
    // Public fields
    //--------------------------------------------------------------------------
    XMLNodeType     type        = XMLNodeType::ELEMENT;                 // Node type classification
    std::string     name;                                               // Tag name / PI target / "#text" etc.
    std::string     value;                                              // Text / comment / CDATA / PI data
    std::string     prefix;                                             // Namespace prefix (elements/attrs)
    std::string     localName;                                          // Local part of element/attribute name
    std::string     namespaceURI;                                       // Resolved namespace URI

    std::vector<XMLAttribute>                       attributes;         // Element attributes in document order
    std::vector<std::shared_ptr<XMLNode>>           children;           // Ordered child nodes
    std::weak_ptr<XMLNode>                          parent;             // Parent (weak to avoid reference cycles)
    std::unordered_map<std::string, std::string>    namespaceMappings;  // xmlns:* declarations on this element

    //--------------------------------------------------------------------------
    // Construction
    //--------------------------------------------------------------------------
    XMLNode() = default;
    explicit XMLNode(XMLNodeType t) : type(t) {}
    XMLNode(XMLNodeType t, const std::string& n, const std::string& v = "")
        : type(t), name(n), value(v), localName(n) {}

    //--------------------------------------------------------------------------
    // Attribute access and mutation
    //--------------------------------------------------------------------------
    bool            HasAttribute(const std::string& attrName) const;
    bool            HasAttributeNS(const std::string& nsURI, const std::string& localAttrName) const;
    std::string     GetAttribute(const std::string& attrName, const std::string& defaultValue = "") const;
    std::string     GetAttributeNS(const std::string& nsURI, const std::string& localAttrName, const std::string& defaultValue = "") const;
    void            SetAttribute(const std::string& attrName, const std::string& attrValue);
    void            SetAttributeNS(const std::string& nsURI, const std::string& qualifiedName, const std::string& attrValue);
    void            RemoveAttribute(const std::string& attrName);
    void            RemoveAttributeNS(const std::string& nsURI, const std::string& localAttrName);
    const XMLAttribute* FindAttribute(const std::string& attrName) const;
    XMLAttribute*       FindAttribute(const std::string& attrName);

    //--------------------------------------------------------------------------
    // Child navigation
    //--------------------------------------------------------------------------
    std::shared_ptr<XMLNode> GetFirstChild() const;
    std::shared_ptr<XMLNode> GetLastChild() const;
    std::shared_ptr<XMLNode> GetNextSibling() const;
    std::shared_ptr<XMLNode> GetPreviousSibling() const;
    std::shared_ptr<XMLNode> GetFirstChildByName(const std::string& elemName) const;
    std::shared_ptr<XMLNode> GetFirstChildByLocalName(const std::string& localElemName, const std::string& nsURI = "") const;

    std::vector<std::shared_ptr<XMLNode>> GetChildrenByName(const std::string& elemName) const;
    std::vector<std::shared_ptr<XMLNode>> GetChildrenByLocalName(const std::string& localElemName, const std::string& nsURI = "") const;
    std::vector<std::shared_ptr<XMLNode>> GetElementsByTagName(const std::string& elemName) const;
    std::vector<std::shared_ptr<XMLNode>> GetElementsByLocalName(const std::string& localElemName, const std::string& nsURI = "") const;

    //--------------------------------------------------------------------------
    // Text content helpers
    //--------------------------------------------------------------------------
    std::string     GetTextContent() const;                             // Concatenated text of all descendant TEXT/CDATA nodes
    void            SetTextContent(const std::string& text);            // Replace all children with a single TEXT node
    std::string     GetInnerText() const;                               // Alias for GetTextContent
    std::string     GetDirectText() const;                              // Only direct TEXT/CDATA children (not grandchildren)

    //--------------------------------------------------------------------------
    // Child manipulation (DOM Level 2 Core API)
    //--------------------------------------------------------------------------
    std::shared_ptr<XMLNode> AppendChild(std::shared_ptr<XMLNode> child);
    std::shared_ptr<XMLNode> PrependChild(std::shared_ptr<XMLNode> child);
    std::shared_ptr<XMLNode> InsertBefore(std::shared_ptr<XMLNode> newNode, std::shared_ptr<XMLNode> refNode);
    std::shared_ptr<XMLNode> InsertAfter(std::shared_ptr<XMLNode> newNode, std::shared_ptr<XMLNode> refNode);
    std::shared_ptr<XMLNode> RemoveChild(std::shared_ptr<XMLNode> child);
    std::shared_ptr<XMLNode> ReplaceChild(std::shared_ptr<XMLNode> newNode, std::shared_ptr<XMLNode> oldNode);
    void                     RemoveAllChildren();

    //--------------------------------------------------------------------------
    // Namespace utilities
    //--------------------------------------------------------------------------
    std::string     LookupNamespaceURI(const std::string& nsPrefix) const;   // Walk ancestors to resolve prefix
    std::string     LookupPrefix(const std::string& uri) const;              // Walk ancestors to find prefix for URI
    bool            IsDefaultNamespace(const std::string& uri) const;

    //--------------------------------------------------------------------------
    // Utility / introspection
    //--------------------------------------------------------------------------
    bool IsElement()  const { return type == XMLNodeType::ELEMENT; }
    bool IsText()     const { return type == XMLNodeType::TEXT; }
    bool IsCDATA()    const { return type == XMLNodeType::CDATA_SECTION; }
    bool IsComment()  const { return type == XMLNodeType::COMMENT; }
    bool IsPI()       const { return type == XMLNodeType::PROCESSING_INSTRUCTION; }
    bool IsDocument() const { return type == XMLNodeType::DOCUMENT; }
    bool IsDocType()  const { return type == XMLNodeType::DOCUMENT_TYPE; }
    bool HasChildren() const { return !children.empty(); }
    int  ChildCount()  const { return static_cast<int>(children.size()); }
    int  GetDepth()    const;                                           // Distance from document root (root = 0)
    int  GetChildIndex() const;                                         // 0-based index among siblings (-1 if none)

    std::shared_ptr<XMLNode> Clone(bool deep = true) const;
    std::string              ToString(bool pretty = false, int indentLevel = 0, int indentWidth = XML_DEFAULT_INDENT) const;

    // Internal recursive helpers (public so XMLDocument can call them without friend declaration)
    void CollectText(std::string& out) const;
    void CollectElements(const std::string& tagName, std::vector<std::shared_ptr<XMLNode>>& out) const;
    void CollectElementsByLocal(const std::string& localName, const std::string& nsURI, std::vector<std::shared_ptr<XMLNode>>& out) const;
};

//==============================================================================
// XMLDocument - Root Document Container (DOM Level 2)
//==============================================================================
class XMLDocument
{
public:
    std::shared_ptr<XMLNode>                        root;               // Document node (type = DOCUMENT)
    std::string                                     version;            // XML version string (default "1.0")
    std::string                                     encoding;           // Declared encoding (default "UTF-8")
    bool                                            standalone  = false; // Standalone declaration value
    std::string                                     doctypeName;        // DOCTYPE declared element name
    std::string                                     doctypeSystemId;    // DOCTYPE SYSTEM identifier URI
    std::string                                     doctypePublicId;    // DOCTYPE PUBLIC identifier
    std::string                                     doctypeInternalSubset; // Raw DOCTYPE internal subset text
    std::unordered_map<std::string, std::string>    entities;           // User-defined general entity map

    XMLDocument();

    //--------------------------------------------------------------------------
    // Document structure access
    //--------------------------------------------------------------------------
    std::shared_ptr<XMLNode> GetDocumentElement() const;                // First ELEMENT child of the document root
    std::shared_ptr<XMLNode> GetDocType() const;                        // DOCUMENT_TYPE node if present

    //--------------------------------------------------------------------------
    // Factory / builder methods (DOM Level 2 Core)
    //--------------------------------------------------------------------------
    std::shared_ptr<XMLNode> CreateElement(const std::string& tagName) const;
    std::shared_ptr<XMLNode> CreateElementNS(const std::string& nsURI, const std::string& qualifiedName) const;
    std::shared_ptr<XMLNode> CreateTextNode(const std::string& text) const;
    std::shared_ptr<XMLNode> CreateCDATASection(const std::string& data) const;
    std::shared_ptr<XMLNode> CreateComment(const std::string& comment) const;
    std::shared_ptr<XMLNode> CreateProcessingInstruction(const std::string& target, const std::string& data) const;

    //--------------------------------------------------------------------------
    // DOM search methods
    //--------------------------------------------------------------------------
    std::shared_ptr<XMLNode>              GetElementById(const std::string& id) const;
    std::vector<std::shared_ptr<XMLNode>> GetElementsByTagName(const std::string& tagName) const;
    std::vector<std::shared_ptr<XMLNode>> GetElementsByLocalName(const std::string& localName, const std::string& nsURI = "") const;

    //--------------------------------------------------------------------------
    // XPath 1.0 query engine
    //--------------------------------------------------------------------------
    // Returns all matching nodes for an XPath expression.
    std::vector<std::shared_ptr<XMLNode>> XPathQuery(const std::string& expression, std::shared_ptr<XMLNode> context = nullptr) const;
    // Returns the first matching node, or nullptr.
    std::shared_ptr<XMLNode>              XPathQuerySingle(const std::string& expression, std::shared_ptr<XMLNode> context = nullptr) const;
    // Returns the string-value of the first matching node.
    std::string                           XPathQueryString(const std::string& expression, std::shared_ptr<XMLNode> context = nullptr) const;
    // Returns the numeric value of the first matching node's text content.
    double                                XPathQueryNumber(const std::string& expression, std::shared_ptr<XMLNode> context = nullptr) const;
    // Returns true if the expression matches at least one node.
    bool                                  XPathQueryBool(const std::string& expression, std::shared_ptr<XMLNode> context = nullptr) const;

    //--------------------------------------------------------------------------
    // Serialization / saving
    //--------------------------------------------------------------------------
    std::string ToString(bool pretty = true, int indentWidth = XML_DEFAULT_INDENT) const;
    bool        SaveToFile(const std::string& filepath, bool pretty = true, int indentWidth = XML_DEFAULT_INDENT) const;
    bool        SaveToFile(const std::wstring& filepath, bool pretty = true, int indentWidth = XML_DEFAULT_INDENT) const;

    //--------------------------------------------------------------------------
    // Node import (from another document)
    //--------------------------------------------------------------------------
    std::shared_ptr<XMLNode> ImportNode(const std::shared_ptr<XMLNode>& node, bool deep = true) const;

private:
    //--------------------------------------------------------------------------
    // XPath internals
    //--------------------------------------------------------------------------
    struct XPathStep
    {
        XMLXPathAxis    axis                = XMLXPathAxis::CHILD;      // Which axis to walk
        std::string     nodeTest;                                       // Name test, *, text(), comment() etc.
        bool            isWildcard          = false;                    // True when nodeTest is "*"
        bool            isTextTest          = false;                    // True when nodeTest is "text()"
        bool            isCommentTest       = false;                    // True when nodeTest is "comment()"
        bool            isPITest            = false;                    // True when nodeTest is "processing-instruction()"
        bool            isNodeTest          = false;                    // True when nodeTest is "node()"

        // Predicate: positional [n] or [last()]
        int             positionPredicate   = -1;                       // 1-based position, -1 = not set
        bool            positionIsLast      = false;                    // True when predicate is [last()]

        // Predicate: attribute [@name] or [@name='value']
        std::string     predicateAttr;                                  // Attribute name for predicate
        bool            hasAttrPredicate    = false;
        std::string     predicateAttrValue;                             // Expected attribute value
        bool            hasAttrValueTest    = false;

        // Predicate: text content [text()='value']
        std::string     predicateTextValue;
        bool            hasTextValueTest    = false;

        // Predicate: child element value [elem='value']
        std::string     predicateElemName;
        std::string     predicateElemValue;
        bool            hasElemValueTest    = false;

        // Predicate: child element existence [elem]
        std::string     predicateElemExist;
        bool            hasElemExistTest    = false;

        bool            descendantShortcut  = false;                    // Set when '//' shortcut precedes step
    };

    std::vector<XPathStep>                ParseXPath(const std::string& expression) const;
    XPathStep                             ParseXPathStep(const std::string& stepStr, bool isDescendant) const;
    void                                  ParsePredicate(const std::string& pred, XPathStep& step) const;
    std::vector<std::shared_ptr<XMLNode>> EvaluateXPath(const std::vector<XPathStep>& steps, std::shared_ptr<XMLNode> context) const;
    void                                  CollectAxis(const XPathStep& step, std::shared_ptr<XMLNode> ctx, std::vector<std::shared_ptr<XMLNode>>& out) const;
    bool                                  MatchesNodeTest(const XPathStep& step, const std::shared_ptr<XMLNode>& node) const;
    bool                                  MatchesPredicate(const XPathStep& step, const std::shared_ptr<XMLNode>& node, int pos, int total) const;
    void                                  GetDescendants(const std::shared_ptr<XMLNode>& node, std::vector<std::shared_ptr<XMLNode>>& out) const;
    void                                  GetAncestors(const std::shared_ptr<XMLNode>& node, std::vector<std::shared_ptr<XMLNode>>& out) const;
};

//==============================================================================
// XMLSAXCallbacks - SAX2-style event interface for streaming / low-memory parsing
//==============================================================================
struct XMLSAXCallbacks
{
    // Fired before the first token is processed
    std::function<void()> onStartDocument;
    // Fired after the last token is processed (even on error)
    std::function<void()> onEndDocument;

    // Fired for <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
    std::function<void(const std::string& version, const std::string& encoding, bool standalone)> onXMLDeclaration;

    // Fired for <!DOCTYPE name SYSTEM "..." PUBLIC "..." [...]>
    std::function<void(const std::string& name, const std::string& systemId,
                       const std::string& publicId, const std::string& internalSubset)> onDoctype;

    // Fired for <?target data?>
    std::function<void(const std::string& target, const std::string& data)> onProcessingInstruction;

    // Fired for <!-- ... -->
    std::function<void(const std::string& comment)> onComment;

    // Fired when an element open tag is encountered (namespace-aware)
    std::function<void(const std::string& qualifiedName, const std::string& nsURI,
                       const std::string& localName, const std::vector<XMLAttribute>& attrs)> onStartElement;

    // Fired when an element close tag is encountered
    std::function<void(const std::string& qualifiedName, const std::string& nsURI,
                       const std::string& localName)> onEndElement;

    // Fired for text content and CDATA sections (isCDATA distinguishes them)
    std::function<void(const std::string& text, bool isCDATA)> onCharacters;

    // Fired when a namespace prefix mapping is opened (xmlns:prefix="uri")
    std::function<void(const std::string& prefix, const std::string& uri)> onStartPrefixMapping;
    // Fired when a namespace prefix mapping goes out of scope
    std::function<void(const std::string& prefix)> onEndPrefixMapping;

    // Fired on any parse error
    std::function<void(XMLParseError code, int line, int col, const std::string& message)> onError;
};

//==============================================================================
// XMLParser - Core Parser Class
//==============================================================================
class XMLParser
{
public:
    XMLParser();
    ~XMLParser() = default;

    //--------------------------------------------------------------------------
    // DOM parsing - reads entire document into an XMLDocument tree
    //--------------------------------------------------------------------------
    XMLParseResult ParseFile(const std::string& filepath, XMLDocument& outDoc);
    XMLParseResult ParseFile(const std::wstring& filepath, XMLDocument& outDoc);
    XMLParseResult ParseString(const std::string& xmlContent, XMLDocument& outDoc);
    XMLParseResult ParseBytes(const std::vector<uint8_t>& data, XMLDocument& outDoc);

    //--------------------------------------------------------------------------
    // SAX parsing - event-driven, minimal memory usage
    //--------------------------------------------------------------------------
    XMLParseResult ParseFileSAX(const std::string& filepath, const XMLSAXCallbacks& callbacks);
    XMLParseResult ParseFileSAX(const std::wstring& filepath, const XMLSAXCallbacks& callbacks);
    XMLParseResult ParseStringSAX(const std::string& xmlContent, const XMLSAXCallbacks& callbacks);
    XMLParseResult ParseBytesSAX(const std::vector<uint8_t>& data, const XMLSAXCallbacks& callbacks);

    //--------------------------------------------------------------------------
    // Parser options
    //--------------------------------------------------------------------------
    void SetPreserveWhitespace(bool preserve)     { mPreserveWhitespace   = preserve; }
    void SetValidationMode(bool validate)         { mValidationMode       = validate; }
    void SetNamespaceProcessing(bool process)     { mProcessNamespaces    = process; }
    void SetMaxDepth(int depth)                   { mMaxDepth             = depth; }
    void SetEntityExpansionLimit(int limit)       { mEntityExpansionLimit = limit; }
    void SetWhitespaceMode(XMLWhitespace mode)    { mWhitespaceMode       = mode; }
    void AddCustomEntity(const std::string& name, const std::string& value) { mCustomEntities[name] = value; }

    bool GetPreserveWhitespace() const            { return mPreserveWhitespace; }
    bool GetValidationMode()     const            { return mValidationMode; }
    bool GetNamespaceProcessing()const            { return mProcessNamespaces; }
    int  GetMaxDepth()           const            { return mMaxDepth; }

    //--------------------------------------------------------------------------
    // Static utilities
    //--------------------------------------------------------------------------
    static std::string  EncodeEntities(const std::string& text);
    static std::string  DecodeEntities(const std::string& text,
                                       const std::unordered_map<std::string, std::string>& customEntities = {});
    static bool         IsValidXMLName(const std::string& name);
    static bool         IsValidXMLChar(uint32_t codepoint);
    static XMLEncoding  DetectEncoding(const uint8_t* data, size_t length, std::string& outDeclaredEncoding);
    static std::string  ConvertToUTF8(const uint8_t* data, size_t length, XMLEncoding encoding);
    static std::string  NormalizeLineEndings(const std::string& text);
    static std::string  ApplyWhitespace(const std::string& text, XMLWhitespace mode);

private:
    //--------------------------------------------------------------------------
    // Parser configuration
    //--------------------------------------------------------------------------
    bool            mPreserveWhitespace     = false;
    bool            mValidationMode         = false;
    bool            mProcessNamespaces      = true;
    int             mMaxDepth               = XML_MAX_DEPTH;
    int             mEntityExpansionLimit   = XML_MAX_ENTITY_EXPAND;
    XMLWhitespace   mWhitespaceMode         = XMLWhitespace::PRESERVE;
    std::unordered_map<std::string, std::string> mCustomEntities;

    //--------------------------------------------------------------------------
    // Internal parse state (stack-allocated per DoParse call)
    //--------------------------------------------------------------------------
    struct ParseState
    {
        const char* data            = nullptr;                          // Input buffer
        size_t      length          = 0;                                // Buffer length in bytes
        size_t      pos             = 0;                                // Current read position
        int         line            = 1;                                // Current line (1-based)
        int         col             = 1;                                // Current column (1-based)
        int         depth           = 0;                                // Current element depth
        int         entityExpansions = 0;                               // Running entity expansion count
        bool        sawXMLDecl      = false;                            // XML declaration already consumed
        bool        sawDocType      = false;                            // DOCTYPE already consumed
        bool        sawRootElement  = false;                            // Root element already started
        bool        hasError        = false;                            // Error occurred flag
        bool        inDTD           = false;                            // Currently parsing DOCTYPE internal subset
        XMLParseResult errorResult;                                     // Captured error details

        XMLDocument*                doc         = nullptr;              // Output document (DOM mode, may be null)
        const XMLSAXCallbacks*      callbacks   = nullptr;              // SAX callbacks (SAX mode, may be null)

        std::unordered_map<std::string, std::string> entities;         // Active entity table
        std::unordered_map<std::string, std::string> notations;        // Declared notations
        std::stack<std::string>                       elementStack;     // Open element qualified names
        std::stack<std::unordered_map<std::string, std::string>> nsStack; // Namespace scope stack
    };

    //--------------------------------------------------------------------------
    // Core dispatch
    //--------------------------------------------------------------------------
    XMLParseResult DoParse(const char* data, size_t length, XMLDocument* doc, const XMLSAXCallbacks* callbacks);

    //--------------------------------------------------------------------------
    // Section parsers (all return false on error; error stored in state.errorResult)
    //--------------------------------------------------------------------------
    bool ParseProlog(ParseState& state, std::shared_ptr<XMLNode> parent);
    bool ParseXMLDeclaration(ParseState& state, std::shared_ptr<XMLNode> parent);
    bool ParseDocType(ParseState& state);
    bool ParseInternalSubset(ParseState& state);
    bool ParseEntityDecl(ParseState& state);
    bool ParseAttlistDecl(ParseState& state);
    bool ParseElementDecl(ParseState& state);
    bool ParseNotationDecl(ParseState& state);
    bool ParseElement(ParseState& state, std::shared_ptr<XMLNode> parent);
    bool ParseContent(ParseState& state, std::shared_ptr<XMLNode> parent);
    bool ParseAttributes(ParseState& state, std::shared_ptr<XMLNode> node,
                         std::unordered_map<std::string, std::string>& nsLocalMap);
    bool ParseComment(ParseState& state, std::shared_ptr<XMLNode> parent);
    bool ParseCDATA(ParseState& state, std::shared_ptr<XMLNode> parent);
    bool ParseProcessingInstruction(ParseState& state, std::shared_ptr<XMLNode> parent);
    bool ParseText(ParseState& state, std::shared_ptr<XMLNode> parent);

    //--------------------------------------------------------------------------
    // Character-level lexer
    //--------------------------------------------------------------------------
    char            PeekChar(const ParseState& state, int offset = 0) const;
    char            ReadChar(ParseState& state);
    bool            IsAtEnd(const ParseState& state) const;
    bool            IsWhitespace(char c) const;
    bool            IsNameStartChar(unsigned char c) const;
    bool            IsNameChar(unsigned char c) const;
    void            SkipWhitespace(ParseState& state);
    bool            ExpectChar(ParseState& state, char c, XMLParseError err = XMLParseError::MALFORMED_TAG);
    bool            ExpectString(ParseState& state, const char* str, XMLParseError err = XMLParseError::MALFORMED_TAG);
    bool            MatchString(const ParseState& state, const char* str, int offset = 0) const;
    std::string     ReadName(ParseState& state);
    std::string     ReadAttributeValue(ParseState& state, char quoteChar);
    std::string     ReadUntilString(ParseState& state, const char* endStr, bool includeEnd = true);
    std::string     ReadQuotedLiteral(ParseState& state);

    //--------------------------------------------------------------------------
    // Entity resolution
    //--------------------------------------------------------------------------
    std::string ResolveEntityReference(ParseState& state, const std::string& name);
    std::string ResolveCharacterReference(const std::string& ref);
    std::string ExpandEntitiesInText(ParseState& state, const std::string& raw);

    //--------------------------------------------------------------------------
    // Namespace processing
    //--------------------------------------------------------------------------
    void        PushNamespaceScope(ParseState& state, const std::unordered_map<std::string, std::string>& localMap);
    void        PopNamespaceScope(ParseState& state);
    std::string LookupNamespace(const ParseState& state, const std::string& prefix) const;
    void        ResolveNodeNamespaces(std::shared_ptr<XMLNode> node, const ParseState& state);
    void        SplitQName(const std::string& qname, std::string& outPrefix, std::string& outLocal) const;

    //--------------------------------------------------------------------------
    // Error handling
    //--------------------------------------------------------------------------
    XMLParseResult MakeError(const ParseState& state, XMLParseError code, const std::string& desc);
    bool           SetError(ParseState& state, XMLParseError code, const std::string& desc);
};

//==============================================================================
// XMLSerializer - Standalone XML Serializer with configurable options
//==============================================================================
class XMLSerializer
{
public:
    struct Options
    {
        bool        pretty           = true;                            // Enable indented output
        int         indentWidth      = XML_DEFAULT_INDENT;              // Spaces per indent level
        char        indentChar       = ' ';                             // Indent character (' ' or '\t')
        bool        writeDeclaration = true;                            // Emit <?xml ...?> header
        bool        sortAttributes   = false;                           // Sort attributes alphabetically
        std::string lineEnding       = "\n";                            // Line ending ("\n" or "\r\n")
        std::string encoding         = "UTF-8";                         // Encoding name in declaration
        bool        omitXmlns        = false;                           // Suppress xmlns:* attributes in output
    };

    XMLSerializer() = default;
    explicit XMLSerializer(const Options& opts) : mOptions(opts) {}

    std::string Serialize(const XMLDocument& doc) const;
    std::string SerializeNode(const std::shared_ptr<XMLNode>& node, int depth = 0) const;
    bool        WriteToFile(const XMLDocument& doc, const std::string& filepath) const;
    bool        WriteToFile(const XMLDocument& doc, const std::wstring& filepath) const;

    void           SetOptions(const Options& opts) { mOptions = opts; }
    const Options& GetOptions() const              { return mOptions; }

private:
    Options mOptions;

    void        WriteNode(std::ostringstream& out, const std::shared_ptr<XMLNode>& node, int depth) const;
    void        WriteElement(std::ostringstream& out, const std::shared_ptr<XMLNode>& node, int depth) const;
    void        WriteText(std::ostringstream& out, const std::shared_ptr<XMLNode>& node) const;
    void        WriteCDATA(std::ostringstream& out, const std::shared_ptr<XMLNode>& node) const;
    void        WriteComment(std::ostringstream& out, const std::shared_ptr<XMLNode>& node, int depth) const;
    void        WritePI(std::ostringstream& out, const std::shared_ptr<XMLNode>& node, int depth) const;
    void        WriteDocType(std::ostringstream& out, const std::shared_ptr<XMLNode>& node) const;
    std::string GetIndent(int depth) const;
    bool        IsInlineNode(const std::shared_ptr<XMLNode>& node) const;
};
