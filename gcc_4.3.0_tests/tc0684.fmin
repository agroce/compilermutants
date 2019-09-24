int printf (const char *, ...);
int a = 1, d = 1;
static int *b = &a;
static const int *c = &a;
int
main ()
{
    *b = !0xE10A4E6FL;
    if (*c)
        d = 0;
    *b = 0;
    printf ("%d\n", d);
    return 0;
}
