int printf (const char *, ...);
int a = 1;
static const int *b = &a;
static int *c = &a;
int
main ()
{
    a = 0;
    *c = *b;
    printf ("%d\n", a);
    return 0;
}
