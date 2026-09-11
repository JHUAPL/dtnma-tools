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
  add_unity_test(SOURCE "test_amm_const.c")
  target_link_libraries(test_amm_const PUBLIC refda test_util)
  
  add_unity_test(SOURCE "test_amm_ctrl.c")
  target_link_libraries(test_amm_ctrl PUBLIC refda test_util)
  
  add_unity_test(SOURCE "test_exec.c")
  target_link_libraries(test_exec PUBLIC refda test_util)
  
  add_unity_test(SOURCE "test_eval.c")
  target_link_libraries(test_eval PUBLIC refda test_util)
  
  add_unity_test(SOURCE "test_reporting.c")
  target_link_libraries(test_reporting PUBLIC refda test_util)
  
  add_unity_test(SOURCE "test_alarms.c")
  target_link_libraries(test_alarms PUBLIC refda test_util)
  
  add_unity_test(SOURCE "test_adm_ietf_dtnma_agent.c")
  target_link_libraries(test_adm_ietf_dtnma_agent PUBLIC refda test_util)
  
  add_unity_test(SOURCE "test_adm_ietf_alarms.c")
  target_link_libraries(test_adm_ietf_alarms PUBLIC refda test_util)
  
  # Test just being able to compile and link a C++ user with the refda library
  add_executable(test_refda_cpp)
  target_sources(test_refda_cpp PRIVATE test_refda_cpp.cpp)
  target_link_libraries(test_refda_cpp PUBLIC refda test_util)
  add_test(NAME test_refda_cpp COMMAND test_refda_cpp)
  set_tests_properties(test_refda_cpp PROPERTIES TIMEOUT 30)
endif(BUILD_UNITTEST AND NOT BUILD_FUZZING)
