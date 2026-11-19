// glslang's C interface: glslang_shader_create/preprocess/parse -> glslang_program_link ->
// map_io -> glslang_program_SPIRV_generate, with include callbacks.
// Input: byte0 = stage selector, byte1 = option bits, rest = source (NUL-terminated copy).
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "glslang/Include/glslang_c_interface.h"
#include "glslang/Public/resource_limits_c.h"

static const char kHeader[] = "#define MAYHEM_INCLUDED 1\nfloat mayhem_inc(float x) { return x; }\n";

static glsl_include_result_t* inc_cb(void*, const char* name, const char*, size_t depth) {
    if (depth > 8) return nullptr;
    auto* r = static_cast<glsl_include_result_t*>(calloc(1, sizeof(glsl_include_result_t)));
    r->header_name = strdup(name);
    r->header_data = kHeader;
    r->header_length = sizeof(kHeader) - 1;
    return r;
}
static int free_cb(void*, glsl_include_result_t* r) {
    if (r) { free(const_cast<char*>(r->header_name)); free(r); }
    return 0;
}

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    glslang_initialize_process();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return -1;
    const glslang_stage_t stage = static_cast<glslang_stage_t>(data[0] % GLSLANG_STAGE_COUNT);
    const uint8_t opt = data[1];
    const std::string code(reinterpret_cast<const char*>(data + 2), size - 2);

    const bool hlsl = opt & 0x01;
    const bool vulkan = hlsl || (opt & 0x02);
    int m = GLSLANG_MSG_DEFAULT_BIT;
    if (vulkan) m |= GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT;
    if (hlsl) m |= GLSLANG_MSG_READ_HLSL_BIT;
    if (opt & 0x04) m |= GLSLANG_MSG_RELAXED_ERRORS_BIT;
    if (opt & 0x08) m |= GLSLANG_MSG_DEBUG_INFO_BIT;

    glslang_input_t input = {};
    input.language = hlsl ? GLSLANG_SOURCE_HLSL : GLSLANG_SOURCE_GLSL;
    input.stage = stage;
    input.client = vulkan ? GLSLANG_CLIENT_VULKAN : GLSLANG_CLIENT_OPENGL;
    input.client_version = vulkan ? GLSLANG_TARGET_VULKAN_1_2 : GLSLANG_TARGET_OPENGL_450;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_5;
    input.code = code.c_str();
    input.default_version = 100;
    input.default_profile = GLSLANG_NO_PROFILE;
    input.force_default_version_and_profile = 0;
    input.forward_compatible = (opt & 0x10) ? 1 : 0;
    input.messages = static_cast<glslang_messages_t>(m);
    input.resource = glslang_default_resource();
    input.callbacks.include_system = inc_cb;
    input.callbacks.include_local = inc_cb;
    input.callbacks.free_include_result = free_cb;
    input.callbacks_ctx = nullptr;

    glslang_shader_t* shader = glslang_shader_create(&input);
    if (!shader) return 0;
    glslang_shader_set_entry_point(shader, "main");
    if (opt & 0x20)
        glslang_shader_set_options(shader, GLSLANG_SHADER_AUTO_MAP_BINDINGS | GLSLANG_SHADER_AUTO_MAP_LOCATIONS);

    if (glslang_shader_preprocess(shader, &input)) {
        (void)glslang_shader_get_preprocessed_code(shader);
        if (glslang_shader_parse(shader, &input)) {
            glslang_program_t* program = glslang_program_create();
            glslang_program_add_shader(program, shader);
            if (glslang_program_link(program, m)) {
                glslang_program_map_io(program);
                if (vulkan) {
                    glslang_spv_options_t so = {};
                    so.disable_optimizer = true;
                    so.generate_debug_info = (opt & 0x08) != 0;
                    glslang_program_SPIRV_generate_with_options(program, stage, &so);
                    (void)glslang_program_SPIRV_get_size(program);
                    (void)glslang_program_SPIRV_get_messages(program);
                }
            }
            (void)glslang_program_get_info_log(program);
            glslang_program_delete(program);
        }
    }
    (void)glslang_shader_get_info_log(shader);
    glslang_shader_delete(shader);
    return 0;
}
