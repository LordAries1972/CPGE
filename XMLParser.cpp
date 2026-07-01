// -------------------------------------------------------------------------------------------------------------
//
// Written by Daniel J. Hobson of Australia, 2026
//
// XMLParser.cpp - Standards-Compliant XML 1.0/1.1 Parser Implementation
//
// Implements: XMLNode, XMLDocument, XMLParser, XMLSerializer
//
// 28-06-2026   -   Initial implementation
//                  DOM Level 2 Core (createElement, appendChild, insertBefore, replaceChild, etc.)
//                  SAX2-style event callbacks
//                  XML 1.0/1.1 parsing (elements, attributes, text, CDATA, comments, PIs)
//                  DOCTYPE parsing with internal subset (ENTITY, ATTLIST, ELEMENT, NOTATION)
//                  Namespace processing (xmlns prefix declarations and URI resolution)
//                  XPath 1.0 engine (axes, wildcards, predicates, union operator)
//                  UTF-8 primary encoding; UTF-16 LE/BE BOM detection
//                  Entity resolution (5 built-in + character refs + user-defined)
//                  XML document building and serialization (pretty-print, compact)
//                  XXE protection via entity expansion limit
// -------------------------------------------------------------------------------------------------------------
#include "XMLParser.h"
#include "Debug.h"

// ==============================================================================================
// Built-in XML entities (XML 1.0 spec, section 4.6)
// ==============================================================================================
static const std::unordered_map<std::string, std::string> s_BuiltinEntities = {
    { "amp",  "&"  },
    { "lt",   "<"  },
    { "gt",   ">"  },
    { "apos", "'"  },
    { "quot", "\"" }
};

// ==============================================================================================
// Utility: UTF-16 to UTF-8 conversion (BOM-aware)
// ==============================================================================================
static std::string UTF16ToUTF8(const uint16_t* src, size_t count, bool bigEndian)
{
    std::string out;
    out.reserve(count * 2);

    for (size_t i = 0; i < count; ++i)
    {
        uint16_t w = src[i];
        if (bigEndian)
            w = static_cast<uint16_t>((w >> 8) | (w << 8));            // Swap bytes for big-endian

        uint32_t cp = w;

        // Handle UTF-16 surrogate pairs
        if (w >= 0xD800 && w <= 0xDBFF && i + 1 < count)
        {
            uint16_t w2 = src[i + 1];
            if (bigEndian)
                w2 = static_cast<uint16_t>((w2 >> 8) | (w2 << 8));
            if (w2 >= 0xDC00 && w2 <= 0xDFFF)
            {
                cp = 0x10000 + ((uint32_t)(w - 0xD800) << 10) + (w2 - 0xDC00);
                ++i;
            }
        }

        // Encode codepoint as UTF-8
        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

// ==============================================================================================
// Utility: Trim whitespace from a string
// ==============================================================================================
static std::string TrimString(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n\f\v");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n\f\v");
    return s.substr(start, end - start + 1);
}

static std::wstring WidenForXMLLog(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}

// ==============================================================================================
// XMLParser - Static Utilities
// ==============================================================================================

std::string XMLParser::NormalizeLineEndings(const std::string& text)
{
    // XML 1.0 spec section 2.11: normalize \r\n and lone \r to \n
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\r')
        {
            out += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;                                                    // Skip the \n in \r\n pair
        }
        else
        {
            out += text[i];
        }
    }
    return out;
}

XMLEncoding XMLParser::DetectEncoding(const uint8_t* data, size_t length, std::string& outDeclaredEncoding)
{
    outDeclaredEncoding.clear();
    if (!data || length < 2)
        return XMLEncoding::UTF_8;

    // Check for UTF-16 BOM
    if (length >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        return XMLEncoding::UTF_16_LE;
    if (length >= 2 && data[0] == 0xFE && data[1] == 0xFF)
        return XMLEncoding::UTF_16_BE;

    // Check for UTF-8 BOM (EF BB BF)
    if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        return XMLEncoding::UTF_8;

    // Default: UTF-8
    return XMLEncoding::UTF_8;
}

std::string XMLParser::ConvertToUTF8(const uint8_t* data, size_t length, XMLEncoding encoding)
{
    if (encoding == XMLEncoding::UTF_8 || encoding == XMLEncoding::US_ASCII || encoding == XMLEncoding::UNKNOWN)
    {
        // Skip UTF-8 BOM if present
        size_t start = 0;
        if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
            start = 3;
        return std::string(reinterpret_cast<const char*>(data + start), length - start);
    }
    if (encoding == XMLEncoding::UTF_16_LE)
    {
        size_t start = (length >= 2 && data[0] == 0xFF && data[1] == 0xFE) ? 2 : 0;
        return UTF16ToUTF8(reinterpret_cast<const uint16_t*>(data + start), (length - start) / 2, false);
    }
    if (encoding == XMLEncoding::UTF_16_BE)
    {
        size_t start = (length >= 2 && data[0] == 0xFE && data[1] == 0xFF) ? 2 : 0;
        return UTF16ToUTF8(reinterpret_cast<const uint16_t*>(data + start), (length - start) / 2, true);
    }
    if (encoding == XMLEncoding::ISO_8859_1)
    {
        // Latin-1: each byte maps directly to the same Unicode codepoint
        std::string out;
        out.reserve(length * 2);
        for (size_t i = 0; i < length; ++i)
        {
            uint8_t c = data[i];
            if (c < 0x80)
            {
                out += static_cast<char>(c);
            }
            else
            {
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return out;
    }
    return std::string(reinterpret_cast<const char*>(data), length);
}

bool XMLParser::IsValidXMLName(const std::string& name)
{
    if (name.empty()) return false;
    auto isNameStart = [](unsigned char c) -> bool {
        return (c == ':' || c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0);
    };
    auto isNameChar = [&](unsigned char c) -> bool {
        return (isNameStart(c) || c == '-' || c == '.' || (c >= '0' && c <= '9') || c == 0xB7);
    };
    if (!isNameStart(static_cast<unsigned char>(name[0]))) return false;
    for (size_t i = 1; i < name.size(); ++i)
    {
        if (!isNameChar(static_cast<unsigned char>(name[i]))) return false;
    }
    return true;
}

bool XMLParser::IsValidXMLChar(uint32_t cp)
{
    // XML 1.0 production [2]: legal character set
    return (cp == 0x9 || cp == 0xA || cp == 0xD ||
           (cp >= 0x20   && cp <= 0xD7FF)  ||
           (cp >= 0xE000 && cp <= 0xFFFD)  ||
           (cp >= 0x10000 && cp <= 0x10FFFF));
}

std::string XMLParser::EncodeEntities(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (unsigned char c : text)
    {
        switch (c)
        {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += static_cast<char>(c); break;
        }
    }
    return out;
}

std::string XMLParser::DecodeEntities(const std::string& text,
    const std::unordered_map<std::string, std::string>& customEntities)
{
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] != '&')
        {
            out += text[i++];
            continue;
        }
        size_t end = text.find(';', i + 1);
        if (end == std::string::npos)
        {
            out += text[i++];
            continue;
        }
        std::string ref = text.substr(i + 1, end - i - 1);
        if (!ref.empty() && ref[0] == '#')
        {
            // Character reference
            uint32_t cp = 0;
            try
            {
                if (ref.size() > 1 && ref[1] == 'x')
                    cp = std::stoul(ref.substr(2), nullptr, 16);
                else
                    cp = std::stoul(ref.substr(1), nullptr, 10);
            }
            catch (...) { cp = 0; }
            if (cp < 0x80)
            {
                out += static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
        else
        {
            // Named entity
            auto it = s_BuiltinEntities.find(ref);
            if (it != s_BuiltinEntities.end())
            {
                out += it->second;
            }
            else
            {
                auto it2 = customEntities.find(ref);
                if (it2 != customEntities.end())
                    out += it2->second;
                else
                    out += '&', out += ref, out += ';';                 // Leave unknown entities as-is
            }
        }
        i = end + 1;
    }
    return out;
}

std::string XMLParser::ApplyWhitespace(const std::string& text, XMLWhitespace mode)
{
    if (mode == XMLWhitespace::PRESERVE)
        return text;
    if (mode == XMLWhitespace::TRIM)
        return TrimString(text);

    // NORMALIZE: collapse runs of whitespace to a single space
    std::string out;
    out.reserve(text.size());
    bool lastWasWS = false;
    for (char c : text)
    {
        bool ws = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
        if (ws)
        {
            if (!lastWasWS) out += ' ';
            lastWasWS = true;
        }
        else
        {
            out += c;
            lastWasWS = false;
        }
    }
    return TrimString(out);
}

// ==============================================================================================
// XMLNode - Attribute Access
// ==============================================================================================

bool XMLNode::HasAttribute(const std::string& attrName) const
{
    for (const auto& a : attributes)
        if (a.name == attrName) return true;
    return false;
}

bool XMLNode::HasAttributeNS(const std::string& nsURI, const std::string& localAttrName) const
{
    for (const auto& a : attributes)
        if (a.localName == localAttrName && a.namespaceURI == nsURI) return true;
    return false;
}

std::string XMLNode::GetAttribute(const std::string& attrName, const std::string& defaultValue) const
{
    for (const auto& a : attributes)
        if (a.name == attrName) return a.value;
    return defaultValue;
}

std::string XMLNode::GetAttributeNS(const std::string& nsURI, const std::string& localAttrName, const std::string& defaultValue) const
{
    for (const auto& a : attributes)
        if (a.localName == localAttrName && a.namespaceURI == nsURI) return a.value;
    return defaultValue;
}

void XMLNode::SetAttribute(const std::string& attrName, const std::string& attrValue)
{
    for (auto& a : attributes)
    {
        if (a.name == attrName) { a.value = attrValue; return; }
    }
    XMLAttribute newAttr(attrName, attrValue);
    // Extract prefix/localName
    size_t colon = attrName.find(':');
    if (colon != std::string::npos)
    {
        newAttr.prefix    = attrName.substr(0, colon);
        newAttr.localName = attrName.substr(colon + 1);
    }
    attributes.push_back(std::move(newAttr));
}

void XMLNode::SetAttributeNS(const std::string& nsURI, const std::string& qualifiedName, const std::string& attrValue)
{
    for (auto& a : attributes)
    {
        if (a.name == qualifiedName) { a.value = attrValue; a.namespaceURI = nsURI; return; }
    }
    XMLAttribute na(qualifiedName, attrValue);
    na.namespaceURI = nsURI;
    size_t colon = qualifiedName.find(':');
    if (colon != std::string::npos)
    {
        na.prefix    = qualifiedName.substr(0, colon);
        na.localName = qualifiedName.substr(colon + 1);
    }
    attributes.push_back(std::move(na));
}

void XMLNode::RemoveAttribute(const std::string& attrName)
{
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
        [&](const XMLAttribute& a) { return a.name == attrName; }), attributes.end());
}

void XMLNode::RemoveAttributeNS(const std::string& nsURI, const std::string& localAttrName)
{
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
        [&](const XMLAttribute& a) { return a.localName == localAttrName && a.namespaceURI == nsURI; }), attributes.end());
}

const XMLAttribute* XMLNode::FindAttribute(const std::string& attrName) const
{
    for (const auto& a : attributes)
        if (a.name == attrName) return &a;
    return nullptr;
}

XMLAttribute* XMLNode::FindAttribute(const std::string& attrName)
{
    for (auto& a : attributes)
        if (a.name == attrName) return &a;
    return nullptr;
}

// ==============================================================================================
// XMLNode - Child Navigation
// ==============================================================================================

std::shared_ptr<XMLNode> XMLNode::GetFirstChild() const
{
    return children.empty() ? nullptr : children.front();
}

std::shared_ptr<XMLNode> XMLNode::GetLastChild() const
{
    return children.empty() ? nullptr : children.back();
}

std::shared_ptr<XMLNode> XMLNode::GetNextSibling() const
{
    auto p = parent.lock();
    if (!p) return nullptr;
    auto& siblings = p->children;
    for (size_t i = 0; i < siblings.size(); ++i)
        if (siblings[i].get() == this && i + 1 < siblings.size())
            return siblings[i + 1];
    return nullptr;
}

std::shared_ptr<XMLNode> XMLNode::GetPreviousSibling() const
{
    auto p = parent.lock();
    if (!p) return nullptr;
    auto& siblings = p->children;
    for (size_t i = 1; i < siblings.size(); ++i)
        if (siblings[i].get() == this)
            return siblings[i - 1];
    return nullptr;
}

std::shared_ptr<XMLNode> XMLNode::GetFirstChildByName(const std::string& elemName) const
{
    for (const auto& c : children)
        if (c->type == XMLNodeType::ELEMENT && c->name == elemName)
            return c;
    return nullptr;
}

std::shared_ptr<XMLNode> XMLNode::GetFirstChildByLocalName(const std::string& localElemName, const std::string& nsURI) const
{
    for (const auto& c : children)
        if (c->type == XMLNodeType::ELEMENT && c->localName == localElemName &&
            (nsURI.empty() || c->namespaceURI == nsURI))
            return c;
    return nullptr;
}

std::vector<std::shared_ptr<XMLNode>> XMLNode::GetChildrenByName(const std::string& elemName) const
{
    std::vector<std::shared_ptr<XMLNode>> out;
    for (const auto& c : children)
        if (c->type == XMLNodeType::ELEMENT && c->name == elemName)
            out.push_back(c);
    return out;
}

std::vector<std::shared_ptr<XMLNode>> XMLNode::GetChildrenByLocalName(const std::string& localElemName, const std::string& nsURI) const
{
    std::vector<std::shared_ptr<XMLNode>> out;
    for (const auto& c : children)
        if (c->type == XMLNodeType::ELEMENT && c->localName == localElemName &&
            (nsURI.empty() || c->namespaceURI == nsURI))
            out.push_back(c);
    return out;
}

void XMLNode::CollectElements(const std::string& tagName, std::vector<std::shared_ptr<XMLNode>>& out) const
{
    for (const auto& c : children)
    {
        if (c->type == XMLNodeType::ELEMENT && (tagName == "*" || c->name == tagName))
            out.push_back(c);
        c->CollectElements(tagName, out);
    }
}

void XMLNode::CollectElementsByLocal(const std::string& localName, const std::string& nsURI, std::vector<std::shared_ptr<XMLNode>>& out) const
{
    for (const auto& c : children)
    {
        if (c->type == XMLNodeType::ELEMENT &&
            (localName == "*" || c->localName == localName) &&
            (nsURI.empty() || c->namespaceURI == nsURI))
            out.push_back(c);
        c->CollectElementsByLocal(localName, nsURI, out);
    }
}

std::vector<std::shared_ptr<XMLNode>> XMLNode::GetElementsByTagName(const std::string& elemName) const
{
    std::vector<std::shared_ptr<XMLNode>> out;
    CollectElements(elemName, out);
    return out;
}

std::vector<std::shared_ptr<XMLNode>> XMLNode::GetElementsByLocalName(const std::string& localElemName, const std::string& nsURI) const
{
    std::vector<std::shared_ptr<XMLNode>> out;
    CollectElementsByLocal(localElemName, nsURI, out);
    return out;
}

// ==============================================================================================
// XMLNode - Text Content
// ==============================================================================================

void XMLNode::CollectText(std::string& out) const
{
    for (const auto& c : children)
    {
        if (c->type == XMLNodeType::TEXT || c->type == XMLNodeType::CDATA_SECTION)
            out += c->value;
        else if (c->type == XMLNodeType::ELEMENT)
            c->CollectText(out);
    }
}

std::string XMLNode::GetTextContent() const
{
    std::string out;
    CollectText(out);
    return out;
}

std::string XMLNode::GetInnerText() const { return GetTextContent(); }

std::string XMLNode::GetDirectText() const
{
    std::string out;
    for (const auto& c : children)
        if (c->type == XMLNodeType::TEXT || c->type == XMLNodeType::CDATA_SECTION)
            out += c->value;
    return out;
}

void XMLNode::SetTextContent(const std::string& text)
{
    children.clear();
    if (!text.empty())
    {
        auto textNode = std::make_shared<XMLNode>(XMLNodeType::TEXT, "#text", text);
        textNode->parent = shared_from_this();
        children.push_back(std::move(textNode));
    }
}

// ==============================================================================================
// XMLNode - Child Manipulation (DOM Level 2 Core)
// ==============================================================================================

std::shared_ptr<XMLNode> XMLNode::AppendChild(std::shared_ptr<XMLNode> child)
{
    if (!child) return nullptr;
    // Remove from existing parent if any
    auto oldParent = child->parent.lock();
    if (oldParent)
        oldParent->RemoveChild(child);
    child->parent = shared_from_this();
    children.push_back(child);
    return child;
}

std::shared_ptr<XMLNode> XMLNode::PrependChild(std::shared_ptr<XMLNode> child)
{
    if (!child) return nullptr;
    auto oldParent = child->parent.lock();
    if (oldParent) oldParent->RemoveChild(child);
    child->parent = shared_from_this();
    children.insert(children.begin(), child);
    return child;
}

std::shared_ptr<XMLNode> XMLNode::InsertBefore(std::shared_ptr<XMLNode> newNode, std::shared_ptr<XMLNode> refNode)
{
    if (!newNode) return nullptr;
    if (!refNode) return AppendChild(newNode);
    auto oldParent = newNode->parent.lock();
    if (oldParent) oldParent->RemoveChild(newNode);
    auto it = std::find(children.begin(), children.end(), refNode);
    newNode->parent = shared_from_this();
    if (it != children.end())
        children.insert(it, newNode);
    else
        children.push_back(newNode);
    return newNode;
}

std::shared_ptr<XMLNode> XMLNode::InsertAfter(std::shared_ptr<XMLNode> newNode, std::shared_ptr<XMLNode> refNode)
{
    if (!newNode) return nullptr;
    if (!refNode) return AppendChild(newNode);
    auto oldParent = newNode->parent.lock();
    if (oldParent) oldParent->RemoveChild(newNode);
    auto it = std::find(children.begin(), children.end(), refNode);
    newNode->parent = shared_from_this();
    if (it != children.end())
    {
        ++it;
        children.insert(it, newNode);
    }
    else
    {
        children.push_back(newNode);
    }
    return newNode;
}

std::shared_ptr<XMLNode> XMLNode::RemoveChild(std::shared_ptr<XMLNode> child)
{
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end())
    {
        (*it)->parent.reset();
        children.erase(it);
    }
    return child;
}

std::shared_ptr<XMLNode> XMLNode::ReplaceChild(std::shared_ptr<XMLNode> newNode, std::shared_ptr<XMLNode> oldNode)
{
    if (!newNode || !oldNode) return nullptr;
    auto it = std::find(children.begin(), children.end(), oldNode);
    if (it == children.end()) return nullptr;
    auto oldParent = newNode->parent.lock();
    if (oldParent) oldParent->RemoveChild(newNode);
    oldNode->parent.reset();
    newNode->parent = shared_from_this();
    *it = newNode;
    return oldNode;
}

void XMLNode::RemoveAllChildren()
{
    for (auto& c : children) c->parent.reset();
    children.clear();
}

// ==============================================================================================
// XMLNode - Namespace Utilities
// ==============================================================================================

std::string XMLNode::LookupNamespaceURI(const std::string& nsPrefix) const
{
    // Check local mappings first
    auto it = namespaceMappings.find(nsPrefix);
    if (it != namespaceMappings.end()) return it->second;
    // Walk up the parent chain
    auto p = parent.lock();
    if (p) return p->LookupNamespaceURI(nsPrefix);
    return "";
}

std::string XMLNode::LookupPrefix(const std::string& uri) const
{
    for (const auto& [prefix, u] : namespaceMappings)
        if (u == uri) return prefix;
    auto p = parent.lock();
    if (p) return p->LookupPrefix(uri);
    return "";
}

bool XMLNode::IsDefaultNamespace(const std::string& uri) const
{
    return LookupNamespaceURI("") == uri;
}

// ==============================================================================================
// XMLNode - Utility
// ==============================================================================================

int XMLNode::GetDepth() const
{
    int depth = 0;
    auto p = parent.lock();
    while (p) { ++depth; p = p->parent.lock(); }
    return depth;
}

int XMLNode::GetChildIndex() const
{
    auto p = parent.lock();
    if (!p) return -1;
    for (int i = 0; i < static_cast<int>(p->children.size()); ++i)
        if (p->children[i].get() == this) return i;
    return -1;
}

std::shared_ptr<XMLNode> XMLNode::Clone(bool deep) const
{
    auto clone = std::make_shared<XMLNode>();
    clone->type             = type;
    clone->name             = name;
    clone->value            = value;
    clone->prefix           = prefix;
    clone->localName        = localName;
    clone->namespaceURI     = namespaceURI;
    clone->attributes       = attributes;
    clone->namespaceMappings = namespaceMappings;

    if (deep)
    {
        for (const auto& c : children)
        {
            auto childClone = c->Clone(true);
            childClone->parent = clone;
            clone->children.push_back(std::move(childClone));
        }
    }
    return clone;
}

std::string XMLNode::ToString(bool pretty, int indentLevel, int indentWidth) const
{
    XMLSerializer ser;
    XMLSerializer::Options opts;
    opts.pretty         = pretty;
    opts.indentWidth    = indentWidth;
    opts.writeDeclaration = false;
    ser.SetOptions(opts);
    return ser.SerializeNode(std::const_pointer_cast<XMLNode>(shared_from_this()), indentLevel);
}

// ==============================================================================================
// XMLDocument - Construction
// ==============================================================================================

XMLDocument::XMLDocument()
    : root(std::make_shared<XMLNode>(XMLNodeType::DOCUMENT, "#document"))
    , version("1.0")
    , encoding("UTF-8")
    , standalone(false)
{
}

// ==============================================================================================
// XMLDocument - Structure Access
// ==============================================================================================

std::shared_ptr<XMLNode> XMLDocument::GetDocumentElement() const
{
    for (const auto& c : root->children)
        if (c->type == XMLNodeType::ELEMENT) return c;
    return nullptr;
}

std::shared_ptr<XMLNode> XMLDocument::GetDocType() const
{
    for (const auto& c : root->children)
        if (c->type == XMLNodeType::DOCUMENT_TYPE) return c;
    return nullptr;
}

// ==============================================================================================
// XMLDocument - Factory Methods
// ==============================================================================================

std::shared_ptr<XMLNode> XMLDocument::CreateElement(const std::string& tagName) const
{
    auto node = std::make_shared<XMLNode>(XMLNodeType::ELEMENT, tagName);
    size_t colon = tagName.find(':');
    if (colon != std::string::npos)
    {
        node->prefix    = tagName.substr(0, colon);
        node->localName = tagName.substr(colon + 1);
    }
    return node;
}

std::shared_ptr<XMLNode> XMLDocument::CreateElementNS(const std::string& nsURI, const std::string& qualifiedName) const
{
    auto node = CreateElement(qualifiedName);
    node->namespaceURI = nsURI;
    return node;
}

std::shared_ptr<XMLNode> XMLDocument::CreateTextNode(const std::string& text) const
{
    return std::make_shared<XMLNode>(XMLNodeType::TEXT, "#text", text);
}

std::shared_ptr<XMLNode> XMLDocument::CreateCDATASection(const std::string& data) const
{
    return std::make_shared<XMLNode>(XMLNodeType::CDATA_SECTION, "#cdata-section", data);
}

std::shared_ptr<XMLNode> XMLDocument::CreateComment(const std::string& comment) const
{
    return std::make_shared<XMLNode>(XMLNodeType::COMMENT, "#comment", comment);
}

std::shared_ptr<XMLNode> XMLDocument::CreateProcessingInstruction(const std::string& target, const std::string& data) const
{
    auto node = std::make_shared<XMLNode>(XMLNodeType::PROCESSING_INSTRUCTION, target, data);
    return node;
}

// ==============================================================================================
// XMLDocument - DOM Search
// ==============================================================================================

static void FindById(const std::shared_ptr<XMLNode>& node, const std::string& id, std::shared_ptr<XMLNode>& result)
{
    if (!node || result) return;
    if (node->type == XMLNodeType::ELEMENT)
    {
        std::string idVal = node->GetAttribute("id");
        if (idVal.empty()) idVal = node->GetAttribute("ID");
        if (idVal == id) { result = node; return; }
    }
    for (const auto& c : node->children)
        FindById(c, id, result);
}

std::shared_ptr<XMLNode> XMLDocument::GetElementById(const std::string& id) const
{
    std::shared_ptr<XMLNode> result;
    FindById(root, id, result);
    return result;
}

std::vector<std::shared_ptr<XMLNode>> XMLDocument::GetElementsByTagName(const std::string& tagName) const
{
    std::vector<std::shared_ptr<XMLNode>> out;
    auto docElem = GetDocumentElement();
    if (docElem) docElem->CollectElements(tagName, out);
    return out;
}

std::vector<std::shared_ptr<XMLNode>> XMLDocument::GetElementsByLocalName(const std::string& localName, const std::string& nsURI) const
{
    std::vector<std::shared_ptr<XMLNode>> out;
    auto docElem = GetDocumentElement();
    if (docElem) docElem->CollectElementsByLocal(localName, nsURI, out);
    return out;
}

// ==============================================================================================
// XMLDocument - Node Import
// ==============================================================================================

std::shared_ptr<XMLNode> XMLDocument::ImportNode(const std::shared_ptr<XMLNode>& node, bool deep) const
{
    if (!node) return nullptr;
    return node->Clone(deep);
}

// ==============================================================================================
// XMLDocument - Serialization
// ==============================================================================================

std::string XMLDocument::ToString(bool pretty, int indentWidth) const
{
    XMLSerializer ser;
    XMLSerializer::Options opts;
    opts.pretty         = pretty;
    opts.indentWidth    = indentWidth;
    opts.encoding       = encoding;
    ser.SetOptions(opts);
    return ser.Serialize(*this);
}

bool XMLDocument::SaveToFile(const std::string& filepath, bool pretty, int indentWidth) const
{
    XMLSerializer ser;
    XMLSerializer::Options opts;
    opts.pretty         = pretty;
    opts.indentWidth    = indentWidth;
    opts.encoding       = encoding;
    ser.SetOptions(opts);
    return ser.WriteToFile(*this, filepath);
}

bool XMLDocument::SaveToFile(const std::wstring& filepath, bool pretty, int indentWidth) const
{
    XMLSerializer ser;
    XMLSerializer::Options opts;
    opts.pretty         = pretty;
    opts.indentWidth    = indentWidth;
    opts.encoding       = encoding;
    ser.SetOptions(opts);
    return ser.WriteToFile(*this, filepath);
}

// ==============================================================================================
// XMLDocument - XPath Engine
// ==============================================================================================

static std::vector<std::string> SplitXPathUnion(const std::string& expr)
{
    // Split on '|' outside of predicates '[ ... ]'
    std::vector<std::string> parts;
    int depth = 0;
    std::string cur;
    for (char c : expr)
    {
        if (c == '[') { ++depth; cur += c; }
        else if (c == ']') { --depth; cur += c; }
        else if (c == '|' && depth == 0)
        {
            parts.push_back(TrimString(cur));
            cur.clear();
        }
        else cur += c;
    }
    if (!cur.empty()) parts.push_back(TrimString(cur));
    return parts;
}

static std::vector<std::string> SplitXPathSteps(const std::string& path)
{
    // Split on '/' but treat '//' as a single token
    // Returns individual step strings; '//' becomes an empty string followed by the next step
    std::vector<std::string> steps;
    size_t i = 0;
    std::string cur;
    int depth = 0;
    while (i < path.size())
    {
        if (path[i] == '[') { ++depth; cur += path[i++]; }
        else if (path[i] == ']') { --depth; cur += path[i++]; }
        else if (path[i] == '/' && depth == 0)
        {
            steps.push_back(cur);
            cur.clear();
            ++i;
            if (i < path.size() && path[i] == '/')
            {
                steps.push_back("//");                                  // Descendant shortcut marker
                ++i;
            }
        }
        else cur += path[i++];
    }
    steps.push_back(cur);
    return steps;
}

void XMLDocument::ParsePredicate(const std::string& pred, XPathStep& step) const
{
    std::string p = TrimString(pred);
    if (p.empty()) return;

    // Positional: [n] or [last()]
    if (p == "last()")
    {
        step.positionIsLast = true;
        step.positionPredicate = 0;
        return;
    }
    bool allDigits = !p.empty();
    for (char c : p) if (!isdigit(static_cast<unsigned char>(c))) { allDigits = false; break; }
    if (allDigits)
    {
        step.positionPredicate = std::stoi(p);
        return;
    }

    // Attribute test: [@name] or [@name='value'] or [@name="value"]
    if (!p.empty() && p[0] == '@')
    {
        std::string rest = p.substr(1);
        size_t eq = rest.find('=');
        if (eq == std::string::npos)
        {
            step.predicateAttr  = TrimString(rest);
            step.hasAttrPredicate = true;
        }
        else
        {
            step.predicateAttr      = TrimString(rest.substr(0, eq));
            step.hasAttrPredicate   = true;
            std::string val         = TrimString(rest.substr(eq + 1));
            if (val.size() >= 2 && (val.front() == '\'' || val.front() == '"') && val.back() == val.front())
                val = val.substr(1, val.size() - 2);
            step.predicateAttrValue = val;
            step.hasAttrValueTest   = true;
        }
        return;
    }

    // Text content: [text()='value']
    if (p.substr(0, 6) == "text()")
    {
        size_t eq = p.find('=');
        if (eq != std::string::npos)
        {
            std::string val = TrimString(p.substr(eq + 1));
            if (val.size() >= 2 && (val.front() == '\'' || val.front() == '"') && val.back() == val.front())
                val = val.substr(1, val.size() - 2);
            step.predicateTextValue = val;
            step.hasTextValueTest   = true;
        }
        return;
    }

    // Child element test: [elem] or [elem='value']
    size_t eq = p.find('=');
    if (eq != std::string::npos)
    {
        step.predicateElemName  = TrimString(p.substr(0, eq));
        std::string val         = TrimString(p.substr(eq + 1));
        if (val.size() >= 2 && (val.front() == '\'' || val.front() == '"') && val.back() == val.front())
            val = val.substr(1, val.size() - 2);
        step.predicateElemValue = val;
        step.hasElemValueTest   = true;
    }
    else
    {
        // Child element existence: [elem]
        step.predicateElemExist = TrimString(p);
        step.hasElemExistTest   = true;
    }
}

XMLDocument::XPathStep XMLDocument::ParseXPathStep(const std::string& stepStr, bool isDescendant) const
{
    XPathStep step;
    step.descendantShortcut = isDescendant;

    // Extract predicate if any
    std::string base = stepStr;
    size_t predStart = stepStr.find('[');
    if (predStart != std::string::npos)
    {
        size_t predEnd = stepStr.rfind(']');
        if (predEnd != std::string::npos && predEnd > predStart)
        {
            ParsePredicate(stepStr.substr(predStart + 1, predEnd - predStart - 1), step);
            base = stepStr.substr(0, predStart);
        }
    }
    base = TrimString(base);

    // Check for explicit axis (child::, descendant::, etc.)
    size_t axisColon = base.find("::");
    if (axisColon != std::string::npos)
    {
        std::string axisStr = base.substr(0, axisColon);
        base = base.substr(axisColon + 2);
        if (axisStr == "child")                  step.axis = XMLXPathAxis::CHILD;
        else if (axisStr == "descendant")        step.axis = XMLXPathAxis::DESCENDANT;
        else if (axisStr == "descendant-or-self")step.axis = XMLXPathAxis::DESCENDANT_OR_SELF;
        else if (axisStr == "parent")            step.axis = XMLXPathAxis::PARENT;
        else if (axisStr == "ancestor")          step.axis = XMLXPathAxis::ANCESTOR;
        else if (axisStr == "ancestor-or-self")  step.axis = XMLXPathAxis::ANCESTOR_OR_SELF;
        else if (axisStr == "self")              step.axis = XMLXPathAxis::SELF;
        else if (axisStr == "attribute")         step.axis = XMLXPathAxis::ATTRIBUTE;
        else if (axisStr == "following-sibling") step.axis = XMLXPathAxis::FOLLOWING_SIBLING;
        else if (axisStr == "preceding-sibling") step.axis = XMLXPathAxis::PRECEDING_SIBLING;
    }
    else if (base == "..")
    {
        step.axis = XMLXPathAxis::PARENT;
        base = "node()";
        step.isNodeTest = true;
    }
    else if (base == ".")
    {
        step.axis = XMLXPathAxis::SELF;
        base = "node()";
        step.isNodeTest = true;
    }
    else if (!base.empty() && base[0] == '@')
    {
        step.axis = XMLXPathAxis::ATTRIBUTE;
        base = base.substr(1);
    }

    // Determine node test
    if (base == "*")
    {
        step.isWildcard = true;
    }
    else if (base == "text()")
    {
        step.isTextTest = true;
    }
    else if (base == "comment()")
    {
        step.isCommentTest = true;
    }
    else if (base == "processing-instruction()" || base.substr(0, 24) == "processing-instruction()")
    {
        step.isPITest = true;
    }
    else if (base == "node()")
    {
        step.isNodeTest = true;
    }
    else
    {
        step.nodeTest = base;
    }

    return step;
}

std::vector<XMLDocument::XPathStep> XMLDocument::ParseXPath(const std::string& expression) const
{
    std::vector<XPathStep> steps;
    std::string expr = TrimString(expression);

    bool isAbsolute = (!expr.empty() && expr[0] == '/');
    if (isAbsolute)
    {
        // Absolute path: add document-root step
        XPathStep rootStep;
        rootStep.axis      = XMLXPathAxis::SELF;
        rootStep.isNodeTest = true;
        rootStep.nodeTest  = "__root__";
        steps.push_back(rootStep);
        expr = expr.substr(1);
        if (!expr.empty() && expr[0] == '/')
        {
            // '//' at start = descendant-or-self::node()
            XPathStep descStep;
            descStep.axis           = XMLXPathAxis::DESCENDANT_OR_SELF;
            descStep.isNodeTest     = true;
            descStep.descendantShortcut = true;
            steps.push_back(descStep);
            expr = expr.substr(1);
        }
    }

    std::vector<std::string> rawSteps = SplitXPathSteps(expr);
    bool nextIsDescendant = false;
    for (const auto& raw : rawSteps)
    {
        if (raw == "//")
        {
            nextIsDescendant = true;
            continue;
        }
        if (raw.empty())
        {
            nextIsDescendant = true;
            continue;
        }
        steps.push_back(ParseXPathStep(raw, nextIsDescendant));
        nextIsDescendant = false;
    }
    return steps;
}

bool XMLDocument::MatchesNodeTest(const XPathStep& step, const std::shared_ptr<XMLNode>& node) const
{
    if (step.isNodeTest)    return true;
    if (step.isWildcard)    return node->type == XMLNodeType::ELEMENT;
    if (step.isTextTest)    return node->type == XMLNodeType::TEXT || node->type == XMLNodeType::CDATA_SECTION;
    if (step.isCommentTest) return node->type == XMLNodeType::COMMENT;
    if (step.isPITest)      return node->type == XMLNodeType::PROCESSING_INSTRUCTION;
    if (step.axis == XMLXPathAxis::ATTRIBUTE)
        return true;                                                    // Attribute checks done separately
    return node->type == XMLNodeType::ELEMENT &&
           (step.nodeTest == "*" || node->name == step.nodeTest || node->localName == step.nodeTest);
}

bool XMLDocument::MatchesPredicate(const XPathStep& step, const std::shared_ptr<XMLNode>& node, int pos, int total) const
{
    if (step.positionIsLast && pos != total) return false;
    if (!step.positionIsLast && step.positionPredicate >= 1 && pos != step.positionPredicate) return false;

    if (step.hasAttrPredicate)
    {
        if (!node->HasAttribute(step.predicateAttr)) return false;
        if (step.hasAttrValueTest && node->GetAttribute(step.predicateAttr) != step.predicateAttrValue) return false;
    }
    if (step.hasTextValueTest && node->GetDirectText() != step.predicateTextValue) return false;
    if (step.hasElemValueTest)
    {
        auto child = node->GetFirstChildByName(step.predicateElemName);
        if (!child || child->GetDirectText() != step.predicateElemValue) return false;
    }
    if (step.hasElemExistTest)
    {
        if (!node->GetFirstChildByName(step.predicateElemExist)) return false;
    }
    return true;
}

void XMLDocument::GetDescendants(const std::shared_ptr<XMLNode>& node, std::vector<std::shared_ptr<XMLNode>>& out) const
{
    for (const auto& c : node->children)
    {
        out.push_back(c);
        GetDescendants(c, out);
    }
}

void XMLDocument::GetAncestors(const std::shared_ptr<XMLNode>& node, std::vector<std::shared_ptr<XMLNode>>& out) const
{
    auto p = node->parent.lock();
    while (p) { out.push_back(p); p = p->parent.lock(); }
}

void XMLDocument::CollectAxis(const XPathStep& step, std::shared_ptr<XMLNode> ctx, std::vector<std::shared_ptr<XMLNode>>& out) const
{
    if (!ctx) return;
    switch (step.axis)
    {
        case XMLXPathAxis::SELF:
            out.push_back(ctx);
            break;

        case XMLXPathAxis::CHILD:
            for (const auto& c : ctx->children) out.push_back(c);
            break;

        case XMLXPathAxis::DESCENDANT:
            GetDescendants(ctx, out);
            break;

        case XMLXPathAxis::DESCENDANT_OR_SELF:
            out.push_back(ctx);
            GetDescendants(ctx, out);
            break;

        case XMLXPathAxis::PARENT:
        {
            auto p = ctx->parent.lock();
            if (p) out.push_back(p);
            break;
        }
        case XMLXPathAxis::ANCESTOR:
            GetAncestors(ctx, out);
            break;

        case XMLXPathAxis::ANCESTOR_OR_SELF:
            out.push_back(ctx);
            GetAncestors(ctx, out);
            break;

        case XMLXPathAxis::ATTRIBUTE:
            // Return synthetic attribute nodes
            for (const auto& a : ctx->attributes)
            {
                auto attrNode = std::make_shared<XMLNode>(XMLNodeType::ATTRIBUTE, a.name, a.value);
                attrNode->localName    = a.localName;
                attrNode->prefix       = a.prefix;
                attrNode->namespaceURI = a.namespaceURI;
                attrNode->parent       = ctx;
                if (step.isWildcard || step.nodeTest.empty() || step.nodeTest == a.name || step.nodeTest == a.localName)
                    out.push_back(std::move(attrNode));
            }
            break;

        case XMLXPathAxis::FOLLOWING_SIBLING:
        {
            auto p = ctx->parent.lock();
            if (!p) break;
            bool found = false;
            for (const auto& c : p->children)
            {
                if (c == ctx) { found = true; continue; }
                if (found) out.push_back(c);
            }
            break;
        }
        case XMLXPathAxis::PRECEDING_SIBLING:
        {
            auto p = ctx->parent.lock();
            if (!p) break;
            for (const auto& c : p->children)
            {
                if (c == ctx) break;
                out.push_back(c);
            }
            break;
        }
    }
}

std::vector<std::shared_ptr<XMLNode>> XMLDocument::EvaluateXPath(const std::vector<XPathStep>& steps, std::shared_ptr<XMLNode> context) const
{
    std::vector<std::shared_ptr<XMLNode>> current;
    current.push_back(context);

    for (size_t si = 0; si < steps.size(); ++si)
    {
        const XPathStep& step = steps[si];

        // Special: root step (absolute path anchor)
        if (step.nodeTest == "__root__")
        {
            current.clear();
            current.push_back(root);
            continue;
        }

        std::vector<std::shared_ptr<XMLNode>> next;

        if (step.descendantShortcut && step.axis != XMLXPathAxis::DESCENDANT_OR_SELF)
        {
            // '//' before this step: expand each context to descendant-or-self, then apply step
            std::vector<std::shared_ptr<XMLNode>> expanded;
            for (const auto& ctx : current)
            {
                expanded.push_back(ctx);
                GetDescendants(ctx, expanded);
            }
            // Now apply the step's node test and predicate across expanded set
            std::vector<std::shared_ptr<XMLNode>> matched;
            for (const auto& node : expanded)
            {
                std::vector<std::shared_ptr<XMLNode>> axisNodes;
                CollectAxis(step, node, axisNodes);
                for (const auto& an : axisNodes)
                    if (MatchesNodeTest(step, an))
                        matched.push_back(an);
            }
            // Remove duplicates (maintain order)
            std::unordered_set<XMLNode*> seen;
            int pos = 1;
            for (auto& m : matched)
            {
                if (seen.insert(m.get()).second)
                {
                    if (MatchesPredicate(step, m, pos, static_cast<int>(matched.size())))
                        next.push_back(m);
                    ++pos;
                }
            }
        }
        else
        {
            for (const auto& ctx : current)
            {
                std::vector<std::shared_ptr<XMLNode>> axisNodes;
                CollectAxis(step, ctx, axisNodes);

                int total = 0;
                for (const auto& an : axisNodes)
                    if (MatchesNodeTest(step, an)) ++total;

                int pos = 1;
                for (const auto& an : axisNodes)
                {
                    if (!MatchesNodeTest(step, an)) continue;
                    if (MatchesPredicate(step, an, pos, total))
                        next.push_back(an);
                    ++pos;
                }
            }
        }
        current = std::move(next);
    }
    return current;
}

std::vector<std::shared_ptr<XMLNode>> XMLDocument::XPathQuery(const std::string& expression, std::shared_ptr<XMLNode> context) const
{
    if (!context) context = root;

    // Handle union expressions
    std::vector<std::string> parts = SplitXPathUnion(expression);
    if (parts.size() > 1)
    {
        std::vector<std::shared_ptr<XMLNode>> result;
        std::unordered_set<XMLNode*> seen;
        for (const auto& part : parts)
        {
            auto sub = XPathQuery(part, context);
            for (auto& n : sub)
                if (seen.insert(n.get()).second)
                    result.push_back(n);
        }
        return result;
    }

    auto steps = ParseXPath(expression);
    return EvaluateXPath(steps, context);
}

std::shared_ptr<XMLNode> XMLDocument::XPathQuerySingle(const std::string& expression, std::shared_ptr<XMLNode> context) const
{
    auto results = XPathQuery(expression, context);
    return results.empty() ? nullptr : results.front();
}

std::string XMLDocument::XPathQueryString(const std::string& expression, std::shared_ptr<XMLNode> context) const
{
    auto node = XPathQuerySingle(expression, context);
    if (!node) return "";
    if (node->type == XMLNodeType::ATTRIBUTE) return node->value;
    return node->GetTextContent();
}

double XMLDocument::XPathQueryNumber(const std::string& expression, std::shared_ptr<XMLNode> context) const
{
    std::string s = XPathQueryString(expression, context);
    try { return std::stod(s); } catch (...) { return 0.0; }
}

bool XMLDocument::XPathQueryBool(const std::string& expression, std::shared_ptr<XMLNode> context) const
{
    return !XPathQuery(expression, context).empty();
}

// ==============================================================================================
// XMLParser - Construction
// ==============================================================================================

XMLParser::XMLParser()
{
    // Register the 5 predefined XML entities (they are always available but we seed the table)
    for (const auto& [k, v] : s_BuiltinEntities)
        mCustomEntities[k] = v;
}

// ==============================================================================================
// XMLParser - Public API
// ==============================================================================================

XMLParseResult XMLParser::ParseFile(const std::string& filepath, XMLDocument& outDoc)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        XMLParseResult r;
        r.error = XMLParseError::FILE_NOT_FOUND;
        r.description = "Cannot open file: " + filepath;
        #ifdef _DEBUG_XMLPARSER_
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"XMLParser: " + WidenForXMLLog(r.description));
        #endif
        return r;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    return ParseBytes(bytes, outDoc);
}

XMLParseResult XMLParser::ParseFile(const std::wstring& filepath, XMLDocument& outDoc)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        XMLParseResult r;
        r.error = XMLParseError::FILE_NOT_FOUND;
        r.description = "Cannot open file";
        #ifdef _DEBUG_XMLPARSER_
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"XMLParser: Cannot open wide-path file: " + filepath);
        #endif
        return r;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    return ParseBytes(bytes, outDoc);
}

XMLParseResult XMLParser::ParseString(const std::string& xmlContent, XMLDocument& outDoc)
{
    return DoParse(xmlContent.c_str(), xmlContent.size(), &outDoc, nullptr);
}

XMLParseResult XMLParser::ParseBytes(const std::vector<uint8_t>& data, XMLDocument& outDoc)
{
    std::string declEnc;
    XMLEncoding enc = DetectEncoding(data.data(), data.size(), declEnc);
    std::string utf8 = ConvertToUTF8(data.data(), data.size(), enc);
    utf8 = NormalizeLineEndings(utf8);
    return DoParse(utf8.c_str(), utf8.size(), &outDoc, nullptr);
}

XMLParseResult XMLParser::ParseFileSAX(const std::string& filepath, const XMLSAXCallbacks& callbacks)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        XMLParseResult r;
        r.error = XMLParseError::FILE_NOT_FOUND;
        r.description = "Cannot open file: " + filepath;
        #ifdef _DEBUG_XMLPARSER_
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"XMLParser SAX: " + WidenForXMLLog(r.description));
        #endif
        return r;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    return ParseBytesSAX(bytes, callbacks);
}

XMLParseResult XMLParser::ParseFileSAX(const std::wstring& filepath, const XMLSAXCallbacks& callbacks)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        XMLParseResult r;
        r.error = XMLParseError::FILE_NOT_FOUND;
        r.description = "Cannot open file";
        #ifdef _DEBUG_XMLPARSER_
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"XMLParser SAX: Cannot open wide-path file: " + filepath);
        #endif
        return r;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    return ParseBytesSAX(bytes, callbacks);
}

XMLParseResult XMLParser::ParseStringSAX(const std::string& xmlContent, const XMLSAXCallbacks& callbacks)
{
    return DoParse(xmlContent.c_str(), xmlContent.size(), nullptr, &callbacks);
}

XMLParseResult XMLParser::ParseBytesSAX(const std::vector<uint8_t>& data, const XMLSAXCallbacks& callbacks)
{
    std::string declEnc;
    XMLEncoding enc = DetectEncoding(data.data(), data.size(), declEnc);
    std::string utf8 = ConvertToUTF8(data.data(), data.size(), enc);
    utf8 = NormalizeLineEndings(utf8);
    return DoParse(utf8.c_str(), utf8.size(), nullptr, &callbacks);
}

// ==============================================================================================
// XMLParser - Character Lexer
// ==============================================================================================

char XMLParser::PeekChar(const ParseState& state, int offset) const
{
    size_t p = state.pos + static_cast<size_t>(offset);
    if (p >= state.length) return '\0';
    return state.data[p];
}

char XMLParser::ReadChar(ParseState& state)
{
    if (state.pos >= state.length) return '\0';
    char c = state.data[state.pos++];
    if (c == '\n') { ++state.line; state.col = 1; }
    else           { ++state.col; }
    return c;
}

bool XMLParser::IsAtEnd(const ParseState& state) const
{
    return state.pos >= state.length;
}

bool XMLParser::IsWhitespace(char c) const
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool XMLParser::IsNameStartChar(unsigned char c) const
{
    return (c == ':' || c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0);
}

bool XMLParser::IsNameChar(unsigned char c) const
{
    return (IsNameStartChar(c) || c == '-' || c == '.' || (c >= '0' && c <= '9') || c == 0xB7);
}

void XMLParser::SkipWhitespace(ParseState& state)
{
    while (!IsAtEnd(state) && IsWhitespace(PeekChar(state)))
        ReadChar(state);
}

bool XMLParser::ExpectChar(ParseState& state, char c, XMLParseError err)
{
    if (IsAtEnd(state) || PeekChar(state) != c)
        return SetError(state, err, std::string("Expected '") + c + "'");
    ReadChar(state);
    return true;
}

bool XMLParser::ExpectString(ParseState& state, const char* str, XMLParseError err)
{
    for (size_t i = 0; str[i]; ++i)
    {
        if (IsAtEnd(state) || PeekChar(state) != str[i])
            return SetError(state, err, std::string("Expected '") + str + "'");
        ReadChar(state);
    }
    return true;
}

bool XMLParser::MatchString(const ParseState& state, const char* str, int offset) const
{
    size_t i = 0;
    while (str[i])
    {
        if (state.pos + offset + i >= state.length) return false;
        if (state.data[state.pos + offset + i] != str[i]) return false;
        ++i;
    }
    return true;
}

std::string XMLParser::ReadName(ParseState& state)
{
    std::string name;
    if (IsAtEnd(state) || !IsNameStartChar(static_cast<unsigned char>(PeekChar(state))))
        return name;
    while (!IsAtEnd(state) && IsNameChar(static_cast<unsigned char>(PeekChar(state))))
        name += ReadChar(state);
    return name;
}

std::string XMLParser::ReadAttributeValue(ParseState& state, char quoteChar)
{
    std::string raw;
    while (!IsAtEnd(state))
    {
        char c = PeekChar(state);
        if (c == quoteChar) break;
        if (c == '<')
        {
            SetError(state, XMLParseError::INVALID_ATTRIBUTE, "Attribute value contains '<'");
            break;
        }
        raw += ReadChar(state);
    }
    // Expand entity references in the attribute value
    std::string result = ExpandEntitiesInText(state, raw);
    // Normalize attribute whitespace (XML spec 3.3.3): newlines/tabs -> spaces
    for (char& ch : result)
        if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
    return result;
}

std::string XMLParser::ReadUntilString(ParseState& state, const char* endStr, bool includeEnd)
{
    std::string out;
    size_t endLen = strlen(endStr);
    while (!IsAtEnd(state))
    {
        if (MatchString(state, endStr))
        {
            if (includeEnd)
                for (size_t i = 0; i < endLen; ++i) ReadChar(state);
            break;
        }
        out += ReadChar(state);
    }
    return out;
}

std::string XMLParser::ReadQuotedLiteral(ParseState& state)
{
    if (IsAtEnd(state)) return "";
    char q = PeekChar(state);
    if (q != '"' && q != '\'') return "";
    ReadChar(state);
    std::string val;
    while (!IsAtEnd(state) && PeekChar(state) != q)
        val += ReadChar(state);
    if (!IsAtEnd(state)) ReadChar(state);                              // Consume closing quote
    return val;
}

// ==============================================================================================
// XMLParser - Entity Resolution
// ==============================================================================================

std::string XMLParser::ResolveCharacterReference(const std::string& ref)
{
    // ref is what's between '&#' and ';', so either digits or 'x' + hex digits
    uint32_t cp = 0;
    try
    {
        if (!ref.empty() && ref[0] == 'x')
            cp = std::stoul(ref.substr(1), nullptr, 16);
        else
            cp = std::stoul(ref, nullptr, 10);
    }
    catch (...) { return ""; }

    // Encode as UTF-8
    std::string out;
    if (cp < 0x80)      { out += static_cast<char>(cp); }
    else if (cp < 0x800){ out += static_cast<char>(0xC0|(cp>>6)); out += static_cast<char>(0x80|(cp&0x3F)); }
    else if (cp <0x10000){ out+=static_cast<char>(0xE0|(cp>>12)); out+=static_cast<char>(0x80|((cp>>6)&0x3F)); out+=static_cast<char>(0x80|(cp&0x3F)); }
    else                { out+=static_cast<char>(0xF0|(cp>>18)); out+=static_cast<char>(0x80|((cp>>12)&0x3F)); out+=static_cast<char>(0x80|((cp>>6)&0x3F)); out+=static_cast<char>(0x80|(cp&0x3F)); }
    return out;
}

std::string XMLParser::ResolveEntityReference(ParseState& state, const std::string& name)
{
    // Check built-ins first
    auto it = s_BuiltinEntities.find(name);
    if (it != s_BuiltinEntities.end()) return it->second;

    // Check document entities
    auto it2 = state.entities.find(name);
    if (it2 != state.entities.end())
    {
        ++state.entityExpansions;
        if (state.entityExpansions > mEntityExpansionLimit)
        {
            SetError(state, XMLParseError::ENTITY_EXPANSION_LIMIT, "Entity expansion limit exceeded (XXE protection)");
            return "";
        }
        return it2->second;
    }

    // Unknown entity
    if (mValidationMode)
        SetError(state, XMLParseError::INVALID_ENTITY, "Undeclared entity: &" + name + ";");
    return "&" + name + ";";                                            // Leave unknown entity as-is in lenient mode
}

std::string XMLParser::ExpandEntitiesInText(ParseState& state, const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    size_t i = 0;
    while (i < raw.size())
    {
        if (raw[i] != '&') { out += raw[i++]; continue; }

        size_t semi = raw.find(';', i + 1);
        if (semi == std::string::npos) { out += raw[i++]; continue; }

        std::string ref = raw.substr(i + 1, semi - i - 1);
        if (!ref.empty() && ref[0] == '#')
        {
            out += ResolveCharacterReference(ref.substr(1));
        }
        else
        {
            out += ResolveEntityReference(state, ref);
        }
        i = semi + 1;
    }
    return out;
}

// ==============================================================================================
// XMLParser - Namespace Handling
// ==============================================================================================

void XMLParser::SplitQName(const std::string& qname, std::string& outPrefix, std::string& outLocal) const
{
    size_t colon = qname.find(':');
    if (colon == std::string::npos)
    {
        outPrefix.clear();
        outLocal = qname;
    }
    else
    {
        outPrefix = qname.substr(0, colon);
        outLocal  = qname.substr(colon + 1);
    }
}

void XMLParser::PushNamespaceScope(ParseState& state, const std::unordered_map<std::string, std::string>& localMap)
{
    std::unordered_map<std::string, std::string> merged;
    if (!state.nsStack.empty()) merged = state.nsStack.top();
    for (const auto& [k, v] : localMap) merged[k] = v;
    state.nsStack.push(std::move(merged));
}

void XMLParser::PopNamespaceScope(ParseState& state)
{
    if (!state.nsStack.empty()) state.nsStack.pop();
}

std::string XMLParser::LookupNamespace(const ParseState& state, const std::string& prefix) const
{
    if (!state.nsStack.empty())
    {
        const auto& top = state.nsStack.top();
        auto it = top.find(prefix);
        if (it != top.end()) return it->second;
    }
    return "";
}

void XMLParser::ResolveNodeNamespaces(std::shared_ptr<XMLNode> node, const ParseState& state)
{
    if (!mProcessNamespaces) return;
    std::string prefix, local;
    SplitQName(node->name, prefix, local);
    node->prefix        = prefix;
    node->localName     = local;
    node->namespaceURI  = LookupNamespace(state, prefix);

    for (auto& attr : node->attributes)
    {
        if (attr.name == "xmlns" || attr.name.substr(0, 6) == "xmlns:") continue;
        SplitQName(attr.name, attr.prefix, attr.localName);
        if (!attr.prefix.empty())
            attr.namespaceURI = LookupNamespace(state, attr.prefix);
    }
}

// ==============================================================================================
// XMLParser - Error Handling
// ==============================================================================================

XMLParseResult XMLParser::MakeError(const ParseState& state, XMLParseError code, const std::string& desc)
{
    XMLParseResult r;
    r.error       = code;
    r.line        = state.line;
    r.column      = state.col;
    r.description = desc;
    return r;
}

bool XMLParser::SetError(ParseState& state, XMLParseError code, const std::string& desc)
{
    if (!state.hasError)
    {
        state.hasError    = true;
        state.errorResult = MakeError(state, code, desc);
        if (state.callbacks && state.callbacks->onError)
            state.callbacks->onError(code, state.line, state.col, desc);

        #ifdef _DEBUG_XMLPARSER_
        debug.logDebugMessage(LogLevel::LOG_WARNING,
            L"XMLParser: Parse error %d at line %d, column %d: %hs",
            static_cast<int>(code), state.line, state.col, desc.c_str());
        #endif
    }
    return false;
}

// ==============================================================================================
// XMLParser - Prolog Parsing
// ==============================================================================================

bool XMLParser::ParseXMLDeclaration(ParseState& state, std::shared_ptr<XMLNode> parent)
{
    // We are positioned right after "<?xml"
    std::string version, encoding, standalone;

    SkipWhitespace(state);
    while (!IsAtEnd(state) && PeekChar(state) != '?')
    {
        std::string attrName = ReadName(state);
        if (attrName.empty()) break;
        SkipWhitespace(state);
        if (!ExpectChar(state, '=', XMLParseError::INVALID_XML_DECLARATION)) return false;
        SkipWhitespace(state);
        char q = PeekChar(state);
        if (q != '"' && q != '\'')
            return SetError(state, XMLParseError::INVALID_XML_DECLARATION, "Expected quoted value in XML declaration");
        ReadChar(state);
        std::string val;
        while (!IsAtEnd(state) && PeekChar(state) != q) val += ReadChar(state);
        if (!IsAtEnd(state)) ReadChar(state);

        if (attrName == "version")    version    = val;
        else if (attrName == "encoding") encoding = val;
        else if (attrName == "standalone") standalone = val;
        SkipWhitespace(state);
    }
    if (!ExpectString(state, "?>", XMLParseError::INVALID_XML_DECLARATION)) return false;

    if (state.doc)
    {
        if (!version.empty())  state.doc->version  = version;
        if (!encoding.empty()) state.doc->encoding = encoding;
        state.doc->standalone = (standalone == "yes");
    }
    if (state.callbacks && state.callbacks->onXMLDeclaration)
        state.callbacks->onXMLDeclaration(version, encoding, standalone == "yes");

    state.sawXMLDecl = true;
    return true;
}

bool XMLParser::ParseDocType(ParseState& state)
{
    // We are positioned right after "<!DOCTYPE"
    SkipWhitespace(state);
    std::string name = ReadName(state);
    std::string systemId, publicId, internalSubset;
    SkipWhitespace(state);

    // Optional SYSTEM or PUBLIC identifier
    if (MatchString(state, "SYSTEM"))
    {
        ExpectString(state, "SYSTEM");
        SkipWhitespace(state);
        systemId = ReadQuotedLiteral(state);
        SkipWhitespace(state);
    }
    else if (MatchString(state, "PUBLIC"))
    {
        ExpectString(state, "PUBLIC");
        SkipWhitespace(state);
        publicId = ReadQuotedLiteral(state);
        SkipWhitespace(state);
        if (PeekChar(state) == '"' || PeekChar(state) == '\'')
            systemId = ReadQuotedLiteral(state);
        SkipWhitespace(state);
    }

    // Optional internal subset [...]
    if (!IsAtEnd(state) && PeekChar(state) == '[')
    {
        ReadChar(state);                                                // Consume '['
        internalSubset = ReadUntilString(state, "]", true);
        internalSubset.pop_back();                                      // Remove trailing ']'
        SkipWhitespace(state);

        // Parse internal subset for ENTITY/ATTLIST/ELEMENT/NOTATION decls
        state.inDTD = true;
        const char* subData   = internalSubset.c_str();
        size_t      subLen    = internalSubset.size();
        ParseState  sub       = state;
        sub.data   = subData;
        sub.length = subLen;
        sub.pos    = 0;
        sub.line   = 1;
        sub.col    = 1;
        ParseInternalSubset(sub);
        // Merge declared entities back
        for (const auto& [k, v] : sub.entities)
            state.entities[k] = v;
        state.inDTD = false;
    }

    // Consume closing '>'
    SkipWhitespace(state);
    if (!IsAtEnd(state) && PeekChar(state) == '>')
        ReadChar(state);

    if (state.doc)
    {
        state.doc->doctypeName          = name;
        state.doc->doctypeSystemId      = systemId;
        state.doc->doctypePublicId      = publicId;
        state.doc->doctypeInternalSubset = internalSubset;
        auto dtNode = std::make_shared<XMLNode>(XMLNodeType::DOCUMENT_TYPE, name);
        dtNode->value = systemId;
        state.doc->root->AppendChild(dtNode);
        for (const auto& [k, v] : state.entities)
            state.doc->entities[k] = v;
    }
    if (state.callbacks && state.callbacks->onDoctype)
        state.callbacks->onDoctype(name, systemId, publicId, internalSubset);

    state.sawDocType = true;
    return true;
}

bool XMLParser::ParseInternalSubset(ParseState& state)
{
    while (!IsAtEnd(state))
    {
        SkipWhitespace(state);
        if (IsAtEnd(state)) break;
        if (PeekChar(state) == '<')
        {
            ReadChar(state);
            if (IsAtEnd(state)) break;
            if (PeekChar(state) == '!')
            {
                ReadChar(state);
                if (MatchString(state, "ENTITY"))
                {
                    ExpectString(state, "ENTITY");
                    ParseEntityDecl(state);
                }
                else if (MatchString(state, "ATTLIST"))
                {
                    ExpectString(state, "ATTLIST");
                    ParseAttlistDecl(state);
                }
                else if (MatchString(state, "ELEMENT"))
                {
                    ExpectString(state, "ELEMENT");
                    ParseElementDecl(state);
                }
                else if (MatchString(state, "NOTATION"))
                {
                    ExpectString(state, "NOTATION");
                    ParseNotationDecl(state);
                }
                else if (MatchString(state, "--"))
                {
                    // Comment inside DOCTYPE
                    ExpectString(state, "--");
                    ReadUntilString(state, "-->");
                }
                else
                {
                    // Skip unknown declaration
                    ReadUntilString(state, ">");
                }
            }
            else if (PeekChar(state) == '?')
            {
                ReadChar(state);
                ReadUntilString(state, "?>");
            }
        }
        else if (PeekChar(state) == '%')
        {
            // Parameter entity reference - skip
            ReadUntilString(state, ";");
        }
        else break;
    }
    return true;
}

bool XMLParser::ParseEntityDecl(ParseState& state)
{
    SkipWhitespace(state);
    bool isParameter = false;
    if (PeekChar(state) == '%') { ReadChar(state); isParameter = true; SkipWhitespace(state); }
    std::string entityName = ReadName(state);
    SkipWhitespace(state);

    if (PeekChar(state) == '"' || PeekChar(state) == '\'')
    {
        // Internal entity value
        std::string val = ReadQuotedLiteral(state);
        if (!isParameter && !entityName.empty())
            state.entities[entityName] = val;
    }
    else if (MatchString(state, "SYSTEM") || MatchString(state, "PUBLIC"))
    {
        // External entity - skip for now (would require file loading)
        ReadUntilString(state, ">");
        return true;
    }
    SkipWhitespace(state);
    if (!IsAtEnd(state) && PeekChar(state) == '>') ReadChar(state);
    return true;
}

bool XMLParser::ParseAttlistDecl(ParseState& state)
{
    ReadUntilString(state, ">");                                        // Skip ATTLIST declarations
    return true;
}

bool XMLParser::ParseElementDecl(ParseState& state)
{
    ReadUntilString(state, ">");                                        // Skip ELEMENT content model declarations
    return true;
}

bool XMLParser::ParseNotationDecl(ParseState& state)
{
    SkipWhitespace(state);
    std::string name = ReadName(state);
    SkipWhitespace(state);
    if (MatchString(state, "SYSTEM"))
    {
        ExpectString(state, "SYSTEM");
        SkipWhitespace(state);
        std::string sysId = ReadQuotedLiteral(state);
        state.notations[name] = sysId;
    }
    else if (MatchString(state, "PUBLIC"))
    {
        ExpectString(state, "PUBLIC");
        SkipWhitespace(state);
        std::string pubId = ReadQuotedLiteral(state);
        SkipWhitespace(state);
        if (PeekChar(state) == '"' || PeekChar(state) == '\'')
        {
            std::string sysId = ReadQuotedLiteral(state);
            state.notations[name] = pubId + " " + sysId;
        }
        else state.notations[name] = pubId;
    }
    SkipWhitespace(state);
    if (!IsAtEnd(state) && PeekChar(state) == '>') ReadChar(state);
    return true;
}

bool XMLParser::ParseProlog(ParseState& state, std::shared_ptr<XMLNode> parent)
{
    // Prolog: optional XMLDecl, optional DocType, optional PIs and comments
    SkipWhitespace(state);

    // XML Declaration
    if (MatchString(state, "<?xml") && (IsWhitespace(PeekChar(state, 5)) || MatchString(state, "?>", 5)))
    {
        ReadChar(state); ReadChar(state);                               // Consume '<' '?'
        ExpectString(state, "xml");
        if (!ParseXMLDeclaration(state, parent)) return false;
        SkipWhitespace(state);
    }

    // Optional PIs and comments before DOCTYPE or root element
    while (!IsAtEnd(state))
    {
        SkipWhitespace(state);
        if (IsAtEnd(state)) break;
        if (PeekChar(state) != '<') break;

        if (MatchString(state, "<!--"))
        {
            ReadChar(state);                                            // '<'
            if (!ParseComment(state, parent)) return false;
        }
        else if (MatchString(state, "<?"))
        {
            ReadChar(state);                                            // '<'
            if (!ParseProcessingInstruction(state, parent)) return false;
        }
        else if (MatchString(state, "<!DOCTYPE"))
        {
            ReadChar(state);                                            // '<'
            ReadChar(state);                                            // '!'
            ExpectString(state, "DOCTYPE");
            if (!ParseDocType(state)) return false;
        }
        else break;                                                     // Must be the root element
    }
    return true;
}

// ==============================================================================================
// XMLParser - Attribute Parsing
// ==============================================================================================

bool XMLParser::ParseAttributes(ParseState& state, std::shared_ptr<XMLNode> node,
                                 std::unordered_map<std::string, std::string>& nsLocalMap)
{
    std::unordered_set<std::string> seenNames;

    while (!IsAtEnd(state))
    {
        SkipWhitespace(state);
        char c = PeekChar(state);
        if (c == '/' || c == '>') break;

        std::string attrName = ReadName(state);
        if (attrName.empty())
            return SetError(state, XMLParseError::MALFORMED_TAG, "Expected attribute name");

        if (seenNames.count(attrName))
            return SetError(state, XMLParseError::DUPLICATE_ATTRIBUTE, "Duplicate attribute: " + attrName);
        seenNames.insert(attrName);

        SkipWhitespace(state);
        if (!ExpectChar(state, '=', XMLParseError::INVALID_ATTRIBUTE)) return false;
        SkipWhitespace(state);

        char quote = PeekChar(state);
        if (quote != '"' && quote != '\'')
            return SetError(state, XMLParseError::INVALID_ATTRIBUTE, "Attribute value must be quoted");
        ReadChar(state);

        std::string attrValue = ReadAttributeValue(state, quote);
        if (!ExpectChar(state, quote, XMLParseError::INVALID_ATTRIBUTE)) return false;
        if (state.hasError) return false;

        // Collect namespace declarations
        if (mProcessNamespaces)
        {
            if (attrName == "xmlns")
                nsLocalMap[""] = attrValue;
            else if (attrName.size() > 6 && attrName.substr(0, 6) == "xmlns:")
                nsLocalMap[attrName.substr(6)] = attrValue;
        }

        if (node)
        {
            XMLAttribute attr(attrName, attrValue);
            SplitQName(attrName, attr.prefix, attr.localName);
            node->attributes.push_back(std::move(attr));
            // Store xmlns mappings on the node
            if (mProcessNamespaces)
            {
                if (attrName == "xmlns")
                    node->namespaceMappings[""] = attrValue;
                else if (attrName.size() > 6 && attrName.substr(0, 6) == "xmlns:")
                    node->namespaceMappings[attrName.substr(6)] = attrValue;
            }
        }
    }
    return true;
}

// ==============================================================================================
// XMLParser - Element Parsing
// ==============================================================================================

bool XMLParser::ParseElement(ParseState& state, std::shared_ptr<XMLNode> parent)
{
    if (state.depth >= mMaxDepth)
        return SetError(state, XMLParseError::MAX_DEPTH_EXCEEDED, "Maximum element depth exceeded");

    // Read element name
    std::string elemName = ReadName(state);
    if (elemName.empty())
        return SetError(state, XMLParseError::MALFORMED_TAG, "Expected element name");

    ++state.depth;
    state.elementStack.push(elemName);

    // Create DOM node
    std::shared_ptr<XMLNode> node;
    if (state.doc)
    {
        node = std::make_shared<XMLNode>(XMLNodeType::ELEMENT, elemName);
    }

    // Parse attributes and collect namespace declarations
    std::unordered_map<std::string, std::string> nsLocalMap;
    if (!ParseAttributes(state, node, nsLocalMap)) { --state.depth; return false; }

    // Push namespace scope BEFORE resolving this element's namespaces
    if (mProcessNamespaces) PushNamespaceScope(state, nsLocalMap);

    // Resolve this element's namespace after scope is established
    if (node && mProcessNamespaces) ResolveNodeNamespaces(node, state);

    // SAX: fire namespace start-prefix-mapping events
    if (state.callbacks)
    {
        for (const auto& [pfx, uri] : nsLocalMap)
            if (state.callbacks->onStartPrefixMapping)
                state.callbacks->onStartPrefixMapping(pfx, uri);
    }

    // SAX: fire start element
    std::string nsURI, localName;
    if (mProcessNamespaces)
    {
        std::string pfx;
        SplitQName(elemName, pfx, localName);
        nsURI = LookupNamespace(state, pfx);
    }
    else localName = elemName;

    if (state.callbacks && state.callbacks->onStartElement)
    {
        std::vector<XMLAttribute> saxAttrs;
        if (node) saxAttrs = node->attributes;
        state.callbacks->onStartElement(elemName, nsURI, localName, saxAttrs);
    }

    SkipWhitespace(state);

    if (PeekChar(state) == '/')
    {
        // Empty element: <name/>
        ReadChar(state);
        if (!ExpectChar(state, '>', XMLParseError::MALFORMED_TAG))
        {
            if (mProcessNamespaces) PopNamespaceScope(state);
            --state.depth; return false;
        }
    }
    else
    {
        // Content element: <name>...</name>
        if (!ExpectChar(state, '>', XMLParseError::MALFORMED_TAG))
        {
            if (mProcessNamespaces) PopNamespaceScope(state);
            --state.depth; return false;
        }

        // Parse content
        if (!ParseContent(state, node))
        {
            if (mProcessNamespaces) PopNamespaceScope(state);
            --state.depth; return false;
        }

        // End tag: </name>
        if (!ExpectString(state, "</", XMLParseError::MISMATCHED_TAG))
        {
            if (mProcessNamespaces) PopNamespaceScope(state);
            --state.depth; return false;
        }
        std::string closeName = ReadName(state);
        if (closeName != elemName)
        {
            if (mProcessNamespaces) PopNamespaceScope(state);
            --state.depth;
            return SetError(state, XMLParseError::MISMATCHED_TAG,
                "Mismatched tags: <" + elemName + "> closed by </" + closeName + ">");
        }
        SkipWhitespace(state);
        if (!ExpectChar(state, '>', XMLParseError::MISMATCHED_TAG))
        {
            if (mProcessNamespaces) PopNamespaceScope(state);
            --state.depth; return false;
        }
    }

    // SAX: fire end element
    if (state.callbacks && state.callbacks->onEndElement)
        state.callbacks->onEndElement(elemName, nsURI, localName);

    // SAX: fire namespace end-prefix-mapping events
    if (state.callbacks)
        for (const auto& [pfx, uri] : nsLocalMap)
            if (state.callbacks->onEndPrefixMapping)
                state.callbacks->onEndPrefixMapping(pfx);

    if (mProcessNamespaces) PopNamespaceScope(state);

    // Append to DOM parent
    if (node && parent) parent->AppendChild(node);

    state.elementStack.pop();
    --state.depth;
    state.sawRootElement = true;
    return true;
}

// ==============================================================================================
// XMLParser - Content Parsing (inside an element)
// ==============================================================================================

bool XMLParser::ParseContent(ParseState& state, std::shared_ptr<XMLNode> parent)
{
    while (!IsAtEnd(state))
    {
        if (MatchString(state, "</")) return true;                      // End tag found - return to caller

        if (PeekChar(state) == '<')
        {
            ReadChar(state);                                            // Consume '<'
            if (IsAtEnd(state)) return SetError(state, XMLParseError::UNEXPECTED_EOF, "Unexpected end of file");

            if (MatchString(state, "!--"))
            {
                // Comment
                ReadChar(state); ReadChar(state); ReadChar(state);     // Consume '!--'
                if (!ParseComment(state, parent)) return false;
            }
            else if (MatchString(state, "![CDATA["))
            {
                // CDATA section
                ReadChar(state);                                        // '!'
                ExpectString(state, "[CDATA[");
                if (!ParseCDATA(state, parent)) return false;
            }
            else if (PeekChar(state) == '!')
            {
                // Other markup (NOTATION etc.) - skip
                ReadChar(state);
                ReadUntilString(state, ">");
            }
            else if (PeekChar(state) == '?')
            {
                // Processing instruction
                ReadChar(state);                                        // '?'
                if (!ParseProcessingInstruction(state, parent)) return false;
            }
            else
            {
                // Child element
                if (!ParseElement(state, parent)) return false;
            }
        }
        else
        {
            // Text content
            if (!ParseText(state, parent)) return false;
        }
    }
    return true;
}

// ==============================================================================================
// XMLParser - Text Node Parsing
// ==============================================================================================

bool XMLParser::ParseText(ParseState& state, std::shared_ptr<XMLNode> parent)
{
    std::string raw;
    while (!IsAtEnd(state) && PeekChar(state) != '<')
    {
        if (PeekChar(state) == '&')
        {
            ReadChar(state);                                            // Consume '&'
            // Collect entity reference
            if (!IsAtEnd(state) && PeekChar(state) == '#')
            {
                ReadChar(state);                                        // '#'
                std::string digits;
                bool isHex = (!IsAtEnd(state) && (PeekChar(state) == 'x' || PeekChar(state) == 'X'));
                if (isHex) { ReadChar(state); }
                while (!IsAtEnd(state) && PeekChar(state) != ';')
                    digits += ReadChar(state);
                if (!IsAtEnd(state)) ReadChar(state);                  // ';'
                raw += ResolveCharacterReference(isHex ? ("x" + digits) : digits);
            }
            else
            {
                std::string entityName;
                while (!IsAtEnd(state) && PeekChar(state) != ';' && !IsWhitespace(PeekChar(state)))
                    entityName += ReadChar(state);
                if (!IsAtEnd(state) && PeekChar(state) == ';') ReadChar(state);
                raw += ResolveEntityReference(state, entityName);
                if (state.hasError) return false;
            }
        }
        else
        {
            raw += ReadChar(state);
        }
    }

    if (!raw.empty())
    {
        // Apply whitespace handling
        std::string processed = (mWhitespaceMode != XMLWhitespace::PRESERVE && !mPreserveWhitespace)
            ? ApplyWhitespace(raw, mWhitespaceMode) : raw;

        if (!processed.empty() || mPreserveWhitespace)
        {
            if (state.callbacks && state.callbacks->onCharacters)
                state.callbacks->onCharacters(processed, false);

            if (parent)
            {
                auto textNode = std::make_shared<XMLNode>(XMLNodeType::TEXT, "#text", processed);
                parent->AppendChild(textNode);
            }
        }
    }
    return true;
}

// ==============================================================================================
// XMLParser - Comment Parsing
// ==============================================================================================

bool XMLParser::ParseComment(ParseState& state, std::shared_ptr<XMLNode> parent)
{
    // We are positioned after "<!--"
    std::string text;
    while (!IsAtEnd(state))
    {
        if (MatchString(state, "--"))
        {
            ReadChar(state); ReadChar(state);                           // '--'
            if (IsAtEnd(state) || PeekChar(state) != '>')
                return SetError(state, XMLParseError::INVALID_COMMENT, "Double dash '--' inside comment");
            ReadChar(state);                                            // '>'
            break;
        }
        text += ReadChar(state);
    }

    if (state.callbacks && state.callbacks->onComment)
        state.callbacks->onComment(text);

    if (parent)
    {
        auto commentNode = std::make_shared<XMLNode>(XMLNodeType::COMMENT, "#comment", text);
        parent->AppendChild(commentNode);
    }
    return true;
}

// ==============================================================================================
// XMLParser - CDATA Section Parsing
// ==============================================================================================

bool XMLParser::ParseCDATA(ParseState& state, std::shared_ptr<XMLNode> parent)
{
    // We are positioned after "<![CDATA["
    std::string data;
    while (!IsAtEnd(state))
    {
        if (MatchString(state, "]]>"))
        {
            ReadChar(state); ReadChar(state); ReadChar(state);         // ']]>'
            break;
        }
        data += ReadChar(state);
    }

    if (state.callbacks && state.callbacks->onCharacters)
        state.callbacks->onCharacters(data, true);

    if (parent)
    {
        auto cdataNode = std::make_shared<XMLNode>(XMLNodeType::CDATA_SECTION, "#cdata-section", data);
        parent->AppendChild(cdataNode);
    }
    return true;
}

// ==============================================================================================
// XMLParser - Processing Instruction Parsing
// ==============================================================================================

bool XMLParser::ParseProcessingInstruction(ParseState& state, std::shared_ptr<XMLNode> parent)
{
    // We are positioned after the '<' and '?'
    std::string target = ReadName(state);
    if (target.empty())
        return SetError(state, XMLParseError::INVALID_PI, "Processing instruction has no target");

    std::string data;
    if (IsWhitespace(PeekChar(state)))
    {
        SkipWhitespace(state);
        while (!IsAtEnd(state))
        {
            if (MatchString(state, "?>"))
            {
                ReadChar(state); ReadChar(state);
                break;
            }
            data += ReadChar(state);
        }
    }
    else
    {
        if (!ExpectString(state, "?>", XMLParseError::INVALID_PI)) return false;
    }

    if (state.callbacks && state.callbacks->onProcessingInstruction)
        state.callbacks->onProcessingInstruction(target, data);

    if (parent)
    {
        auto piNode = std::make_shared<XMLNode>(XMLNodeType::PROCESSING_INSTRUCTION, target, data);
        parent->AppendChild(piNode);
    }
    return true;
}

// ==============================================================================================
// XMLParser - Core Parse Dispatch
// ==============================================================================================

XMLParseResult XMLParser::DoParse(const char* data, size_t length, XMLDocument* doc, const XMLSAXCallbacks* callbacks)
{
    ParseState state;
    state.data      = data;
    state.length    = length;
    state.doc       = doc;
    state.callbacks = callbacks;

    // Seed entities from parser config
    for (const auto& [k, v] : mCustomEntities)
        state.entities[k] = v;

    if (callbacks && callbacks->onStartDocument)
        callbacks->onStartDocument();

    // Parse prolog (XML decl + DOCTYPE + misc)
    auto parent = doc ? doc->root : nullptr;
    if (!ParseProlog(state, parent))
    {
        if (callbacks && callbacks->onEndDocument) callbacks->onEndDocument();
        return state.errorResult;
    }

    // Parse root element and any trailing misc content
    SkipWhitespace(state);
    while (!IsAtEnd(state))
    {
        SkipWhitespace(state);
        if (IsAtEnd(state)) break;
        if (PeekChar(state) == '<')
        {
            if (MatchString(state, "<!--"))
            {
                ReadChar(state);
                ReadChar(state); ReadChar(state); ReadChar(state);    // '!--'
                if (!ParseComment(state, parent)) break;
            }
            else if (MatchString(state, "<?"))
            {
                ReadChar(state); ReadChar(state);                      // '<' '?'
                if (!ParseProcessingInstruction(state, parent)) break;
            }
            else
            {
                ReadChar(state);                                       // Consume '<'
                if (!ParseElement(state, parent)) break;
            }
        }
        else break;
    }

    if (callbacks && callbacks->onEndDocument) callbacks->onEndDocument();

    if (state.hasError) return state.errorResult;

    XMLParseResult ok;
    ok.error = XMLParseError::NONE;
    return ok;
}

// ==============================================================================================
// XMLSerializer
// ==============================================================================================

std::string XMLSerializer::GetIndent(int depth) const
{
    if (!mOptions.pretty || depth <= 0) return "";
    return std::string(static_cast<size_t>(depth) * mOptions.indentWidth, mOptions.indentChar);
}

bool XMLSerializer::IsInlineNode(const std::shared_ptr<XMLNode>& node) const
{
    // A node is considered inline if it has only text/CDATA children (no element children)
    if (!node) return false;
    for (const auto& c : node->children)
        if (c->type == XMLNodeType::ELEMENT) return false;
    return true;
}

void XMLSerializer::WriteText(std::ostringstream& out, const std::shared_ptr<XMLNode>& node) const
{
    out << XMLParser::EncodeEntities(node->value);
}

void XMLSerializer::WriteCDATA(std::ostringstream& out, const std::shared_ptr<XMLNode>& node) const
{
    out << "<![CDATA[" << node->value << "]]>";
}

void XMLSerializer::WriteComment(std::ostringstream& out, const std::shared_ptr<XMLNode>& node, int depth) const
{
    out << GetIndent(depth) << "<!--" << node->value << "-->";
    if (mOptions.pretty) out << mOptions.lineEnding;
}

void XMLSerializer::WritePI(std::ostringstream& out, const std::shared_ptr<XMLNode>& node, int depth) const
{
    out << GetIndent(depth) << "<?" << node->name;
    if (!node->value.empty()) out << " " << node->value;
    out << "?>";
    if (mOptions.pretty) out << mOptions.lineEnding;
}

void XMLSerializer::WriteDocType(std::ostringstream& out, const std::shared_ptr<XMLNode>& node) const
{
    out << "<!DOCTYPE " << node->name;
    if (!node->value.empty()) out << " SYSTEM \"" << node->value << "\"";
    out << ">";
    if (mOptions.pretty) out << mOptions.lineEnding;
}

void XMLSerializer::WriteElement(std::ostringstream& out, const std::shared_ptr<XMLNode>& node, int depth) const
{
    out << GetIndent(depth) << "<" << node->name;

    // Write attributes
    std::vector<XMLAttribute> attrs = node->attributes;
    if (mOptions.sortAttributes)
        std::sort(attrs.begin(), attrs.end(), [](const XMLAttribute& a, const XMLAttribute& b) { return a.name < b.name; });

    for (const auto& attr : attrs)
    {
        if (mOptions.omitXmlns && (attr.name == "xmlns" || attr.name.substr(0, 6) == "xmlns:")) continue;
        out << " " << attr.name << "=\"" << XMLParser::EncodeEntities(attr.value) << "\"";
    }

    if (node->children.empty())
    {
        out << "/>";
        if (mOptions.pretty) out << mOptions.lineEnding;
        return;
    }

    out << ">";

    bool isInline = IsInlineNode(node);
    if (!isInline && mOptions.pretty) out << mOptions.lineEnding;

    // Write children
    for (const auto& child : node->children)
        WriteNode(out, child, isInline ? 0 : depth + 1);

    if (!isInline && mOptions.pretty) out << GetIndent(depth);
    out << "</" << node->name << ">";
    if (mOptions.pretty) out << mOptions.lineEnding;
}

void XMLSerializer::WriteNode(std::ostringstream& out, const std::shared_ptr<XMLNode>& node, int depth) const
{
    if (!node) return;
    switch (node->type)
    {
        case XMLNodeType::ELEMENT:
            WriteElement(out, node, depth);
            break;
        case XMLNodeType::TEXT:
            if (mOptions.pretty && depth > 0) out << GetIndent(depth);
            WriteText(out, node);
            if (mOptions.pretty && depth > 0) out << mOptions.lineEnding;
            break;
        case XMLNodeType::CDATA_SECTION:
            if (mOptions.pretty) out << GetIndent(depth);
            WriteCDATA(out, node);
            if (mOptions.pretty) out << mOptions.lineEnding;
            break;
        case XMLNodeType::COMMENT:
            WriteComment(out, node, depth);
            break;
        case XMLNodeType::PROCESSING_INSTRUCTION:
            WritePI(out, node, depth);
            break;
        case XMLNodeType::DOCUMENT_TYPE:
            WriteDocType(out, node);
            break;
        default:
            break;
    }
}

std::string XMLSerializer::Serialize(const XMLDocument& doc) const
{
    std::ostringstream out;

    // XML Declaration
    if (mOptions.writeDeclaration)
    {
        out << "<?xml version=\"" << doc.version << "\" encoding=\"" << mOptions.encoding << "\"";
        if (doc.standalone) out << " standalone=\"yes\"";
        out << "?>";
        if (mOptions.pretty) out << mOptions.lineEnding;
    }

    // All root children (DOCTYPE, PIs, comments, root element)
    for (const auto& child : doc.root->children)
        WriteNode(out, child, 0);

    return out.str();
}

std::string XMLSerializer::SerializeNode(const std::shared_ptr<XMLNode>& node, int depth) const
{
    std::ostringstream out;
    WriteNode(out, node, depth);
    return out.str();
}

bool XMLSerializer::WriteToFile(const XMLDocument& doc, const std::string& filepath) const
{
    std::string xml = Serialize(doc);
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        #ifdef _DEBUG_XMLPARSER_
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"XMLSerializer: Cannot write to file: " + WidenForXMLLog(filepath));
        #endif
        return false;
    }
    file.write(xml.c_str(), static_cast<std::streamsize>(xml.size()));
    return file.good();
}

bool XMLSerializer::WriteToFile(const XMLDocument& doc, const std::wstring& filepath) const
{
    std::string xml = Serialize(doc);
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        #ifdef _DEBUG_XMLPARSER_
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"XMLSerializer: Cannot write to wide-path file: " + filepath);
        #endif
        return false;
    }
    file.write(xml.c_str(), static_cast<std::streamsize>(xml.size()));
    return file.good();
}
