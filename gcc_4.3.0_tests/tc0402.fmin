int printf (const char *, ...);
struct S3
{
    int f0;
}
a, b, e, f =
{
    1
}

, *d = &a;
static const struct S3 *c = &b;
void
fn1 ()
{
    struct S3 *g = &b;
    *g = f;
}

int
main ()
{
    {
        struct S3 *h = &b;
        *h = f;
    }
    e = *c;
    *d = e;
    fn1 ();
    printf ("%d\n", a.f0);
    return 0;
}
