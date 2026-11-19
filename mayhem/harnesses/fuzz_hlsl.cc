// HLSL -> TShader::parse (EShSourceHlsl) -> TProgram::link -> GlslangToSpv.
// Input: byte0 = stage selector, byte1 = option bits, rest = HLSL source (entry point "main").
#include "fuzz_common.h"
#include "SPIRV/GlslangToSpv.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) { mayhem::InitOnce(); return 0; }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return -1;
    const EShLanguage stage = mayhem::StageFromByte(data[0]);
    const uint8_t opt = data[1];
    const char* src = reinterpret_cast<const char*>(data + 2);
    const int len = static_cast<int>(size - 2);

    int m = EShMsgReadHlsl | EShMsgSpvRules | EShMsgVulkanRules;
    if (opt & 0x01) m |= EShMsgHlslOffsets;
    if (opt & 0x02) m |= EShMsgHlslEnable16BitTypes;
    if (opt & 0x04) m |= EShMsgHlslLegalization;
    if (opt & 0x08) m |= EShMsgHlslDX9Compatible;
    if (opt & 0x10) m |= EShMsgDebugInfo;
    if (opt & 0x20) m |= EShMsgRelaxedErrors;
    const EShMessages msgs = static_cast<EShMessages>(m);

    glslang::TShader shader(stage);
    shader.setStringsWithLengths(&src, &len, 1);
    shader.setEntryPoint("main");
    shader.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    if (opt & 0x40) shader.setHlslIoMapping(true);
    if (opt & 0x80) { shader.setAutoMapBindings(true); shader.setAutoMapLocations(true); }

    if (!shader.parse(GetDefaultResources(), 100, false, msgs)) return 0;

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(msgs)) return 0;
    program.mapIO();

    glslang::TIntermediate* im = program.getIntermediate(stage);
    if (!im) return 0;
    std::vector<unsigned int> spirv;
    spv::SpvBuildLogger logger;
    glslang::SpvOptions spvOpts;
    spvOpts.generateDebugInfo = (opt & 0x10) != 0;
    glslang::GlslangToSpv(*im, spirv, &logger, &spvOpts);
    return 0;
}
