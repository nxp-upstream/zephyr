#
# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#

# Compile an eRPC interface file with the erpcgen host tool.
#
# mpipe_erpc_generate(<target> <idl> <out_dir> <sources_var> [C_SERVER])
#
# <target>      Target the generated sources are compiled in
# <idl>         Interface file, named after its program (<program>.erpc)
# <out_dir>     Directory the code is generated in, added to the include path
#               of the target
# <sources_var> Set to the generated C++ sources: the client, the server and
#               the interface classes
# C_SERVER      Add the C wrapper of the server to the sources, for a server
#               written in C that serves one client
#
# The other C wrappers erpcgen generates are not listed: they hold one object
# per interface.
function(mpipe_erpc_generate target idl out_dir sources_var)
  find_program(ERPCGEN erpcgen REQUIRED)
  get_filename_component(program ${idl} NAME_WE)
  set(sources
    ${out_dir}/${program}_client.cpp
    ${out_dir}/${program}_server.cpp
    ${out_dir}/${program}_interface.cpp
  )
  if("C_SERVER" IN_LIST ARGN)
    list(APPEND sources ${out_dir}/c_${program}_server.cpp)
  endif()
  add_custom_command(
    OUTPUT
      ${sources}
      ${out_dir}/${program}_client.hpp
      ${out_dir}/${program}_server.hpp
      ${out_dir}/${program}_interface.hpp
      ${out_dir}/${program}_common.h
      ${out_dir}/c_${program}_server.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
    COMMAND ${ERPCGEN} -g c -o ${out_dir} ${idl}
    DEPENDS ${idl}
    COMMENT "Generating the eRPC code of ${program}"
  )
  add_custom_target(${program}_erpc_gen DEPENDS ${sources})
  add_dependencies(${target} ${program}_erpc_gen)
  target_include_directories(${target} PRIVATE ${out_dir})
  set(${sources_var} ${sources} PARENT_SCOPE)
endfunction()
