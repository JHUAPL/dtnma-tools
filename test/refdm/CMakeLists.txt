#[[
Copyright (c) 2011-2026 The Johns Hopkins University Applied Physics
Laboratory LLC.

This file is part of the Delay-Tolerant Networking Management
Architecture (DTNMA) Tools package.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
]]

if(BUILD_UNITTEST AND NOT BUILD_FUZZING)
    if(civetweb_FOUND)
        add_unity_test(SOURCE "test_nm_rest.c")
        target_link_libraries(test_nm_rest PUBLIC refdm)
    endif(civetweb_FOUND)

    # Test just being able to compile and link a C++ user with the refdm library
    add_executable(test_refdm_cpp)
    target_sources(test_refdm_cpp PRIVATE test_refdm_cpp.cpp)
    target_link_libraries(test_refdm_cpp PUBLIC refdm)
    add_test(NAME test_refdm_cpp COMMAND test_refdm_cpp)
    set_tests_properties(test_refdm_cpp PROPERTIES TIMEOUT 30)
endif(BUILD_UNITTEST AND NOT BUILD_FUZZING)
