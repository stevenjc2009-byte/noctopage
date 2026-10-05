#include "test.h"

#include "core/natsort.h"

#include <string.h>

#define LT(a, b) CHECK(natcmp((a), (b)) < 0 && natcmp((b), (a)) > 0)

static void test_basic_order(void)
{
    LT("p2.jpg", "p10.jpg");
    LT("p1.jpg", "p2.jpg");
    LT("p9.png", "p10.png");
    LT("p99.png", "p100.png");
    LT("a", "b");
    LT("a", "ab");
    LT("", "a");
    CHECK_INT(natcmp("same", "same"), 0);
    CHECK_INT(natcmp("", ""), 0);
}

static void test_case_insensitive(void)
{
    LT("Apple", "banana");
    LT("apple", "Banana");
    LT("P2.JPG", "p10.jpg");
    LT("img_a1", "img_Z"); /* folded: 'a' < 'z' even though 'Z' < 'a' in ASCII */
}

static void test_leading_zeros(void)
{
    LT("p01.jpg", "p2.jpg");
    LT("p002.jpg", "p10.jpg");
    LT("p007.jpg", "p10.jpg");
    /* Equal by value: tie broken by strcmp so the order is total. */
    CHECK(natcmp("a01", "a1") != 0);
    CHECK(natcmp("a01", "a1") == -natcmp("a1", "a01"));
    LT("a0", "a1");
    LT("a00", "a1");
}

static void test_mixed_text(void)
{
    LT("ch2 p10", "ch10 p1");
    LT("vol1/ch2", "vol1/ch10");
    LT("vol2/ch1", "vol10/ch1");
    LT("a1b2", "a1b10");
    LT("a1b", "a1c");
    LT("x9", "x10y");
    LT("1", "a");          /* digits before letters (byte order) */
    LT("2", "10");
    LT("9", "10a");
}

static void test_big_numbers(void)
{
    /* Longer than any integer type: must compare by length then digits, no overflow. */
    LT("p99999999999999999999999999", "p100000000000000000000000000");
    LT("p123456789012345678901234567890", "p123456789012345678901234567891");
    CHECK_INT(natcmp("p00000000000000000000000000001", "p00000000000000000000000000001"), 0);
}

static void test_utf8_bytes(void)
{
    /* UTF-8 compares as unsigned bytes: ASCII < any multibyte lead byte. */
    LT("z", "\xE3\x81\x82");                         /* z < Hiragana A */
    LT("\xE3\x81\x82" "2", "\xE3\x81\x82" "10");     /* number after multibyte text */
    LT("\xE3\x81\x82", "\xE3\x81\x84");              /* A < I */
    LT("\xE6\xBC\xAB\xE7\x94\xBB" "2.jpg", "\xE6\xBC\xAB\xE7\x94\xBB" "10.jpg");
    LT("\x7f", "\x80"); /* high bytes are unsigned, not negative */
}

static void test_sort_helper(void)
{
    const char *names[] = {"p10.jpg", "p2.jpg", "P1.jpg", "p1b.jpg", "cover.jpg", "p20.jpg", "p3.jpg"};
    const char *want[] = {"cover.jpg", "P1.jpg", "p1b.jpg", "p2.jpg", "p3.jpg", "p10.jpg", "p20.jpg"};
    size_t n = sizeof names / sizeof names[0];

    natsort_strings(names, n);
    for (size_t i = 0; i < n; i++)
        CHECK_STR(names[i], want[i]);

    natsort_strings(names, 0);
    natsort_strings(names, 1);
    CHECK_STR(names[0], "cover.jpg");

    /* Already sorted and reverse-sorted inputs. */
    {
        const char *rev[] = {"p3", "p2", "p1"};
        natsort_strings(rev, 3);
        CHECK_STR(rev[0], "p1");
        CHECK_STR(rev[2], "p3");
    }
}

static void test_total_order(void)
{
    const char *v[] = {"a", "A", "a1", "a01", "a001", "A1", "b", "a10", "a2", "", "1", "01", "\xE3\x81\x82"};
    size_t n = sizeof v / sizeof v[0];
    /* Antisymmetry and transitivity over every triple. */
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) {
            CHECK_INT(natcmp(v[i], v[j]), -natcmp(v[j], v[i]));
            for (size_t k = 0; k < n; k++)
                if (natcmp(v[i], v[j]) < 0 && natcmp(v[j], v[k]) < 0)
                    CHECK(natcmp(v[i], v[k]) < 0);
        }
}

int main(void)
{
    test_basic_order();
    test_case_insensitive();
    test_leading_zeros();
    test_mixed_text();
    test_big_numbers();
    test_utf8_bytes();
    test_sort_helper();
    test_total_order();
    return TEST_DONE("natsort");
}
