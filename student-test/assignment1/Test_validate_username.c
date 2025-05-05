#include "unity.h"
#include <stdbool.h>
#include <stdlib.h>
#include "../../examples/autotest-validate/autotest-validate.h"
#include "../../assignment-autotest/test/assignment1/username-from-conf-file.h"

/**
* This function should:
*   1) Call the my_username() function in Test_assignment_validate.c to get your hard coded username.
*   2) Obtain the value returned from function malloc_username_from_conf_file() in username-from-conf-file.h within
*       the assignment autotest submodule at assignment-autotest/test/assignment1/
*   3) Use unity assertion TEST_ASSERT_EQUAL_STRING_MESSAGE the two strings are equal.  See
*       the [unity assertion reference](https://github.com/ThrowTheSwitch/Unity/blob/master/docs/UnityAssertionsReference.md)
*/
void test_validate_my_username()
{
    /**
     * Verify /conf/username.txt config file and my_username() functions are setup properly
     */

    // Fetch names
    const char *hardname = my_username();
    char *confname = malloc_username_from_conf_file(); 

    // Construct error message
    const char fmt[] = "'%s' from malloc_username_from_conf_file() NOT EQUAL TO '%s' from my_username()";
    int sz = snprintf(NULL, 0, fmt, confname, hardname); // Call with NULL/0 to evaluate needed size
    char message[sz + 1]; // note +1 for terminating null byte
    snprintf(message, sizeof message, fmt, confname, hardname);

    // Apply test
    TEST_ASSERT_EQUAL_STRING_MESSAGE(confname, hardname, message);
    
    // Free malloc'd memory
    free(confname);
}
