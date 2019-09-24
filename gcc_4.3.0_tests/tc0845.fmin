int printf (const char *, ...);
char a;
static char *b = &a;
int c;
int *d = &c;
int
fn1 (int p1, int p2)
{
    return p1 - p2;
}

int
main ()
{
    *d = fn1 (b == &a, 30);
    printf ("%d\n", c);
    return 0;
}
