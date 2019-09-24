int printf (const char *, ...);
int a;
int *const b = &a;
static const int *c = &a;
int
main ()
{
    *b = 1;
    *b = *c;
    printf ("%d\n", a);
    return 0;
}
