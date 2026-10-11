#pragma once

#include <cstddef>
#include <cstdint>

// Minified `git/trees/main?recursive=1` body of a real repository. Every SHA is the real git object id:
// each blob's is `git hash-object` of its content below.
constexpr const char* TREE_FIXTURE_SHA_HEX = "859af781495f25be7e50e1e4c78274d53de4c5af";

constexpr const char* TREE_FIXTURE_BODY =
    R"({"sha":"859af781495f25be7e50e1e4c78274d53de4c5af",)"
    R"("url":"https://api.github.com/repos/owner/books/git/trees/859af781495f25be7e50e1e4c78274d53de4c5af",)"
    R"("tree":[)"
    R"({"path":".atelier","mode":"040000","type":"tree","sha":"d184003c45e7e16dffd8be2c94ba48f842a945d8",)"
    R"("url":"https://api.github.com/repos/owner/books/git/trees/d184003c45e7e16dffd8be2c94ba48f842a945d8"},)"
    R"({"path":".atelier/notes.md","mode":"100644","type":"blob","sha":"bfa655111293037a5564088d1a9bbca4cbcf446b",)"
    R"("size":6,"url":"https://api.github.com/repos/owner/books/git/blobs/bfa655111293037a5564088d1a9bbca4cbcf446b"},)"
    R"({"path":"Livres","mode":"040000","type":"tree","sha":"337bdfc470ad8f52f08cf8b158acc1d6faf18bc4",)"
    R"("url":"https://api.github.com/repos/owner/books/git/trees/337bdfc470ad8f52f08cf8b158acc1d6faf18bc4"},)"
    R"({"path":"Livres/Mon livre.epub","mode":"100644","type":"blob","sha":"1dd50ed34ab30102fb68d6547edb8e25dd7f52af",)"
    R"("size":10,"url":"https://api.github.com/repos/owner/books/git/blobs/1dd50ed34ab30102fb68d6547edb8e25dd7f52af"},)"
    R"({"path":"Livres/SF","mode":"040000","type":"tree","sha":"e1e82ef2acc0f24a8927fcc9d83137de18359732",)"
    R"("url":"https://api.github.com/repos/owner/books/git/trees/e1e82ef2acc0f24a8927fcc9d83137de18359732"},)"
    R"({"path":"Livres/SF/Dune.epub","mode":"100644","type":"blob","sha":"7847c49a26553609b8fe1881d5fb40c1a818eb40",)"
    R"("size":5,"url":"https://api.github.com/repos/owner/books/git/blobs/7847c49a26553609b8fe1881d5fb40c1a818eb40"},)"
    R"({"path":"Livres/vide.txt","mode":"100644","type":"blob","sha":"e69de29bb2d1d6434b8b29ae775ad8c2e48c5391",)"
    R"("size":0,"url":"https://api.github.com/repos/owner/books/git/blobs/e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"},)"
    "{\"path\":\"Livres/\xC3\x89t\xC3\xA9.epub\","
    R"("mode":"100644","type":"blob","sha":"8b2693aabb76d7f5aef04dbc7332c6fdbd3cd0a4",)"
    R"("size":6,"url":"https://api.github.com/repos/owner/books/git/blobs/8b2693aabb76d7f5aef04dbc7332c6fdbd3cd0a4"},)"
    R"({"path":"README.md","mode":"100644","type":"blob","sha":"4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa73",)"
    R"("size":8,"url":"https://api.github.com/repos/owner/books/git/blobs/4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa73"},)"
    R"({"path":"vendor","mode":"160000","type":"commit","sha":"3b18e512dba79e4c8300dd08aeb37f8e728b8dad"})"
    R"(],"truncated":false})";

struct TreeFixtureBlob {
  const char* path;
  const char* content;
  const char* shaHex;
  uint64_t size;
};

// The records the fixture yields, in body order.
constexpr TreeFixtureBlob TREE_FIXTURE_BLOBS[] = {
    {".atelier/notes.md", "notes\n", "bfa655111293037a5564088d1a9bbca4cbcf446b", 6},
    {"Livres/Mon livre.epub", "Mon livre\n", "1dd50ed34ab30102fb68d6547edb8e25dd7f52af", 10},
    {"Livres/SF/Dune.epub", "Dune\n", "7847c49a26553609b8fe1881d5fb40c1a818eb40", 5},
    {"Livres/vide.txt", "", "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391", 0},
    {"Livres/\xC3\x89t\xC3\xA9.epub", "\xC3\x89t\xC3\xA9\n", "8b2693aabb76d7f5aef04dbc7332c6fdbd3cd0a4", 6},
    {"README.md", "# Books\n", "4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa73", 8},
};
