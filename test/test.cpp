#include <unity.h>

void test_example(void) {
    TEST_ASSERT_EQUAL(4, 2 + 2);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_example);
    UNITY_END();
}

void loop() {
    // Kosong — test hanya dijalankan sekali
}