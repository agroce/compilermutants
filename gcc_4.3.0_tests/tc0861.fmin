int printf (const char *, ...);
int a;
static int *b = &a;
char c;
char *d = &c;
void
fn1 (p1)
{
    *d = p1;
}

int
main ()
{
    int *e = &a;
    int f = 2;
    fn1 (b == e ^ f);
    printf ("%d\n", c);
    return 0;
}
