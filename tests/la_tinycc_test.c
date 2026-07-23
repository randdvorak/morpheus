#include <stdio.h>

#include "libtcc.h"

int main(void)
{
    static const char source[] =
        "#define LA_IMPLEMENTATION\n"
        "#include \"la.h\"\n"
        "double morph_la_probe(void) {\n"
        "    V3d value = v3d(1.0, 2.0, 3.0);\n"
        "    V3d other = v3d(4.0, 5.0, 6.0);\n"
        "    M3d identity = m3d_id();\n"
        "    return v3d_dot(m3d_mul_vec(identity, value), other);\n"
        "}\n";
    TCCState *compiler = tcc_new();
    int result;

    if (!compiler) return 1;
    tcc_set_lib_path(compiler, MORPHEUS_TCC_LIB_PATH);
    tcc_set_options(compiler, "-nostdlib -Wall -Werror");
    if (tcc_add_include_path(compiler, MORPHEUS_LA_INCLUDE_PATH) < 0 ||
        tcc_add_include_path(compiler, MORPHEUS_TCC_INCLUDE_PATH) < 0 ||
        tcc_set_output_type(compiler, TCC_OUTPUT_MEMORY) < 0) {
        tcc_delete(compiler);
        return 2;
    }
    result = tcc_compile_string(compiler, source);
    tcc_delete(compiler);
    if (result < 0) {
        fprintf(stderr, "TinyCC rejected the la header\n");
        return 3;
    }
    puts("PASS: TinyCC compiles the la fixed-size math header");
    return 0;
}
