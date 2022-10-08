aux_source_directory(java java_srcs)
aux_source_directory(pt pt_srcs)
aux_source_directory(java-pt java_pt_srcs)
aux_source_directory(task task_srcs)

add_library(structure_lib STATIC
    config.cpp
    ${java_srcs} 
    ${pt_srcs}
    ${java_pt_srcs}
    ${task_srcs}
)
