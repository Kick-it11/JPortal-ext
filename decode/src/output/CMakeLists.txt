aux_source_directory(. output_srcs)

add_library(output_lib STATIC 
    ${output_srcs}
)