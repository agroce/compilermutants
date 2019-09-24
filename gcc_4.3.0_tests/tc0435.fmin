int printf (const char *, ...);
int **a;
static int ***b = &a;
int c, d;
int
fn1 (p1)
{
    return 0 == 0 ? p1 : 0;
}

int
main ()
{
    int ***e = &a;
    c = fn1 (2L);
    d = b == e ^ c;
    printf ("%d\n", d);
    return 0;
}
