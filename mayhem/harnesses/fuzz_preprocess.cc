// TShader::preprocess (the `glslang -E` path) with an in-memory includer.
// Input: byte0 bit0 = HLSL, bits1.. = stage; then source; an optional "\n#--include--\n"
// marker splits the source from the text every #include resolves to.
#include "fuzz_common.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) { mayhem::InitOnce(); return 0; }

static const char kMarker[] = "\n#--include--\n";

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1) return -1;
    const bool hlsl = data[0] & 1;
    const EShLanguage stage = mayhem::StageFromByte(data[0] >> 1);
    std::string all(reinterpret_cast<const char*>(data + 1), size - 1);
    std::string inc;
    const size_t pos = all.find(kMarker);
    if (pos != std::string::npos) {
        inc = all.substr(pos + sizeof(kMarker) - 1);
        all.resize(pos);
    }
    const char* src = all.c_str();
    const int len = static_cast<int>(all.size());

    int m = EShMsgOnlyPreprocessor;
    if (hlsl) m |= EShMsgReadHlsl;
    const EShMessages msgs = static_cast<EShMessages>(m);

    glslang::TShader shader(stage);
    shader.setStringsWithLengths(&src, &len, 1);
    shader.setEntryPoint("main");
    if (hlsl)
        shader.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, 100);
    mayhem::SliceIncluder includer(inc);
    std::string out;
    shader.preprocess(GetDefaultResources(), 110, ENoProfile, false, false, msgs, &out, includer);
    return 0;
}
