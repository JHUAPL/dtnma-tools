#
# Copyright (c) 2011-2026 The Johns Hopkins University Applied Physics
# Laboratory LLC.
#
# This file is part of the Delay-Tolerant Networking Management
# Architecture (DTNMA) Tools package.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
from typing import Any

import schemathesis
from cbor_diag import cbor2diag, diag2cbor


@schemathesis.serializer("text/uri-list")
def urilist_ser(ctx: schemathesis.SerializationContext, value: Any) -> bytes:
    if isinstance(value, bytes):
        return value

    return str(value).encode()


@schemathesis.deserializer("text/uri-list")
def urilist_des(
    ctx: schemathesis.DeserializationContext, resp: schemathesis.Response
) -> list[str]:
    text = resp.content.decode()
    return text.split("\r\n")


@schemathesis.serializer("application/cbor")
def cbor_ser(ctx: schemathesis.SerializationContext, value: Any) -> bytes:
    if isinstance(value, bytes):
        return value

    try:
        return diag2cbor(str(value))
    except ValueError:
        return b''


@schemathesis.deserializer("application/cbor")
def cbor_des(
    ctx: schemathesis.DeserializationContext, resp: schemathesis.Response
) -> list[str]:
    text = cbor2diag(resp.content)
    return text
