// typedef long long mint;
typedef int mint;

mint
fib(mint n)
{
    if (!n)
        return 0;
    if (n <= 2)
        return 1;
    return fib(n - 1) + fib(n - 2);
}
