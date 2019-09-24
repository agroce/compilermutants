int printf (const char *, ...);
int a;
static const int *b = &a, *c;
void
fn1 (const int **p1, int *p2)
{
    *p2 = 1;
    *p2 = **p1;
}

int
main ()
{
    int *d = &a;
    c = b;
    fn1 (&c, d);
    printf ("%d\n", a);
    return 0;
}
