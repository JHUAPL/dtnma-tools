/*
 * Copyright (c) 2011-2026 The Johns Hopkins University Applied Physics
 * Laboratory LLC.
 *
 * This file is part of the Delay-Tolerant Networking Management
 * Architecture (DTNMA) Tools package.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/** @file
 * Test the internal API for alarm bookkeeping, separate from the ADM
 * exposing some of these data and behaviors.
 */
#include <refdm/nm_rest.h>

#include <cace/util/defs.h>
#include <cace/util/logging.h>

#include <sys/stat.h>
#include <unity.h>

// Allow this macro
#define TEST_CASE(...)

/// Temporary testing directory
char *test_root;

void suiteSetUp(void)
{
    cace_openlog();

    test_root = strdup("/tmp/testXXXXXX");
    test_root = mkdtemp(test_root);
    assert(NULL != test_root);

    m_string_t path;
    m_string_init(path);

    m_string_printf(path, "%s/share", test_root);
    assert(0 == mkdir(m_string_get_cstr(path), 0777));
    m_string_cat_cstr(path, "/refdm");
    assert(0 == mkdir(m_string_get_cstr(path), 0777));

    m_string_printf(path, "%s/local", test_root);
    assert(0 == mkdir(m_string_get_cstr(path), 0777));
    m_string_cat_cstr(path, "/share");
    assert(0 == mkdir(m_string_get_cstr(path), 0777));
    m_string_cat_cstr(path, "/refdm");
    assert(0 == mkdir(m_string_get_cstr(path), 0777));

    m_string_clear(path);
}

int suiteTearDown(int failures)
{
    free(test_root);
    test_root = NULL;
    cace_closelog();
    return failures;
}

TEST_CASE(NULL, NULL)
TEST_CASE("", NULL)
TEST_CASE("::", NULL)
TEST_CASE("{tmp}/share", "{tmp}/share/refdm")
TEST_CASE("{tmp}/share/", "{tmp}/share/refdm")
TEST_CASE("rel:{tmp}/share:/other", "{tmp}/share/refdm") // ignore relative
TEST_CASE("::rel::::{tmp}/share:/other", "{tmp}/share/refdm")
TEST_CASE("{tmp}/local/share:{tmp}/share", "{tmp}/local/share/refdm") // first valid
void test_get_docroot(const char *env_fmt, const char *expect_fmt)
{
    if (env_fmt)
    {
        m_string_t path;
        m_string_init_set_cstr(path, env_fmt);
        m_string_replace_all_cstr(path, "{tmp}", test_root);
        setenv("XDG_DATA_DIRS", m_string_get_cstr(path), 1);
        m_string_clear(path);
    }
    else
    {
        unsetenv("XDG_DATA_DIRS");
    }

    m_string_t docroot_path;
    m_string_init(docroot_path);
    refdm_nm_rest_get_docroot(docroot_path);
    if (expect_fmt)
    {
        TEST_ASSERT_FALSE(m_string_empty_p(docroot_path));

        m_string_t path;
        m_string_init_set_cstr(path, expect_fmt);
        m_string_replace_all_cstr(path, "{tmp}", test_root);
        TEST_ASSERT_EQUAL_STRING(m_string_get_cstr(path), m_string_get_cstr(docroot_path));
        m_string_clear(path);
    }
    else
    {
        TEST_ASSERT_TRUE(m_string_empty_p(docroot_path));
    }
    m_string_clear(docroot_path);
}
