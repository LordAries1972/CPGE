# XMLParser Class - Comprehensive Usage Documentation and Examples

## Overview

This document provides comprehensive documentation and examples for the `XMLParser` class system, which provides a fully standards-compliant XML 1.0/1.1 parser with zero external dependencies (pure C++17). The system is composed of four main classes:

- **`XMLParser`** — Parses XML from files, strings, or raw bytes (DOM and SAX modes)
- **`XMLDocument`** — The root document container with factory methods, XPath query engine, and serialization
- **`XMLNode`** — DOM tree node representing elements, text, comments, CDATA, PIs, and DOCTYPE
- **`XMLSerializer`** — Configurable XML serializer (pretty-print, compact, custom options)

### Supported XML Standards

- **XML 1.0** (W3C REC-xml-20081126) — Full specification compliance
- **XML Namespaces 1.0** (xmlns prefix declarations, URI resolution, namespace-aware queries)
- **XPath 1.0** — Axes, wildcards, predicates, union operator, node-type tests
- **SAX2** — Event-driven streaming parse interface with namespace callbacks
- **DOM Level 2 Core** — createElement, appendChild, insertBefore, replaceChild, removeChild, cloneNode
- **DOCTYPE** — Internal subset parsing: `ENTITY`, `ATTLIST`, `ELEMENT`, `NOTATION` declarations
- **Entity Resolution** — Built-in (`&amp;`, `&lt;`, `&gt;`, `&apos;`, `&quot;`), character refs (`&#nnnn;`, `&#xhhhh;`), user-defined
- **CDATA Sections** — `<![CDATA[...]]>` parsed and preserved
- **Processing Instructions** — `<?target data?>` emitted via SAX and stored in DOM
- **Encoding** — UTF-8 (default), UTF-16 LE/BE (BOM detection), ISO-8859-1
- **XXE Protection** — Entity expansion limit (default: 10,000 expansions)

---

## Table of Contents

1.  [System Setup and Headers](#system-setup-and-headers)
2.  [Parsing XML from a File](#parsing-xml-from-a-file)
3.  [Parsing XML from a String](#parsing-xml-from-a-string)
4.  [DOM Tree Navigation](#dom-tree-navigation)
5.  [Attribute Access](#attribute-access)
6.  [Text Content and CDATA](#text-content-and-cdata)
7.  [Comments and Processing Instructions](#comments-and-processing-instructions)
8.  [XPath Query Engine](#xpath-query-engine)
9.  [Namespace Support](#namespace-support)
10. [Building XML Documents Programmatically](#building-xml-documents-programmatically)
11. [Serializing and Saving XML](#serializing-and-saving-xml)
12. [SAX Event-Driven Parsing](#sax-event-driven-parsing)
13. [DOCTYPE and Entity Declarations](#doctype-and-entity-declarations)
14. [Character Encoding](#character-encoding)
15. [Whitespace Handling](#whitespace-handling)
16. [Parser Configuration Options](#parser-configuration-options)
17. [Error Handling](#error-handling)
18. [XMLSerializer Options](#xmlserializer-options)
19. [Entity Encoding and Decoding Utilities](#entity-encoding-and-decoding-utilities)
20. [Advanced DOM Manipulation](#advanced-dom-manipulation)
21. [Complete Game Configuration Example](#complete-game-configuration-example)
22. [API Reference](#api-reference)

---

## System Setup and Headers

```cpp
#include "XMLParser.h"
#include "Debug.h"

// Global debug reference (required by XMLParser for error logging)
extern Debug debug;

// Global XMLParser instance (can also be created locally per operation)
XMLParser xmlParser;
```

No initialization or shutdown is required. `XMLParser` objects can be created on the stack or as class members and are reusable across multiple parse calls.

---

## Parsing XML from a File

### Basic File Parse (DOM Mode)

The most common use case — load an XML file into a DOM tree for random-access querying.

```cpp
// Example XML file: "config.xml"
// <?xml version="1.0" encoding="UTF-8"?>
// <config>
//   <game name="MyGame" version="1.0"/>
//   <graphics width="1920" height="1080" fullscreen="true"/>
// </config>

XMLParser parser;
XMLDocument doc;

XMLParseResult result = parser.ParseFile("config.xml", doc);

if (!result.success())
{
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: Failed to parse config.xml");
    // result.line, result.column, result.description have details
    return;
}

// Access the root element
auto rootElem = doc.GetDocumentElement();
if (rootElem)
{
    debug.logLevelMessage(LogLevel::LOG_INFO, L"XMLParser: Root element parsed successfully");
}
```

### Wide-String File Path (Windows Unicode Paths)

```cpp
XMLDocument doc;
XMLParseResult result = parser.ParseFile(L"C:\\Saves\\player_data.xml", doc);

if (result.success())
{
    auto root = doc.GetDocumentElement();
    // root->name == "PlayerData"
}
```

### Checking Parse Result Details

```cpp
XMLParseResult result = parser.ParseFile("data.xml", doc);

if (!result.success())
{
    // result.error    = XMLParseError enum value
    // result.line     = 1-based source line number
    // result.column   = 1-based source column number
    // result.description = human-readable message
    
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: Parse failed");
    
    if (result.error == XMLParseError::MISMATCHED_TAG)
    {
        // Handle mismatched open/close tags
    }
    else if (result.error == XMLParseError::FILE_NOT_FOUND)
    {
        // Handle missing file
    }
}
```

---

## Parsing XML from a String

Useful for XML received over the network, embedded in code, or generated at runtime.

```cpp
const std::string xmlString = R"(
<?xml version="1.0" encoding="UTF-8"?>
<enemies>
    <enemy id="1" type="grunt">
        <name>Grunt</name>
        <health>100</health>
        <speed>3.5</speed>
    </enemy>
    <enemy id="2" type="boss">
        <name>Overlord</name>
        <health>5000</health>
        <speed>1.2</speed>
    </enemy>
</enemies>
)";

XMLParser parser;
XMLDocument doc;
XMLParseResult result = parser.ParseString(xmlString, doc);

if (result.success())
{
    auto root = doc.GetDocumentElement();               // <enemies> element
    auto enemies = root->GetChildrenByName("enemy");   // All <enemy> children
    // enemies.size() == 2
}
```

### Parsing Raw Bytes

For data already in a `uint8_t` buffer (e.g., from `FileIO::StreamReadFile`).

```cpp
std::vector<uint8_t> rawData = /* loaded from FileIO or network */;

XMLParser parser;
XMLDocument doc;
XMLParseResult result = parser.ParseBytes(rawData, doc);
// ParseBytes automatically detects UTF-8, UTF-16 LE/BE via BOM
```

---

## DOM Tree Navigation

### Element Navigation

The DOM tree is composed of `XMLNode` objects connected via parent/child/sibling relationships.

```cpp
// XML:
// <library>
//   <book id="1">
//     <title>The Art of War</title>
//     <author>Sun Tzu</author>
//   </book>
//   <book id="2">
//     <title>Game Programming Gems</title>
//     <author>Mark DeLoura</author>
//   </book>
// </library>

auto root = doc.GetDocumentElement();               // <library>

// Get all direct children named "book"
auto books = root->GetChildrenByName("book");
for (const auto& book : books)
{
    std::string title  = book->GetFirstChildByName("title")->GetTextContent();
    std::string author = book->GetFirstChildByName("author")->GetTextContent();
}

// Get first child only
auto firstBook = root->GetFirstChildByName("book");

// Navigate siblings
auto secondBook = firstBook->GetNextSibling();
// Note: siblings include text-whitespace nodes if preserveWhitespace is on

// Navigate to parent
auto parent = firstBook->parent.lock();             // Returns shared_ptr to <library>
```

### Recursive Search (GetElementsByTagName)

Search the entire subtree for elements by name.

```cpp
// Find ALL <item> elements anywhere in the tree
auto allItems = doc.GetElementsByTagName("item");

// Or search from a specific subtree root
auto inventoryNode = doc.XPathQuerySingle("/game/inventory");
auto inventoryItems = inventoryNode->GetElementsByTagName("item");

// Wildcard: get all elements at any level
auto allElements = doc.GetElementsByTagName("*");
```

### Node Type Checking

Every `XMLNode` has a `type` field and convenience methods for checking it.

```cpp
for (const auto& child : root->children)
{
    if (child->IsElement())  { /* process element */ }
    if (child->IsText())     { /* text content node */ }
    if (child->IsCDATA())    { /* CDATA section node */ }
    if (child->IsComment())  { /* comment node */ }
    if (child->IsPI())       { /* processing instruction */ }
    if (child->IsDocType())  { /* DOCTYPE node */ }
}
```

### Node Position and Depth

```cpp
auto node = doc.XPathQuerySingle("//enemy[@id='1']");
if (node)
{
    int depth   = node->GetDepth();        // Distance from document root (0 = root)
    int index   = node->GetChildIndex();   // 0-based index among its siblings (-1 if root)
    int count   = node->ChildCount();      // Number of child nodes
    bool hasKids = node->HasChildren();    // True if child count > 0
}
```

---

## Attribute Access

### Reading Attributes

```cpp
// XML: <character name="Hero" level="42" class="Warrior" active="true"/>

auto charNode = doc.XPathQuerySingle("//character");

// Get attribute value (returns default if not present)
std::string name  = charNode->GetAttribute("name");                     // "Hero"
std::string level = charNode->GetAttribute("level", "1");               // "42"
std::string rank  = charNode->GetAttribute("rank", "none");             // "none" (not present)

// Check attribute existence
bool hasLevel = charNode->HasAttribute("level");                        // true
bool hasMana  = charNode->HasAttribute("mana");                         // false

// Convert to typed values
int  levelInt  = std::stoi(charNode->GetAttribute("level", "1"));
bool isActive  = charNode->GetAttribute("active") == "true";
float speed    = std::stof(charNode->GetAttribute("speed", "0.0"));
```

### Setting and Removing Attributes

```cpp
auto elem = doc.XPathQuerySingle("//character");

// Set or update an attribute
elem->SetAttribute("level", "43");
elem->SetAttribute("experience", "12500");

// Remove an attribute
elem->RemoveAttribute("active");

// Iterate all attributes
for (const auto& attr : elem->attributes)
{
    // attr.name        = qualified name (e.g., "ns:attr" or "attr")
    // attr.value       = decoded value
    // attr.prefix      = namespace prefix (e.g., "ns") 
    // attr.localName   = local part (e.g., "attr")
    // attr.namespaceURI = resolved namespace URI
}
```

---

## Text Content and CDATA

### Reading Text Content

```cpp
// XML:
// <description>A brave <em>warrior</em> who fights monsters.</description>

auto desc = doc.XPathQuerySingle("//description");

// GetTextContent: concatenates ALL descendant text nodes recursively
std::string fullText = desc->GetTextContent();
// Returns: "A brave warrior who fights monsters."

// GetDirectText: only direct TEXT children (not grandchildren)
std::string directText = desc->GetDirectText();
// Returns: "A brave " (the text before <em>)
```

### Setting Text Content

`SetTextContent` replaces all children with a single text node.

```cpp
auto titleNode = doc.XPathQuerySingle("//book/title");
titleNode->SetTextContent("The Art of Game Design");
```

### CDATA Sections

CDATA sections allow literal `<` and `&` characters without escaping.

```cpp
// XML:
// <script><![CDATA[
//   if (x < 10 && y > 5) { doSomething(); }
// ]]></script>

auto scriptNode = doc.XPathQuerySingle("//script");

for (const auto& child : scriptNode->children)
{
    if (child->IsCDATA())
    {
        std::string code = child->value;                                // Raw CDATA text (not entity-encoded)
    }
    if (child->IsText())
    {
        std::string text = child->value;                                // Entity-decoded text content
    }
}

// GetTextContent() merges TEXT and CDATA nodes:
std::string allCode = scriptNode->GetTextContent();
```

---

## Comments and Processing Instructions

### Accessing Comments

```cpp
// XML: <!-- This is a version 2.0 config file -->

for (const auto& child : doc.root->children)
{
    if (child->IsComment())
    {
        std::string commentText = child->value;                         // "This is a version 2.0 config file"
    }
}
```

### Accessing Processing Instructions

```cpp
// XML: <?xml-stylesheet type="text/xsl" href="style.xsl"?>

for (const auto& child : doc.root->children)
{
    if (child->IsPI())
    {
        std::string target = child->name;                               // "xml-stylesheet"
        std::string data   = child->value;                              // "type=\"text/xsl\" href=\"style.xsl\""
    }
}
```

---

## XPath Query Engine

The built-in XPath engine implements XPath 1.0 with support for all major axes, node-type tests, predicates, and the union (`|`) operator.

### Basic XPath Queries

```cpp
// XPathQuery returns all matching nodes
// XPathQuerySingle returns the first match (or nullptr)
// XPathQueryString returns the string value of the first match
// XPathQueryNumber returns the numeric value (as double)
// XPathQueryBool returns true if at least one node matches

auto node  = doc.XPathQuerySingle("/library/book[1]/title");
auto nodes = doc.XPathQuery("//enemy[@type='boss']");
std::string text = doc.XPathQueryString("//player/name");
double hp  = doc.XPathQueryNumber("//player/health");
bool exists = doc.XPathQueryBool("//config/graphics[@fullscreen='true']");
```

### Absolute vs Relative Paths

```cpp
// Absolute path (starts with /) - always starts from document root
auto root = doc.XPathQuerySingle("/game");
auto title = doc.XPathQuerySingle("/game/player/name");

// Relative path - evaluated against the context node
auto playerNode = doc.XPathQuerySingle("/game/player");
auto name = doc.XPathQuery("name", playerNode);                         // Children named "name"

// // = descendant-or-self (search anywhere in the tree)
auto allBosses = doc.XPathQuery("//enemy[@type='boss']");
```

### Axes

```cpp
// child:: (default axis)
auto children = doc.XPathQuery("child::book");                          // Direct children named "book"
auto all      = doc.XPathQuery("child::*");                             // All direct element children

// descendant::
auto allItems = doc.XPathQuery("descendant::item");

// parent::
auto parent = doc.XPathQuery("parent::*", someNode);

// ancestor::
auto ancestors = doc.XPathQuery("ancestor::*", deepNode);

// self::
auto self = doc.XPathQuery("self::*", node);

// attribute::
auto attrs = doc.XPathQuery("attribute::*", elem);                      // All attributes
auto idAttr = doc.XPathQuery("attribute::id", elem);                    // Attribute "id"

// following-sibling:: and preceding-sibling::
auto nextSiblings = doc.XPathQuery("following-sibling::book", node);
auto prevSiblings = doc.XPathQuery("preceding-sibling::book", node);
```

### Predicates

```cpp
// Positional predicates
auto first  = doc.XPathQuerySingle("//book[1]");                        // First book
auto last   = doc.XPathQuerySingle("//book[last()]");                   // Last book
auto third  = doc.XPathQuerySingle("//book[3]");                        // Third book

// Attribute existence predicate
auto withId  = doc.XPathQuery("//enemy[@id]");                          // Enemies with an id attribute

// Attribute value predicate
auto bosses  = doc.XPathQuery("//enemy[@type='boss']");                 // Enemies where type="boss"
auto level42 = doc.XPathQuery("//character[@level='42']");

// Text content predicate
auto artOfWar = doc.XPathQuery("//book[title='The Art of War']");       // Book with title "The Art of War"

// Child element text predicate
auto byAuthor = doc.XPathQuery("//book[author='Sun Tzu']");             // Book with author child "Sun Tzu"

// Child element existence predicate
auto hasRating = doc.XPathQuery("//book[rating]");                      // Books with a <rating> child

// Text node test
auto textNodes = doc.XPathQuery("//book/title/text()");                 // Text nodes inside <title>

// Combined predicates (nested element + attribute)
auto namedBoss = doc.XPathQuery("//enemy[@type='boss'][@id='10']");
```

### Node Type Tests

```cpp
// text() — matches TEXT and CDATA nodes
auto textNodes = doc.XPathQuery("//script/text()");

// comment() — matches comment nodes
auto comments = doc.XPathQuery("//comment()");

// processing-instruction() — matches PI nodes
auto piNodes = doc.XPathQuery("//processing-instruction()");

// node() — matches any node type
auto allNodes = doc.XPathQuery("//node()");

// Wildcard * — matches any element
auto anyChild = doc.XPathQuery("/root/*");
auto anyDesc  = doc.XPathQuery("//*");
```

### Union Operator

```cpp
// | combines multiple XPath expressions
auto titleOrAuthor = doc.XPathQuery("//title | //author");
auto firstOrLast   = doc.XPathQuery("//book[1] | //book[last()]");
```

---

## Namespace Support

### Parsing Namespace-Aware XML

```cpp
// XML:
// <catalog xmlns="http://example.com/catalog"
//          xmlns:dc="http://purl.org/dc/elements/1.1/">
//   <dc:title>Game Assets</dc:title>
//   <dc:creator>Studio Name</dc:creator>
// </catalog>

XMLParser parser;
parser.SetNamespaceProcessing(true);                                    // Enabled by default

XMLDocument doc;
parser.ParseString(xmlContent, doc);

auto root = doc.GetDocumentElement();
// root->namespaceURI == "http://example.com/catalog"
// root->localName    == "catalog"

// Get element by local name with namespace URI
auto elements = root->GetElementsByLocalName("title", "http://purl.org/dc/elements/1.1/");

// Or query using prefix (prefix must be declared in the document)
auto titleNode = doc.XPathQuerySingle("//dc:title");
// titleNode->namespaceURI == "http://purl.org/dc/elements/1.1/"
```

### Namespace-Aware Attribute Access

```cpp
// XML: <element xml:space="preserve" xml:lang="en"/>

auto elem = doc.GetDocumentElement();

// Access by qualified name (prefix:localname)
std::string space = elem->GetAttribute("xml:space");                    // "preserve"

// Access by namespace URI + local name
std::string lang = elem->GetAttributeNS("http://www.w3.org/XML/1998/namespace", "lang");

// Check namespace URI and local name
bool hasXmlSpace = elem->HasAttributeNS("http://www.w3.org/XML/1998/namespace", "space");
```

### Namespace Lookup

```cpp
auto node = doc.XPathQuerySingle("//dc:title");
if (node)
{
    // Walk ancestors to resolve a prefix
    std::string uri = node->LookupNamespaceURI("dc");   // "http://purl.org/dc/elements/1.1/"
    std::string pfx = node->LookupPrefix("http://purl.org/dc/elements/1.1/");  // "dc"

    bool isDefault = node->IsDefaultNamespace("http://example.com/catalog");
}
```

---

## Building XML Documents Programmatically

The `XMLDocument` factory methods let you build a complete XML document in memory without parsing any text.

### Creating a Document from Scratch

```cpp
XMLDocument doc;
doc.version    = "1.0";
doc.encoding   = "UTF-8";
doc.standalone = false;

// Create the root element and append to document
auto root = doc.CreateElement("GameSave");
root->SetAttribute("version", "2");
root->SetAttribute("timestamp", "2026-06-28");
doc.root->AppendChild(root);

// Add a comment before the root (directly on document node)
auto comment = doc.CreateComment(" Auto-generated save file ");
doc.root->InsertBefore(comment, root);

// Build child elements
auto playerElem = doc.CreateElement("Player");
playerElem->SetAttribute("id", "1");

auto nameElem = doc.CreateElement("Name");
nameElem->SetTextContent("HeroPlayer");
playerElem->AppendChild(nameElem);

auto levelElem = doc.CreateElement("Level");
levelElem->SetTextContent("42");
playerElem->AppendChild(levelElem);

auto healthElem = doc.CreateElement("Health");
healthElem->SetAttribute("current", "850");
healthElem->SetAttribute("max", "1000");
playerElem->AppendChild(healthElem);

root->AppendChild(playerElem);
```

### Adding CDATA Sections

Use CDATA when your text content contains literal `<` or `&` characters (e.g., scripts, HTML fragments, SQL).

```cpp
auto scriptElem = doc.CreateElement("Script");
auto cdataNode  = doc.CreateCDATASection(
    "if (player.health < 100 && !player.isDead) { respawn(player); }"
);
scriptElem->AppendChild(cdataNode);
root->AppendChild(scriptElem);
```

### Adding Processing Instructions

```cpp
// Add a stylesheet PI before the root element
auto pi = doc.CreateProcessingInstruction("xml-stylesheet",
    "type=\"text/css\" href=\"game.css\"");
doc.root->InsertBefore(pi, root);
```

### Namespace-Aware Element Creation

```cpp
// CreateElementNS: set namespace URI at creation time
auto nsCatalog = doc.CreateElementNS("http://example.com/catalog", "catalog");
auto nsTitle   = doc.CreateElementNS("http://purl.org/dc/elements/1.1/", "dc:title");
nsTitle->SetTextContent("Game Asset Catalog");
nsCatalog->AppendChild(nsTitle);

// Declare namespace on parent element
nsCatalog->SetAttribute("xmlns", "http://example.com/catalog");
nsCatalog->SetAttribute("xmlns:dc", "http://purl.org/dc/elements/1.1/");

doc.root->AppendChild(nsCatalog);
```

### Cloning Nodes

```cpp
// Clone a node (deep = true includes all descendants)
auto original = doc.XPathQuerySingle("//enemy[@id='1']");
auto clone    = original->Clone(true);

// Clone without children
auto shallowClone = original->Clone(false);

// Append the clone elsewhere
auto bossGroup = doc.XPathQuerySingle("//bossGroup");
bossGroup->AppendChild(clone);
```

### Importing Nodes from Another Document

```cpp
XMLDocument doc1, doc2;
// ... parse or build doc1 and doc2 ...

auto node = doc1.XPathQuerySingle("//enemy[@type='boss']");
auto imported = doc2.ImportNode(node, true);                            // Deep import (clone)
doc2.GetDocumentElement()->AppendChild(imported);
```

---

## Serializing and Saving XML

### Convert to String

```cpp
// Pretty-printed output (default)
std::string prettyXml = doc.ToString(true, 4);

// Compact output (no newlines or indentation)
std::string compactXml = doc.ToString(false);
```

### Save to File

```cpp
// Save with pretty-printing
bool ok = doc.SaveToFile("output.xml", true, 4);

// Save compact to wide-string path
bool ok2 = doc.SaveToFile(L"C:\\Saves\\level_data.xml", false);

if (!ok)
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: Failed to save XML file");
```

### Serializing a Single Node

```cpp
auto node = doc.XPathQuerySingle("//player");
std::string nodeXml = node->ToString(true, 0, 4);                       // pretty, 0 indent start, 4 width
```

---

## SAX Event-Driven Parsing

SAX parsing processes XML as a stream of events without building a DOM tree. This uses minimal memory and is ideal for very large files or when only certain elements need to be extracted.

### Basic SAX Parse

```cpp
XMLSAXCallbacks callbacks;

// Called at the start of every element
callbacks.onStartElement = [](const std::string& name, const std::string& nsURI,
                               const std::string& localName, const std::vector<XMLAttribute>& attrs)
{
    if (name == "enemy")
    {
        std::string id   = "";
        std::string type = "";
        for (const auto& attr : attrs)
        {
            if (attr.name == "id")   id   = attr.value;
            if (attr.name == "type") type = attr.value;
        }
        // Process enemy element
    }
};

// Called at the end of every element
callbacks.onEndElement = [](const std::string& name, const std::string& nsURI, const std::string& localName)
{
    // Matched to onStartElement
};

// Called for text content and CDATA
callbacks.onCharacters = [](const std::string& text, bool isCDATA)
{
    // isCDATA == true for <![CDATA[...]]> sections
};

// Called for comments
callbacks.onComment = [](const std::string& comment) {};

// Called for processing instructions
callbacks.onProcessingInstruction = [](const std::string& target, const std::string& data) {};

// Called for the XML declaration
callbacks.onXMLDeclaration = [](const std::string& version, const std::string& encoding, bool standalone) {};

// Called for DOCTYPE declaration
callbacks.onDoctype = [](const std::string& name, const std::string& systemId,
                          const std::string& publicId, const std::string& internalSubset) {};

// Called on any parse error
callbacks.onError = [](XMLParseError code, int line, int col, const std::string& msg)
{
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: SAX parse error");
};

// Called at start and end of the document
callbacks.onStartDocument = []() {};
callbacks.onEndDocument   = []() {};

// Namespace prefix mapping events
callbacks.onStartPrefixMapping = [](const std::string& prefix, const std::string& uri) {};
callbacks.onEndPrefixMapping   = [](const std::string& prefix) {};

// Parse the file
XMLParser parser;
XMLParseResult result = parser.ParseFileSAX("enemies.xml", callbacks);
```

### SAX State-Machine Example — Extract All Enemy Names

```cpp
std::vector<std::string> enemyNames;
std::string currentText;
bool inName = false;

XMLSAXCallbacks cb;

cb.onStartElement = [&](const std::string& name, const std::string&,
                         const std::string&, const std::vector<XMLAttribute>&)
{
    if (name == "name") { inName = true; currentText.clear(); }
};

cb.onCharacters = [&](const std::string& text, bool)
{
    if (inName) currentText += text;
};

cb.onEndElement = [&](const std::string& name, const std::string&, const std::string&)
{
    if (name == "name" && inName)
    {
        enemyNames.push_back(currentText);
        inName = false;
    }
};

XMLParser parser;
parser.ParseFileSAX("enemies.xml", cb);

// enemyNames now contains all <name> text values
```

---

## DOCTYPE and Entity Declarations

### Parsing a Document with DOCTYPE

```cpp
// XML with DOCTYPE and ENTITY declarations:
// <!DOCTYPE game [
//   <!ENTITY gameName "The Siege of Orbis">
//   <!ENTITY version  "2.1.0">
// ]>
// <game>
//   <title>&gameName;</title>
//   <version>&version;</version>
// </game>

XMLDocument doc;
XMLParser parser;
parser.ParseString(xmlWithDocType, doc);

// Entity references are automatically expanded in content:
auto title = doc.XPathQuerySingle("//title");
std::string titleText = title->GetTextContent();    // "The Siege of Orbis"

// Access declared entities
auto it = doc.entities.find("gameName");
if (it != doc.entities.end())
{
    std::string val = it->second;                   // "The Siege of Orbis"
}
```

### Registering Custom Entities Before Parsing

If you cannot embed a DOCTYPE, you can pre-register entities directly on the parser.

```cpp
XMLParser parser;
parser.AddCustomEntity("company", "Stellar Studios");
parser.AddCustomEntity("product", "CPGE Engine");

XMLDocument doc;
parser.ParseString("<info>&company; presents &product;</info>", doc);

// Text content == "Stellar Studios presents CPGE Engine"
```

### DOCTYPE Access

```cpp
auto doctypeNode = doc.GetDocType();
if (doctypeNode)
{
    std::string typeName = doctypeNode->name;       // Declared element name
    std::string systemId = doctypeNode->value;      // SYSTEM identifier (if any)

    // Full details from the document:
    // doc.doctypeName, doc.doctypeSystemId, doc.doctypePublicId, doc.doctypeInternalSubset
}
```

---

## Character Encoding

### Automatic BOM Detection

`ParseFile` and `ParseBytes` automatically detect encoding via BOM:

| BOM Bytes | Detected Encoding |
|-----------|-------------------|
| `FF FE`   | UTF-16 Little Endian |
| `FE FF`   | UTF-16 Big Endian |
| `EF BB BF`| UTF-8 (BOM stripped) |
| (none)    | UTF-8 (default) |

```cpp
// UTF-16 LE file is automatically converted to UTF-8 internally
XMLDocument doc;
parser.ParseFile("utf16le_document.xml", doc);                          // Just works
```

### Encoding Declaration

The encoding declared in the `<?xml encoding="..."?>` is stored on the document but the parser always converts to UTF-8 internally.

```cpp
XMLDocument doc;
parser.ParseFile("latin1.xml", doc);
// doc.encoding == "ISO-8859-1" (from the declaration)
// Content is decoded to UTF-8 internally
```

---

## Whitespace Handling

### Whitespace Modes

Configure how whitespace in text content is processed:

```cpp
XMLParser parser;

// PRESERVE (default): keep all whitespace exactly as-is
parser.SetWhitespaceMode(XMLWhitespace::PRESERVE);

// TRIM: strip leading and trailing whitespace from text nodes
parser.SetWhitespaceMode(XMLWhitespace::TRIM);

// NORMALIZE: collapse whitespace runs to a single space and trim
parser.SetWhitespaceMode(XMLWhitespace::NORMALIZE);
```

### Preserving Whitespace in the DOM

By default, insignificant whitespace (between elements) is preserved. To strip it:

```cpp
// Parse with whitespace trimming
XMLParser parser;
parser.SetWhitespaceMode(XMLWhitespace::TRIM);
parser.SetPreserveWhitespace(false);

XMLDocument doc;
parser.ParseString(xml, doc);
// Pure-whitespace text nodes are suppressed
```

### Applying Whitespace Externally

```cpp
// Static utility - apply whitespace mode to any string
std::string normalized = XMLParser::ApplyWhitespace("  hello   world  ", XMLWhitespace::NORMALIZE);
// == "hello world"

std::string trimmed = XMLParser::ApplyWhitespace("  hello world  ", XMLWhitespace::TRIM);
// == "hello world"
```

---

## Parser Configuration Options

All options must be set before calling any Parse method.

```cpp
XMLParser parser;

// Maximum nesting depth (default: 1000)
// Exceeding this returns XMLParseError::MAX_DEPTH_EXCEEDED
parser.SetMaxDepth(500);

// Entity expansion limit to prevent XXE attacks (default: 10000)
parser.SetEntityExpansionLimit(100);

// Enable XML namespace processing (default: true)
// When false, prefix:localname elements are treated as plain names
parser.SetNamespaceProcessing(true);

// Preserve all whitespace nodes in DOM (default: false)
parser.SetPreserveWhitespace(true);

// Enable DTD-based validation (default: false)
// Currently validates entity references; future: attribute types, content models
parser.SetValidationMode(false);

// Whitespace handling mode for text content
parser.SetWhitespaceMode(XMLWhitespace::NORMALIZE);

// Pre-register custom entity definitions
parser.AddCustomEntity("myEntity", "My Entity Value");
```

---

## Error Handling

### XMLParseError Codes

| Error Code | Meaning |
|------------|---------|
| `NONE` | Success |
| `UNEXPECTED_EOF` | Input ended prematurely |
| `INVALID_CHARACTER` | Illegal character in context |
| `MALFORMED_TAG` | Tag or attribute syntax error |
| `MISMATCHED_TAG` | Close tag doesn't match open tag |
| `INVALID_ATTRIBUTE` | Attribute syntax error |
| `DUPLICATE_ATTRIBUTE` | Same attribute appears twice on an element |
| `INVALID_ENTITY` | Unknown entity reference (validation mode only) |
| `ENTITY_EXPANSION_LIMIT` | XXE protection triggered |
| `INVALID_ENCODING` | Unsupported or mismatched encoding |
| `INVALID_DOCTYPE` | Malformed DOCTYPE |
| `INVALID_CDATA` | Malformed CDATA section |
| `INVALID_COMMENT` | Double-dash `--` inside comment body |
| `INVALID_PI` | Malformed processing instruction |
| `INVALID_XML_DECLARATION` | Malformed `<?xml?>` |
| `NAMESPACE_ERROR` | Undeclared namespace prefix used |
| `MAX_DEPTH_EXCEEDED` | Nesting too deep |
| `FILE_NOT_FOUND` | File could not be opened |
| `IO_ERROR` | File read/write failure |
| `XPATH_SYNTAX_ERROR` | Invalid XPath expression |

### Comprehensive Error Handling

```cpp
XMLParseResult result = parser.ParseFile("data.xml", doc);

if (!result.success())
{
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: Parse error occurred");

    switch (result.error)
    {
        case XMLParseError::FILE_NOT_FOUND:
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: File not found");
            // Create a default document instead
            break;

        case XMLParseError::MISMATCHED_TAG:
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: Malformed XML - tag mismatch");
            break;

        case XMLParseError::ENTITY_EXPANSION_LIMIT:
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: XXE attack detected");
            break;

        default:
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: Unknown parse error");
            break;
    }
    return;
}
```

---

## XMLSerializer Options

### Custom Serialization Options

```cpp
XMLSerializer::Options opts;
opts.pretty           = true;           // Enable pretty-printing
opts.indentWidth      = 2;              // 2 spaces per level (default: 4)
opts.indentChar       = ' ';           // Space indentation (use '\t' for tabs)
opts.writeDeclaration = true;           // Write <?xml ...?> header
opts.sortAttributes   = true;           // Sort attributes alphabetically
opts.lineEnding       = "\r\n";         // Windows line endings
opts.encoding         = "UTF-8";        // Encoding in XML declaration
opts.omitXmlns        = false;          // Keep xmlns: declarations

XMLSerializer ser(opts);
std::string xml = ser.Serialize(doc);

// Or write directly to a file
ser.WriteToFile(doc, "output.xml");
ser.WriteToFile(doc, L"C:\\Output\\data.xml");
```

### Compact Serialization

```cpp
XMLSerializer::Options compact;
compact.pretty           = false;
compact.writeDeclaration = true;

XMLSerializer ser(compact);
std::string xml = ser.Serialize(doc);                                   // One line per document
```

---

## Entity Encoding and Decoding Utilities

### EncodeEntities — Escape Special Characters for XML

Use before inserting user-provided text into XML attribute values or text content.

```cpp
std::string raw = "Scores: 10 < 20 & \"hello world\"";
std::string encoded = XMLParser::EncodeEntities(raw);
// == "Scores: 10 &lt; 20 &amp; &quot;hello world&quot;"

// Useful when setting attribute values programmatically
auto elem = doc.CreateElement("message");
// SetAttribute automatically handles this, but for manual string building:
std::string safeAttr = XMLParser::EncodeEntities(userInputText);
```

### DecodeEntities — Expand Entity References in a String

```cpp
std::string encoded = "Price: &lt;$100&gt; &amp; tax";
std::string decoded = XMLParser::DecodeEntities(encoded);
// == "Price: <$100> & tax"

// With custom entities
std::unordered_map<std::string, std::string> customs = { { "company", "Stellar Studios" } };
std::string text = XMLParser::DecodeEntities("Made by &company;", customs);
// == "Made by Stellar Studios"
```

### XML Name Validation

```cpp
// Validates that a string is a legal XML element/attribute name
bool ok1 = XMLParser::IsValidXMLName("my-element");    // true
bool ok2 = XMLParser::IsValidXMLName("123invalid");    // false (starts with digit)
bool ok3 = XMLParser::IsValidXMLName("ns:prefix");     // true (qualified name)
bool ok4 = XMLParser::IsValidXMLName("");              // false (empty)
```

---

## Advanced DOM Manipulation

### InsertBefore and InsertAfter

```cpp
auto parent = doc.XPathQuerySingle("//inventory");
auto refNode = doc.XPathQuerySingle("//inventory/sword");

// Insert before a reference node
auto shield = doc.CreateElement("shield");
shield->SetAttribute("durability", "100");
parent->InsertBefore(shield, refNode);

// Insert after a reference node
auto axe = doc.CreateElement("axe");
axe->SetAttribute("damage", "45");
parent->InsertAfter(axe, refNode);
```

### RemoveChild and ReplaceChild

```cpp
auto parent = doc.XPathQuerySingle("//weapons");
auto oldNode = doc.XPathQuerySingle("//weapons/rusty_sword");

// Remove a child
parent->RemoveChild(oldNode);

// Replace a child with a new node
auto newSword = doc.CreateElement("magic_sword");
newSword->SetAttribute("damage", "150");
parent->ReplaceChild(newSword, doc.XPathQuerySingle("//weapons/iron_sword"));

// Remove all children at once
parent->RemoveAllChildren();
```

### PrependChild

```cpp
auto parent = doc.XPathQuerySingle("//skills");
auto firstSkill = doc.CreateElement("skill");
firstSkill->SetAttribute("name", "Fireball");
parent->PrependChild(firstSkill);                                       // Inserts at front
```

### GetElementById

```cpp
// Searches for elements with id="..." or ID="..." attributes
auto elem = doc.GetElementById("player_001");
if (elem)
{
    std::string level = elem->GetAttribute("level");
}
```

---

## Complete Game Configuration Example

This example demonstrates building an entire game configuration XML document, saving it, and then loading and querying it.

### Building and Saving a Game Config

```cpp
bool SaveGameConfig(const std::string& filepath)
{
    XMLDocument doc;
    doc.version    = "1.0";
    doc.encoding   = "UTF-8";
    doc.standalone = true;

    // XML declaration will be written automatically by the serializer

    auto comment = doc.CreateComment(" CPGE Game Configuration File - Do Not Edit Manually ");
    doc.root->AppendChild(comment);

    // Root element
    auto config = doc.CreateElement("GameConfig");
    config->SetAttribute("version", "2.0");
    config->SetAttribute("engine",  "CPGE");
    doc.root->AppendChild(config);

    // Display settings
    auto display = doc.CreateElement("Display");
    display->SetAttribute("width",      "1920");
    display->SetAttribute("height",     "1080");
    display->SetAttribute("fullscreen", "true");
    display->SetAttribute("vsync",      "true");
    display->SetAttribute("renderer",   "DirectX12");
    config->AppendChild(display);

    // Audio settings
    auto audio = doc.CreateElement("Audio");
    audio->SetAttribute("masterVolume", "0.8");
    audio->SetAttribute("sfxVolume",    "0.9");
    audio->SetAttribute("musicVolume",  "0.6");
    audio->SetAttribute("enabled",      "true");
    config->AppendChild(audio);

    // Player settings
    auto player = doc.CreateElement("Player");
    player->SetAttribute("id", "player_001");

    auto nameElem = doc.CreateElement("Name");
    nameElem->SetTextContent("Hero");
    player->AppendChild(nameElem);

    auto statsElem = doc.CreateElement("Stats");
    statsElem->SetAttribute("health",    "1000");
    statsElem->SetAttribute("mana",      "500");
    statsElem->SetAttribute("strength",  "75");
    statsElem->SetAttribute("dexterity", "60");
    player->AppendChild(statsElem);

    auto inventory = doc.CreateElement("Inventory");
    auto sword = doc.CreateElement("Item");
    sword->SetAttribute("type", "weapon");
    sword->SetAttribute("name", "Excalibur");
    sword->SetAttribute("damage", "250");
    inventory->AppendChild(sword);

    auto shield = doc.CreateElement("Item");
    shield->SetAttribute("type", "armor");
    shield->SetAttribute("name", "Dragon Shield");
    shield->SetAttribute("defense", "180");
    inventory->AppendChild(shield);
    player->AppendChild(inventory);

    // Script with CDATA (no entity escaping needed)
    auto scripts = doc.CreateElement("Scripts");
    auto onStart = doc.CreateElement("Script");
    onStart->SetAttribute("event", "OnGameStart");
    auto scriptCode = doc.CreateCDATASection(
        "if (player.level < 10) { showTutorial(); }\n"
        "if (player.health < 100 && !godMode) { applyRegen(2.5f); }"
    );
    onStart->AppendChild(scriptCode);
    scripts->AppendChild(onStart);
    config->AppendChild(scripts);

    config->AppendChild(player);

    // Save with pretty-printing
    bool saved = doc.SaveToFile(filepath, true, 4);
    if (saved)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"XMLParser: Game config saved");
    else
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: Failed to save game config");

    return saved;
}
```

### Loading and Querying the Game Config

```cpp
bool LoadGameConfig(const std::string& filepath)
{
    XMLParser parser;
    XMLDocument doc;

    XMLParseResult result = parser.ParseFile(filepath, doc);
    if (!result.success())
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"XMLParser: Failed to load game config");
        return false;
    }

    // XPath queries for common config values
    int    screenW   = static_cast<int>(doc.XPathQueryNumber("/GameConfig/Display/@width"));
    int    screenH   = static_cast<int>(doc.XPathQueryNumber("/GameConfig/Display/@height"));
    bool   fullscr   = doc.XPathQueryString("/GameConfig/Display/@fullscreen") == "true";
    float  vol       = static_cast<float>(doc.XPathQueryNumber("/GameConfig/Audio/@masterVolume"));
    std::string name = doc.XPathQueryString("/GameConfig/Player/Name");

    // Get all inventory items
    auto items = doc.XPathQuery("/GameConfig/Player/Inventory/Item");
    for (const auto& item : items)
    {
        std::string type   = item->GetAttribute("type");
        std::string iname  = item->GetAttribute("name");
    }

    // Attribute-predicate query — find weapon items specifically
    auto weapons = doc.XPathQuery("//Item[@type='weapon']");

    // SAX-style streaming parse for very large config files — also supported
    // parser.ParseFileSAX(filepath, callbacks);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"XMLParser: Game config loaded successfully");
    return true;
}
```

---

## API Reference

### XMLParser

| Method | Description |
|--------|-------------|
| `ParseFile(path, doc)` | Parse XML file into DOM (std::string path) |
| `ParseFile(wpath, doc)` | Parse XML file into DOM (std::wstring path) |
| `ParseString(str, doc)` | Parse XML string into DOM |
| `ParseBytes(bytes, doc)` | Parse raw byte buffer into DOM |
| `ParseFileSAX(path, cbs)` | SAX parse from file (std::string) |
| `ParseFileSAX(wpath, cbs)` | SAX parse from file (std::wstring) |
| `ParseStringSAX(str, cbs)` | SAX parse from string |
| `ParseBytesSAX(bytes, cbs)` | SAX parse from byte buffer |
| `SetPreserveWhitespace(b)` | Preserve all whitespace nodes |
| `SetValidationMode(b)` | Enable entity/DTD validation |
| `SetNamespaceProcessing(b)` | Enable XML namespace resolution |
| `SetMaxDepth(n)` | Maximum element nesting depth |
| `SetEntityExpansionLimit(n)` | XXE expansion limit |
| `SetWhitespaceMode(m)` | PRESERVE / NORMALIZE / TRIM |
| `AddCustomEntity(n, v)` | Register a custom entity before parsing |
| `EncodeEntities(s)` _(static)_ | Escape `<`, `>`, `&`, `"`, `'` |
| `DecodeEntities(s, map)` _(static)_ | Expand entity references |
| `IsValidXMLName(s)` _(static)_ | Validate an XML name |
| `DetectEncoding(data, n, enc)` _(static)_ | Detect encoding from BOM |
| `ConvertToUTF8(data, n, enc)` _(static)_ | Convert bytes to UTF-8 |
| `NormalizeLineEndings(s)` _(static)_ | Normalize `\r\n` and `\r` to `\n` |
| `ApplyWhitespace(s, mode)` _(static)_ | Apply whitespace mode to string |

### XMLDocument

| Method | Description |
|--------|-------------|
| `GetDocumentElement()` | Get first element child of the document |
| `GetDocType()` | Get DOCTYPE node (if present) |
| `CreateElement(name)` | Create a new element node |
| `CreateElementNS(uri, name)` | Create a namespace-aware element |
| `CreateTextNode(text)` | Create a text node |
| `CreateCDATASection(data)` | Create a CDATA section node |
| `CreateComment(text)` | Create a comment node |
| `CreateProcessingInstruction(t, d)` | Create a PI node |
| `GetElementById(id)` | Find element with matching id/ID attribute |
| `GetElementsByTagName(name)` | All elements with this tag name |
| `GetElementsByLocalName(local, ns)` | Namespace-aware element search |
| `XPathQuery(expr, ctx)` | XPath query — returns all matches |
| `XPathQuerySingle(expr, ctx)` | XPath query — returns first match |
| `XPathQueryString(expr, ctx)` | XPath query — returns string value |
| `XPathQueryNumber(expr, ctx)` | XPath query — returns numeric value |
| `XPathQueryBool(expr, ctx)` | XPath query — returns true if any match |
| `ToString(pretty, indent)` | Serialize to XML string |
| `SaveToFile(path, pretty, indent)` | Save XML to file (std::string) |
| `SaveToFile(wpath, pretty, indent)` | Save XML to file (std::wstring) |
| `ImportNode(node, deep)` | Clone node from another document |

### XMLNode

| Method / Field | Description |
|----------------|-------------|
| `type` | XMLNodeType (ELEMENT, TEXT, etc.) |
| `name` | Qualified element/PI name |
| `value` | Text / comment / CDATA / PI data |
| `prefix` | Namespace prefix |
| `localName` | Local name part |
| `namespaceURI` | Resolved namespace URI |
| `attributes` | `std::vector<XMLAttribute>` |
| `children` | `std::vector<shared_ptr<XMLNode>>` |
| `parent` | `std::weak_ptr<XMLNode>` |
| `namespaceMappings` | Local xmlns declarations |
| `HasAttribute(name)` | Check attribute exists |
| `HasAttributeNS(uri, local)` | Namespace-aware attribute check |
| `GetAttribute(name, default)` | Get attribute value |
| `GetAttributeNS(uri, local, def)` | Namespace-aware attribute get |
| `SetAttribute(name, value)` | Set or add attribute |
| `SetAttributeNS(uri, qname, val)` | Namespace-aware attribute set |
| `RemoveAttribute(name)` | Remove attribute |
| `RemoveAttributeNS(uri, local)` | Namespace-aware attribute remove |
| `GetFirstChild()` | First child node |
| `GetLastChild()` | Last child node |
| `GetNextSibling()` | Next sibling node |
| `GetPreviousSibling()` | Previous sibling node |
| `GetFirstChildByName(name)` | First element child with this name |
| `GetChildrenByName(name)` | All element children with this name |
| `GetElementsByTagName(name)` | Recursive descendant search |
| `GetTextContent()` | All descendant text concatenated |
| `GetDirectText()` | Only direct text/CDATA children |
| `SetTextContent(text)` | Replace all children with text node |
| `AppendChild(child)` | Add child at end |
| `PrependChild(child)` | Add child at front |
| `InsertBefore(new, ref)` | Insert before reference node |
| `InsertAfter(new, ref)` | Insert after reference node |
| `RemoveChild(child)` | Remove child |
| `ReplaceChild(new, old)` | Replace child |
| `RemoveAllChildren()` | Remove all children |
| `Clone(deep)` | Deep or shallow clone |
| `LookupNamespaceURI(prefix)` | Resolve prefix to URI |
| `LookupPrefix(uri)` | Find prefix for URI |
| `GetDepth()` | Distance from document root |
| `GetChildIndex()` | 0-based index among siblings |
| `ToString(pretty, level, width)` | Serialize node to XML string |
| `IsElement()`, `IsText()`, etc. | Type check helpers |

### XMLSerializer::Options

| Field | Default | Description |
|-------|---------|-------------|
| `pretty` | `true` | Enable indented output |
| `indentWidth` | `4` | Spaces per indent level |
| `indentChar` | `' '` | Indent character (space or tab) |
| `writeDeclaration` | `true` | Emit `<?xml ...?>` header |
| `sortAttributes` | `false` | Sort attributes alphabetically |
| `lineEnding` | `"\n"` | Line ending sequence |
| `encoding` | `"UTF-8"` | Encoding declared in header |
| `omitXmlns` | `false` | Suppress `xmlns:` attributes |
