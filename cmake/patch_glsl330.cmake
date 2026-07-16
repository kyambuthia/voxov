if(NOT DEFINED INPUT)
    message(FATAL_ERROR "patch_glsl330.cmake requires INPUT")
endif()
string(REGEX REPLACE "^\\\"(.*)\\\"$" "\\1" INPUT "${INPUT}")

file(READ "${INPUT}" shader_header)
# sokol-shdc emits GLSL 4.10 for desktop GL, while VOXOV's GLCORE context
# intentionally targets the widely available GLSL 3.30 feature set. The
# generated source uses only 3.30-compatible constructs; update the embedded
# version bytes without touching HLSL, Metal, or GLES outputs.
string(REPLACE "0x34,0x31,0x30" "0x33,0x33,0x30" shader_header "${shader_header}")
# Join wrapped byte-array lines so layout prefixes can be removed without
# depending on sokol-shdc's line-wrap position.
string(REPLACE "\n    0x" "0x" shader_header "${shader_header}")
set(layout_start
    "0x6c,0x61,0x79,0x6f,0x75,0x74,0x28,"
)
set(layout_body
    "0x6c,0x6f,0x63,0x61,0x74,0x69,0x6f,0x6e,0x20,0x3d,0x20,"
)
foreach(slot IN ITEMS 0 1 2 3)
    string(REGEX REPLACE
        "${layout_start}${layout_body}0x3${slot},0x29,0x20,"
        ""
        shader_header
        "${shader_header}"
    )
endforeach()
file(WRITE "${INPUT}" "${shader_header}")
