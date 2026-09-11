#!/bin/bash
##
## Copyright (c) 2011-2026 The Johns Hopkins University Applied Physics
## Laboratory LLC.
##
## This file is part of the Delay-Tolerant Networking Management
## Architecture (DTNMA) Tools package.
##
## Licensed under the Apache License, Version 2.0 (the "License");
## you may not use this file except in compliance with the License.
## You may obtain a copy of the License at
##     http://www.apache.org/licenses/LICENSE-2.0
## Unless required by applicable law or agreed to in writing, software
## distributed under the License is distributed on an "AS IS" BASIS,
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
## See the License for the specific language governing permissions and
## limitations under the License.
##

#
# From a fresh checkout perform pre-build steps
#
set -e

SELFDIR=$(realpath $(dirname "${BASH_SOURCE[0]}"))
cd ${SELFDIR}

if [[ "$#" -ne 0 ]]
then
    ARGS="$@"
else
    ARGS=$(find src test -iname '*.h' -o -iname '*.c')
fi
clang-format --style=file -i $ARGS

if which jq >/dev/null 2>/dev/null
then
    OPENAPI="src/refdm/openapi.json"
    if jq . <${OPENAPI} >"${OPENAPI}.new"
    then
        mv "${OPENAPI}.new" "${OPENAPI}"
    else
        rm "${OPENAPI}.new"
    fi
fi

# Python test fixtures
pushd item-test/
ANYERR=0
ruff format || ANYERR=$((ANYERR + 1))
ruff check --fix || ANYERR=$((ANYERR + 1))
if which ty
then
    ty check --fix || ANYERR=$((ANYERR + 1))
fi
popd
if [[ "${ANYERR}" -ne 0 ]]
then
    exit ${ANYERR}
fi
