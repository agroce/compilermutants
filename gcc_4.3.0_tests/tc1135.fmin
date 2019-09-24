int printf (const char *, ...);
int a = 1;
static int *b = &a;
static const int *c = &a;
int
main ()
{
    a = 0;
    *b = *c;
    printf ("%d\n", a);
    return 0;
}
