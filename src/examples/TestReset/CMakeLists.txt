# Make sure the compiler can find include files from our Apex library. 
include_directories (${APEX_SOURCE_DIR}/src/apex) 

# Make sure the linker can find the Apex library once it is built. 
link_directories (${APEX_BINARY_DIR}/src/apex) 

# Add executable called "testReset" that is built from the source file
# "reset.cpp". The extensions are automatically found. 
add_executable (testReset reset.cpp) 

# Link the executable to the Apex library. 
target_link_libraries (testReset apex ${LIBS})
if (BUILD_STATIC_EXECUTABLES)
    set_target_properties(testReset PROPERTIES LINK_SEARCH_START_STATIC 1 LINK_SEARCH_END_STATIC 1)
endif()
add_dependencies (testReset apex)
add_dependencies (examples testReset)

INSTALL(TARGETS testReset
  RUNTIME DESTINATION bin OPTIONAL
)
