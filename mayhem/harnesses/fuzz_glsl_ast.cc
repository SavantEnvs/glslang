// GLSL/ESSL with OpenGL (non-SPIR-V) rules -> parse -> link -> reflection -> mapIO, AST dump.
// Input: byte0 = stage selector, byte1 = message bits, byte2 = reflection bits, rest = source.
#include "fuzz_common.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) { mayhem::InitOnce(); return 0; }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 3) return -1;
    const EShLanguage stage = mayhem::StageFromByte(data[0]);
    const uint8_t mb = data[1];
    const uint8_t rb = data[2];
    const char* src = reinterpret_cast<const char*>(data + 3);
    const int len = static_cast<int>(size - 3);

    int m = EShMsgAST;
    if (mb & 0x01) m |= EShMsgRelaxedErrors;
    if (mb & 0x02) m |= EShMsgSuppressWarnings;
    if (mb & 0x04) m |= EShMsgKeepUncalled;
    if (mb & 0x08) m |= EShMsgDebugInfo;
    if (mb & 0x10) m |= EShMsgCascadingErrors;
    if (mb & 0x20) m |= EShMsgBuiltinSymbolTable;
    const EShMessages msgs = static_cast<EShMessages>(m);
    const bool forwardCompatible = (mb & 0x40) != 0;

    glslang::TShader shader(stage);
    shader.setStringsWithLengths(&src, &len, 1);
    shader.setEntryPoint("main");
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, 100);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    if (mb & 0x80) { shader.setAutoMapBindings(true); shader.setAutoMapLocations(true); }

    const bool ok = shader.parse(GetDefaultResources(), 100, forwardCompatible, msgs);
    (void)shader.getInfoLog();
    (void)shader.getInfoDebugLog();  // the AST dump (intermOut) lands here
    if (!ok) return 0;

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(msgs)) return 0;
    (void)program.getInfoDebugLog();
    if (program.buildReflection(rb)) {
        (void)program.getNumLiveUniformVariables();
        (void)program.getNumLiveUniformBlocks();
        (void)program.getNumPipeInputs();
        (void)program.getNumPipeOutputs();
    }
    program.mapIO();
    return 0;
}
