int printf (const char *, ...);
int a, d;
static int *b = &a;
static const int *c = &a;
int
main ()
{
    int *e = &a;
    *b = 1;
    d = *c;
    *e = d;
    printf ("%d\n", a);
    return 0;
}
