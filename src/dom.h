// DOM tree for the htmlview subset.
//
// Two node kinds: Element (a tag with attributes + children) and Text (a run
// of character data). Ownership is a simple unique_ptr tree; raw `parent`
// back-pointers are non-owning.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hv {

// Tags we recognise. Unknown tags parse into Tag::Unknown but keep their name
// string, so the tree round-trips even for elements we don't style specially.
enum class Tag {
    Unknown,
    Html, Head, Body, Title, Style,
    H1, H2, H3, H4, H5, H6,
    P, Div, Span, Pre, Br, Hr,
    A, Img,
    Ul, Ol, Li,
    B, I, Em, Strong,
    Table, Tr, Td, Th,
    Form, Input, Button, Label, Textarea, Select, Option,
};

// name -> Tag, case-insensitive. Returns Tag::Unknown if not recognised.
Tag tag_from_name(const std::string& name);
// Canonical lowercase name for a Tag (for dumping). Tag::Unknown -> "?".
const char* tag_name(Tag t);

// Void elements have no closing tag and take no children.
bool is_void_tag(Tag t);

enum class NodeType { Element, Text };

struct Node {
    NodeType type;
    Node* parent = nullptr;

    explicit Node(NodeType t) : type(t) {}
    virtual ~Node() = default;

    bool is_element() const { return type == NodeType::Element; }
    bool is_text() const { return type == NodeType::Text; }
};

struct Text : Node {
    std::string data;
    explicit Text(std::string d) : Node(NodeType::Text), data(std::move(d)) {}
};

struct Element : Node {
    Tag tag = Tag::Unknown;
    std::string name;  // original (lowercased) tag name, incl. for Unknown
    std::map<std::string, std::string> attrs;
    std::vector<std::unique_ptr<Node>> children;

    Element() : Node(NodeType::Element) {}

    const std::string* attr(const std::string& key) const {
        auto it = attrs.find(key);
        return it == attrs.end() ? nullptr : &it->second;
    }

    // Append a child and set its parent back-pointer; returns the raw pointer.
    Node* append(std::unique_ptr<Node> child) {
        child->parent = this;
        children.push_back(std::move(child));
        return children.back().get();
    }
};

// Print the tree as indented text for --dump-dom.
void dump_dom(const Node* root, std::string& out, int indent = 0);

}  // namespace hv
