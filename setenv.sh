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

SELFDIR=$(realpath $(dirname "${BASH_SOURCE[0]}"))

export DESTDIR=${DESTDIR:-${SELFDIR}/testroot}
export PREFIX=${PREFIX:-/usr}

if [[ -n "${DESTDIR}" || -n "${PREFIX}" ]]
then
    if which dpkg-architecture >/dev/null 2>/dev/null
    then
        # Debian or Ubuntu
        LD_LIBRARY_PATH=${DESTDIR}${PREFIX}/lib:${DESTDIR}${PREFIX}/lib/$(dpkg-architecture -q DEB_BUILD_MULTIARCH)
    else
        # Fedora or RHEL
        LD_LIBRARY_PATH=${DESTDIR}${PREFIX}/lib:${DESTDIR}${PREFIX}/lib64
    fi
    export LD_LIBRARY_PATH
    export PATH=${DESTDIR}${PREFIX}/bin:${PATH}
    export XDG_DATA_DIRS=${DESTDIR}${PREFIX}/share:${XDG_DATA_DIRS}
fi
