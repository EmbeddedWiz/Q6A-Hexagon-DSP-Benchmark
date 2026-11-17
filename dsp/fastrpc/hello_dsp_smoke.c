/* Initial Hexagon V68 FastRPC smoke-test.
 * The return code validates the computation on the DSP.
 */
volatile int result;

int main(void)
{
    volatile int a = 123;
    volatile int b = 456;

    result = a * b + 7;
    return (result == 56095) ? 0 : 1;
}
