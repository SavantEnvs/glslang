// Multi-unit program: up to 4 compilation units -> TProgram::link (cross-stage / multi-unit
// merge) -> mapIO -> GlslangToSpv per linked stage.
// Input: byte0 = option bits; then units separated by "\n#--unit--\n", each unit starting with
// one stage-selector byte.
#include "fuzz_common.h"
#include "SPIRV/GlslangToSpv.h"

#include <memory>

extern "C" int LLVMFuzzerInitialize(int*, char***) { mayhem::InitOnce(); return 0; }

static const char kSep[] = "\n#--unit--\n";

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return -1;
    const uint8_t opt = data[0];
    const std::string all(reinterpret_cast<const char*>(data + 1), size - 1);

    std::vector<std::string> units;
    size_t start = 0;
    while (units.size() < 4) {
        const size_t pos = all.find(kSep, start);
        units.push_back(all.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + sizeof(kSep) - 1;
    }

    const bool vulkan = opt & 0x01;
    int m = vulkan ? (EShMsgSpvRules | EShMsgVulkanRules) : EShMsgDefault;
    if (opt & 0x02) m |= EShMsgRelaxedErrors;
    if (opt & 0x04) m |= EShMsgValidateCrossStageIO;
    if (opt & 0x08) m |= EShMsgLinkTimeOptimization;
    const EShMessages msgs = static_cast<EShMessages>(m);

    std::vector<std::unique_ptr<glslang::TShader>> shaders;
    std::vector<const char*> srcs;
    std::vector<int> lens;
    srcs.reserve(units.size());
    lens.reserve(units.size());
    for (const std::string& u : units) {
        if (u.empty()) return 0;
        const EShLanguage stage = mayhem::StageFromByte(static_cast<uint8_t>(u[0]));
        srcs.push_back(u.c_str() + 1);
        lens.push_back(static_cast<int>(u.size() - 1));
        auto sh = std::make_unique<glslang::TShader>(stage);
        sh->setStringsWithLengths(&srcs.back(), &lens.back(), 1);
        sh->setEntryPoint("main");
        if (vulkan) {
            sh->setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
            sh->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
            sh->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
        } else {
            sh->setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, 100);
            sh->setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
        }
        if (opt & 0x10) { sh->setAutoMapBindings(true); sh->setAutoMapLocations(true); }
        if (!sh->parse(GetDefaultResources(), 100, false, msgs)) return 0;
        shaders.push_back(std::move(sh));
    }

    {
        glslang::TProgram program;
        for (auto& sh : shaders) program.addShader(sh.get());
        if (!program.link(msgs)) return 0;
        if (!program.mapIO()) return 0;
        if (opt & 0x20) program.buildReflection();
        if (vulkan) {
            for (int s = 0; s < EShLangCount; ++s) {
                glslang::TIntermediate* im = program.getIntermediate(static_cast<EShLanguage>(s));
                if (!im) continue;
                std::vector<unsigned int> spirv;
                spv::SpvBuildLogger logger;
                glslang::GlslangToSpv(*im, spirv, &logger);
            }
        }
    }
    return 0;
}
