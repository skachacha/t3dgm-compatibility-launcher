#pragma once
#include "shared.hpp"
#include <vector>

struct EditorPatchEdit {
    std::uint32_t word;
    std::vector<std::uint32_t> expected, replacement;
};
struct EditorPatchImage {
    std::vector<std::uint32_t> words;
    std::vector<EditorPatchEdit> edits;
    std::uint32_t canonical_entry, resource_entry;
};

std::vector<std::uint32_t> relocate_editor_code(const EditorProgram& program,
    std::uint32_t code, std::uint32_t arena);
EditorPatchImage build_editor_patch(const EditorProgram& program,
    std::uint32_t code, std::uint32_t arena, std::uint32_t extension);
std::wstring apply_editor_patch(HANDLE process, const EditorProgram& program);
