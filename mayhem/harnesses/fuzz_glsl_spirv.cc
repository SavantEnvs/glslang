// GLSL (Vulkan rules) -> TShader::parse -> TProgram::link -> GlslangToSpv -> spv::Disassemble.
// Input: byte0 = stage selector, byte1 = option bits, rest = GLSL source.
#include <sstream>

#include "fuzz_common.h"
#include "SPIRV/GlslangToSpv.h"
#include "SPIRV/disassemble.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) { mayhem::InitOnce(); return 0; }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return -1;
    const EShLanguage stage = mayhem::StageFromByte(data[0]);
    const uint8_t opt = data[1];
    const char* src = reinterpret_cast<const char*>(data + 2);
    const int len = static_cast<int>(size - 2);

    EShMessages msgs = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (opt & 0x01) msgs = static_cast<EShMessages>(msgs | EShMsgRelaxedErrors);
    if (opt & 0x02) msgs = static_cast<EShMessages>(msgs | EShMsgDebugInfo);
    if (opt & 0x04) msgs = static_cast<EShMessages>(msgs | EShMsgKeepUncalled);
    const bool forwardCompatible = (opt & 0x08) != 0;

    glslang::TShader shader(stage);
    shader.setStringsWithLengths(&src, &len, 1);
    shader.setEntryPoint("main");
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
    if (opt & 0x10) shader.setAutoMapBindings(true);
    if (opt & 0x20) shader.setAutoMapLocations(true);

    if (!shader.parse(GetDefaultResources(), 100, forwardCompatible, msgs)) return 0;

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(msgs)) return 0;
    if (opt & 0x40) program.mapIO();

    glslang::TIntermediate* im = program.getIntermediate(stage);
    if (!im) return 0;
    std::vector<unsigned int> spirv;
    spv::SpvBuildLogger logger;
    glslang::SpvOptions spvOpts;
    spvOpts.generateDebugInfo = (opt & 0x02) != 0;
    spvOpts.emitNonSemanticShaderDebugInfo = (opt & 0x80) != 0 && (opt & 0x02) != 0;
    glslang::GlslangToSpv(*im, spirv, &logger, &spvOpts);

    std::ostringstream dis;
    spv::Disassemble(dis, spirv);
    return 0;
}
