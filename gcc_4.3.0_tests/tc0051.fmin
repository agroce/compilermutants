int printf (const char *, ...);
int a;
int *b;
int c[4];
static int *d = &c[3];
int **e = &b;
void
fn1 (int *p1, int *p2)
{
    *p1 = 1;
    *p1 = *p2;
    **e = *p2;
}

int
main ()
{
    b = &a;
    fn1 (d, &c[3]);
    printf ("%d\n", a);
    return 0;
}
