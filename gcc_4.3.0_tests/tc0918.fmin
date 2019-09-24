int printf (const char *, ...);
int a = 1;
static const int *b = &a;
int
main ()
{
    int *c = &a;
    *c = 0;
    *c = *b;
    printf ("%d\n", a);
    return 0;
}
