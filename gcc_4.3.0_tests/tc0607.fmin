int printf (const char *, ...);
struct S0
{
    int f3;
    int f5;
};
char a;
char *b = &a;
int
main ()
{
    for (;;)
    {
        struct S0 c = { };
        struct S0 *d = &c;
        *d = c;
        *b = c.f3;
        int *e = &c.f5;
        int **f = &e;
        *e = 0;
        break;
    }
    printf ("%d\n", a);
    return 0;
}
