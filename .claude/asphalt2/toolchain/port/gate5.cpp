// Step 1, first milestone: become a real S60v3 GUI application.
//
// A GUI app's E32Main hands a factory to EikStart::RunApplication, which brings
// up CEikonEnv and then asks the factory for the application object. Nothing in
// the rest of the lifecycle happens without that, so this checks the entry path
// on its own before anything is built on top of it.
//
// TApaApplicationFactory is four words -- a type tag, the payload, a cached
// pointer and a spare -- and is trivially copyable, so AAPCS passes it in
// r0..r3 rather than by reference. Type 0 means the payload is a function
// pointer. Getting that wrong is the one thing here that cannot be checked by
// reading a def file, which is why this milestone exists.

typedef unsigned int u32;
typedef unsigned short u16;

extern "C" {
void user_panic(const void *category, int reason);
void *user_alloc(int size);
void *user_allocz(int size);
int rlibrary_load(void *lib, const void *name, const void *path);
void *rlibrary_lookup(const void *lib, int ordinal);
void eikstart_runapplication(u32 type, u32 data, u32 cached, u32 spare);
void eikapplication_ctor(void *self);
}

// Ordinals, from the Symbian source's own def files.
enum { AVKON_VTABLE_CAknApplication = 3652 };

// The vtable of a CAknApplication, worked out from the declaration order in
// apaapp.h, EIKAPP.H and aknApp.h. Base virtuals come first, and a destructor
// takes two Itanium slots at the position the base declared it.
//
//   0,1 ~CBase          6  OpenIniFileLC           12 GetDefaultDocumentFileName
//   2   Extension_      7  AppFullName             13 BitmapStoreName
//   3   PreDocConstructL 8 Capability              14 ResourceFileName
//   4   CreateDocumentL(CApaProcess*)              15 CEikApplication_Reserved1
//   5   AppDllUid       9  NewAppServerL           16 CEikApplication_Reserved2
//                       10,11 CApaApplication_Reserved1,2
//                                                  17 CreateDocumentL()
enum { VT_HEADER = 2, VT_SLOTS = 18,
       SLOT_APP_DLL_UID = 5, SLOT_CREATE_DOCUMENT = 17 };

enum { EPtrC = 1, KTypeShift = 28 };
struct Ptrc16 { u32 lengthAndType; const u16 *text; };

static void panic(const u16 *cat, int len, int reason)
{
    Ptrc16 d;
    d.lengthAndType = ((u32)EPtrC << KTypeShift) | (u32)len;
    d.text = cat;
    user_panic(&d, reason);
}

#define PANIC(name, reason) do { \
    static const u16 s[] = name; \
    panic(s, (int)(sizeof s / sizeof s[0]), (reason)); } while (0)

#define CAT_NEW {'G','5','N','E','W'}
#define CAT_RET {'G','5','R','E','T'}
#define CAT_DOC {'G','5','D','O','C'}
#define CAT_LIB {'G','5','L','I','B'}

static const u16 kAvkon[] = { 'a', 'v', 'k', 'o', 'n', '.', 'd', 'l', 'l' };

// The two slots a concrete application has to supply. They are ordinary
// functions taking `this` in r0, which is all a vtable slot ever is.
extern "C" u32 gate5_app_dll_uid(void *)
{
    return 0xE0001005;          // TUid is one word, returned in r0
}

extern "C" void *gate5_create_document(void *)
{
    PANIC(CAT_DOC, 1);          // the framework got this far
    return 0;
}

// The framework calls this once it is up, and wants a CApaApplication back.
//
// Rather than declare the base classes and hope the layout matches, take the
// real CAknApplication vtable -- avkon exports it -- copy it, and patch in the
// two slots a concrete application must supply. The object itself is built the
// way the game builds its own: zeroed memory, then the exported base
// constructor, then the vptr.
extern "C" void *gate5_new_application()
{
    u32 lib = 0;
    Ptrc16 name, nopath;
    name.lengthAndType = ((u32)EPtrC << KTypeShift) | (sizeof kAvkon / sizeof kAvkon[0]);
    name.text = kAvkon;
    nopath.lengthAndType = (u32)EPtrC << KTypeShift;
    nopath.text = 0;
    int err = rlibrary_load(&lib, &name, &nopath);
    if (err) PANIC(CAT_LIB, err);

    const u32 *real = (const u32 *)rlibrary_lookup(&lib, AVKON_VTABLE_CAknApplication);
    if (!real) PANIC(CAT_LIB, AVKON_VTABLE_CAknApplication);

    u32 *vt = (u32 *)user_alloc((VT_HEADER + VT_SLOTS) * 4);
    if (!vt) PANIC(CAT_LIB, -4);
    for (u32 i = 0; i < VT_HEADER + VT_SLOTS; i++)
        vt[i] = real[i];
    vt[VT_HEADER + SLOT_APP_DLL_UID] = (u32)&gate5_app_dll_uid;
    vt[VT_HEADER + SLOT_CREATE_DOCUMENT] = (u32)&gate5_create_document;

    // Deliberately generous. The headers in the Symbian source release are a
    // later 9.x than any one phone, so a sizeof computed from them would not be
    // the device's; 256 bytes was too few and the heap said so with USER 42,
    // ETHeapBadCellAddress. Over-allocating a zeroed block cannot corrupt
    // anything, and the framework only touches its own members.
    u32 *app = (u32 *)user_allocz(2048);
    if (!app) PANIC(CAT_LIB, -5);
    eikapplication_ctor(app);
    app[0] = (u32)(vt + VT_HEADER);   // the 9.x vptr points at the first slot
    return app;
}

extern "C" u32 gate5_main()
{
    eikstart_runapplication(0, (u32)&gate5_new_application, 0, 0);
    PANIC(CAT_RET, 0);          // RunApplication is not supposed to come back
    return 0;
}
