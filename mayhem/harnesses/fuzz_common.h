// Shared helpers for the glslang Mayhem harnesses (public API only).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "glslang/Public/ShaderLang.h"
#include "glslang/Public/ResourceLimits.h"

namespace mayhem {

// One stage per value; the selector byte is taken modulo the stage count.
inline EShLanguage StageFromByte(uint8_t b) {
    return static_cast<EShLanguage>(b % EShLangCount);
}

// glslang::InitializeProcess() builds per-process tables once; it is reference-counted and
// is called once from LLVMFuzzerInitialize so every input sees the same, read-only state.
inline void InitOnce() {
    static bool done = glslang::InitializeProcess();
    (void)done;
}

// In-memory includer: every #include resolves to the same caller-provided text (a slice of the
// fuzz input), so the include machinery is reached without touching the filesystem.
class SliceIncluder : public glslang::TShader::Includer {
public:
    explicit SliceIncluder(std::string text) : text_(std::move(text)) {}
    IncludeResult* includeSystem(const char* name, const char*, size_t depth) override {
        return make(name, depth);
    }
    IncludeResult* includeLocal(const char* name, const char*, size_t depth) override {
        return make(name, depth);
    }
    void releaseInclude(IncludeResult* r) override { delete r; }

private:
    IncludeResult* make(const char* name, size_t depth) {
        if (depth > 8)  // glslang's own DirStackFileIncluder has no limit; keep recursion bounded
            return nullptr;
        return new IncludeResult(std::string(name), text_.data(), text_.size(), nullptr);
    }
    std::string text_;
};

}  // namespace mayhem
