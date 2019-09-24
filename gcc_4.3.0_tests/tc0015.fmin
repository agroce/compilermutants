int printf (const char *, ...);
char a;
int **b;
char *c = &a;
int d;
void
fn1 (p1)
{
    int ***e = &b, ***f = &b;
    d = p1 <= (e == f);
    *c = d;
}

int
main ()
{
    fn1 (6);
    printf ("%d\n", a);
    return 0;
}
