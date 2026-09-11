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
set -e

SELFDIR=$(realpath $(dirname "${BASH_SOURCE[0]}"))
cd "${SELFDIR}"

DOCKER=${DOCKER:-docker}

CEXEC="${DOCKER} compose exec -T client"

if [ "$1" = "start" ]
then
    export DOCKER_BUILDKIT=1
    
    ${DOCKER} compose build
    ${DOCKER} compose create --force-recreate --remove-orphans
    ${DOCKER} compose up --detach --wait
elif [ "$1" = "stop" ]
then
    ${DOCKER} compose down --rmi local --volumes
    ${DOCKER} compose rm --force --volumes
elif [ "$1" = "logs" ]
then
    ${DOCKER} compose logs amp-manager amp-agent1
elif [ "$1" = "check" ]
then
    ${DOCKER} compose ps

    CURLOPTS="-svf --variable '%REFDA_EID'"
    # All manager actions operate with this base
    URIBASE="http://amp-manager:8089/nm/api"

    # Probe HTTP API
    for IX in $(seq 10)
    do
        sleep 1

        CMD="curl ${CURLOPTS} -XOPTIONS ${URIBASE}/version"
        FAILURE=0
        echo $CMD | ${CEXEC} bash || FAILURE=$?
        if [[ ${FAILURE} -eq 0 ]]
        then
            echo "HTTP service available"
            echo
            break
        fi
    done
    if [[ ${FAILURE} -ne 0 ]]
    then
        echo "HTTP service did not all start"
        exit 2
    fi

    CMD="curl ${CURLOPTS} -XPOST ${URIBASE}/agents -H 'Content-Type: text/plain' --expand-data '{{REFDA_EID}}'"
    echo $CMD | ${CEXEC} bash
    echo

    CMD="curl ${CURLOPTS} -XPOST --expand-url ${URIBASE}/agents/eid/{{REFDA_EID:trim:url}}/clear_reports"
    echo $CMD | ${CEXEC} bash
    echo

    # Verify empty listing
    CMD="curl ${CURLOPTS} -XGET --expand-url ${URIBASE}/agents/eid/{{REFDA_EID:trim:url}}/reports?form=uri"
    RPTLINES=$(echo $CMD | ${CEXEC} bash)
    if [ -n "${RPTLINES}" ]
    then
        exit 4
    fi

    # send an inspect execution with a nonce, expecting a report back
    CMD="echo 'ari:/EXECSET/n=12345;(//ietf/dtnma-agent/CTRL/report-on(/ac/(//ietf/dtnma-agent/EDD/sw-version)))' | \
        ace_ari --log-level=error --inform uri --outform cbor --must-nickname | \
        curl ${CURLOPTS} -XPOST --expand-url ${URIBASE}/agents/eid/{{REFDA_EID:trim:url}}/send?form=cbor -H 'Content-Type: application/cbor-seq' --data-binary @-; echo"
    echo $CMD | ${CEXEC} bash
    echo

    RPTOBJ=""
    for IX in $(seq 10)
    do
        echo "Waiting on reports back..."
        sleep 1

        CMD="curl ${CURLOPTS} -XGET --expand-url ${URIBASE}/agents/eid/{{REFDA_EID:trim:url}}/reports?form=uri"
        RPTLINES=$(echo $CMD | ${CEXEC} bash)
        LINECOUNT=$(echo "${RPTLINES}" | wc -l)
        echo "Got ${LINECOUNT} lines"
        if [ ${LINECOUNT} -ge 2 ]
        then
            break
        fi
    done

    if [ -n "${RPTLINES}" ]
    then
      echo "Got Report lines:"
      echo "${RPTLINES}"
      echo
    fi
    if [ ${LINECOUNT} -lt 2 ]
    then
      exit 3
    fi

    # view the hex-binary version also
    CMD="curl ${CURLOPTS} -XGET --expand-url ${URIBASE}/agents/eid/{{REFDA_EID:trim:url}}/reports?form=cbor | \
        ace_ari --log-level=error --inform cbor --outform uri"
    echo $CMD | ${CEXEC} bash

    # introspective API check
    ${CEXEC} schemathesis --config-file schemathesis.toml \
        run -w auto ${URIBASE}/openapi.json
fi
