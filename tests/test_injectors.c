#include "test.h"
#include "injectors/injector.h"

int test_injector_find_empty(void) {
    ASSERT_TRUE(injector_find("rMEM_ecc_inject") == NULL);
    ASSERT_INT_EQ(builtin_injector_count, 0);
    return 0;
}

int main(void) {
    RUN_TEST(test_injector_find_empty);
    return TEST_MAIN_RETURN();
}
