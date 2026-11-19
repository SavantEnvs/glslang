// Resource-limits .conf text (what `glslang <file>.conf <shader>` reads) ->
// DecodeResourceLimits, then a fixed shader parsed + linked with the decoded limits.
#include "fuzz_common.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) { mayhem::InitOnce(); return 0; }

static const char kShader[] =
    "#version 450\n"
    "layout(location = 0) in vec4 inColor;\n"
    "layout(location = 0) out vec4 outColor;\n"
    "uniform sampler2D tex[4];\n"
    "void main() {\n"
    "  vec4 c = inColor;\n"
    "  for (int i = 0; i < gl_MaxTextureImageUnits && i < 4; ++i) c += texture(tex[i], vec2(0.5));\n"
    "  outColor = c * float(gl_MaxDrawBuffers + gl_MaxClipDistances);\n"
    "}\n";

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // DecodeResourceLimits takes a NUL-terminated char* and reads only up to the first NUL
    // (it copies into a std::string), exactly as `glslang <file>.conf <shader>` passes the file.
    std::string conf(reinterpret_cast<const char*>(data), size);
    conf.resize(strlen(conf.c_str()));
    std::vector<char> buf(conf.begin(), conf.end());
    buf.push_back('\0');
    TBuiltInResource res = *GetDefaultResources();
    DecodeResourceLimits(&res, buf.data());

    const char* src = kShader;
    glslang::TShader shader(EShLangFragment);
    shader.setStrings(&src, 1);
    shader.setEntryPoint("main");
    if (!shader.parse(&res, 100, false, EShMsgDefault)) return 0;
    glslang::TProgram program;
    program.addShader(&shader);
    program.link(EShMsgDefault);
    return 0;
}
