int printf (const char *, ...);
int a, d;
static int *b = &a, *e = &d;
static const int *c = &a;
int
fn1 (int **p1)
{
    return 0;
}

int
main ()
{
    *b = fn1 (&b) ^ 1;
    *b = *e = *c;
    printf ("%d\n", d);
    return 0;
}
