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

# NM Manager library without concrete messaging interface
set(CIVETWEB_FOUND ${civetweb_FOUND})
configure_file("config.h.in" "config.h")
set(HFILES
  ${CMAKE_CURRENT_BINARY_DIR}/config.h
  agents.h
  ingress.h
  instr.h
  mgr.h
)
set(CFILES
  agents.c
  ingress.c
  instr.c
  mgr.c
)
if(REFDM_UI_CLI)
  list(APPEND HFILES
    nm_mgr_print.h
    nm_mgr_ui.h
    ui_input.h
  )
  list(APPEND CFILES
    nm_mgr_print.c
    nm_mgr_ui.c
    ui_input.c
  )
endif(REFDM_UI_CLI)
if(PostgreSQL_FOUND)
  list(APPEND HFILES nm_sql.h)
  list(APPEND CFILES nm_sql.c)
endif(PostgreSQL_FOUND)
if(civetweb_FOUND)
  list(APPEND HFILES nm_rest.h)
  list(APPEND CFILES nm_rest.c)
endif(civetweb_FOUND)


add_library(refdm)

target_sources(refdm PRIVATE ${HFILES})
target_sources(refdm PRIVATE ${CFILES})
target_include_directories(
  refdm PUBLIC
  $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>
)
target_link_libraries(refdm PUBLIC cace)

# Prefer Postgres when both are available
if(PostgreSQL_FOUND)
  message(STATUS "Manager using POSTGRES client")
  target_link_libraries(refdm PUBLIC PostgreSQL::PostgreSQL)
else()
  message(WARNING "Manager not using any database client")
endif()

if(cJSON_FOUND)
  message(STATUS "Manager using cJSON")
  target_link_libraries(refdm PUBLIC cjson)
endif(cJSON_FOUND)

if(civetweb_FOUND)
  message(STATUS "Manager using civetweb")
  target_link_libraries(refdm PUBLIC civetweb)
endif(civetweb_FOUND)


install(
    TARGETS refdm
    RUNTIME
        COMPONENT runtime
    LIBRARY
        COMPONENT runtime
        NAMELINK_COMPONENT devel
    ARCHIVE
        COMPONENT devel
)
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILE_PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
    COMPONENT devel
    FILES_MATCHING PATTERN "*.h"
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/config.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/refdm"
    PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
    COMPONENT devel
)

install(
    FILES openapi.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/refdm/nm/api/
    COMPONENT runtime
)
