#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool is_oomp_present(
    const char *str, 
    size_t str_len
)
{
    static const char *oomp_str = "oomp";
    static const size_t oomp_len = 4;

    for (size_t i = 0; i < str_len; i++)
    {
        if (str[i] == oomp_str[0])
        {
            for (size_t j = 1; j < oomp_len; j++)
            {
                if (i + j < str_len)
                {
                    if (str[i + j] == oomp_str[j])
                    {
                        if (j >= oomp_len - 1)
                        {
                            return true;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
                else 
                {
                    return false;
                }
            }
        }
    }
    return false;
}

void test(const char *input, bool expected) {
    bool result = is_oomp_present(input, strlen(input));
    printf("Test \"%s\": %s\n", input, (result == expected) ? "PASS" : "FAIL");
}

int main()
{
    test("oomp", true);
    test("hello_oomp_world", true);
    test("noomp", true);
    test("oom", false);
    test("foo", false);
    test("baroomp", true);
    test("oompp", true);
    test("o_oomp", true);
    test("ooommp", false);
    test("", false);
    test("ooooomp", true);
    test("o", false);
    test("p", false);
    test("oomp!", true);
    test("!oomp", true);
    test("!omoooomoomp", true);
    return 0;
}