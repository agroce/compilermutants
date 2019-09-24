int printf (const char *, ...);
static int a = 0, b;
int *c = &b;
int
main ()
{
    int *d = &a;
    *c = d != &a | 9L ^ *d;
    printf ("%d\n", b);
    return 0;
}
