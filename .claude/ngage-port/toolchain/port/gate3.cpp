// Gate 3: can a class compiled for EABI be called through a GCC98r2 vtable?
//
// Measured from the N-Gage binary, which is the ABI we have to satisfy:
//   * the vptr sits at object offset 0 and points 8 bytes BEFORE the first
//     function pointer; those two words are zero, RTTI being off
//   * a virtual call is  ldr r3,[obj] ; ldr r3,[r3,#8+4*slot] ; mov lr,pc ; bx r3
//   * entries are plain function pointers and `this` arrives in r0
//
// EABI points its vptr straight at slot 0, keeping offset-to-top and typeinfo
// at -8 and -4 -- so the layouts differ by exactly that 8-byte bias. This
// republishes a clang-built vtable in the old shape and calls into it the old
// way, and checks that the bias is load-bearing rather than accidentally right.
//
// Each check sets a bit; _start faults at 0xBEEF0000 | mask, so the address the
// loader reports says exactly which ones passed.

typedef unsigned int u32;

extern "C" int old_call(void *obj, u32 slot);   // the general dispatcher
extern "C" int old_call_slot1(void *obj);       // the exact measured instruction
extern "C" void user_panic(const void *category, int reason);   // euser ordinal 650

// A phone shows a panic as "<thread> <category> <reason>", which is the only
// way this test has of reporting a number: the fault address it falls back to
// never reaches the screen. TPtrC16 is a length word whose top four bits are
// the descriptor type (1 = EPtrC) and a pointer to the text.
static const unsigned short kCategory[] = { 'G', 'A', 'T', 'E', '3' };

struct Ptrc16 {
    unsigned int length_and_type;
    const unsigned short *text;
};

class Real {
public:
    virtual int a();
    virtual int b();
    virtual int c();
    int magic;
};

int Real::a() { return magic + 0x10; }
int Real::b() { return magic + 0x20; }
int Real::c() { return magic + 0x30; }

// Every Symbian framework class has a virtual destructor, and that is where the
// two ABIs stop agreeing on slot numbers: Itanium emits two destructor entries
// (complete, then deleting), GCC 2.x emits one. A shim has to collapse them.
class RealD {
public:
    virtual ~RealD();
    virtual int z();
    int magic;
};

RealD::~RealD() {}
int RealD::z() { return magic + 0x50; }

void operator delete(void *) {}
void operator delete(void *, unsigned) {}

// Symbian's whole framework is C-class-plus-M-mixin, so every class the game
// derives from has a second base with its own vptr. The N-Gage constructors
// bear that out: they store one vptr at object offset 0 and another at +4.
class M1 { public: virtual int m1(); };
class M2 { public: virtual int m2(); };
class Both : public M1, public M2 {
public:
    virtual int m1();
    virtual int m2();
    int magic;
};
int Both::m1() { return magic + 0x70; }
int Both::m2() { return magic + 0x60; }

struct Old {        // the shape the old code expects: vptr first, then fields
    void **vptr;
    int magic;
};

struct OldBoth {    // and with a mixin: two vptrs, then the fields
    void **vptr0;
    void **vptr1;
    int magic;
};

extern "C" u32 gate3_main()
{
    u32 pass = 0;

    Real r;
    r.magic = 0x100;
    void **eabi = *(void ***)&r;          // EABI vptr -> &slot0

    void *vt[5];
    vt[0] = 0;                            // offset-to-top, as the old compiler wrote it
    vt[1] = 0;                            // typeinfo, null with RTTI off
    vt[2] = eabi[0];
    vt[3] = eabi[1];
    vt[4] = eabi[2];

    Old o;
    o.vptr = vt;                          // the old vptr points at the header
    o.magic = 0x100;

    if (old_call(&o, 0) == 0x110) pass |= 1u << 0;
    if (old_call_slot1(&o) == 0x120) pass |= 1u << 1;
    if (old_call(&o, 2) == 0x130) pass |= 1u << 2;

    // Bias the vptr the way EABI would and slot 0 must now reach the wrong
    // function. If this check fails, the three above proved nothing.
    Old wrong;
    wrong.vptr = vt + 2;
    wrong.magic = 0x100;
    if (old_call(&wrong, 0) == 0x130) pass |= 1u << 3;

    if (r.b() == 0x120) pass |= 1u << 4;  // the class itself still works

    RealD rd;
    rd.magic = 0x100;
    void **eabid = *(void ***)&rd;
    void *vtd[4];
    vtd[0] = 0;
    vtd[1] = 0;
    vtd[2] = eabid[1];                    // one destructor slot, not two
    vtd[3] = eabid[2];                    // z() moves from EABI slot 2 to old slot 1
    Old od;
    od.vptr = vtd;
    od.magic = 0x100;
    if (old_call(&od, 1) == 0x150) pass |= 1u << 5;

    // The second base. Old code holds a pointer to the M2 subobject, so the
    // vptr it reads is the one at +4 and the entries behind it have to adjust
    // `this` back to the whole object before the method sees it.
    Both bo;
    bo.magic = 0x100;
    void **sec = *(void ***)((char *)&bo + 4);
    void *vts[3];
    vts[0] = 0;
    vts[1] = 0;
    vts[2] = sec[0];
    OldBoth ob;
    ob.vptr0 = 0;
    ob.vptr1 = vts;
    ob.magic = 0x100;
    if (old_call(&ob.vptr1, 0) == 0x160) pass |= 1u << 6;

    Ptrc16 cat;
    cat.length_and_type = (1u << 28) | (sizeof kCategory / sizeof kCategory[0]);
    cat.text = kCategory;
    user_panic(&cat, (int)pass);

    return pass;
}
