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
void eikappui_ctor(void *self);
void akndocument_ctor(void *self, void *app);
}

// Ordinals, from the Symbian source's own def files.
enum { AVKON_VTABLE_CAknApplication = 3652,
       AVKON_VTABLE_CAknDocument = 3632,
       AVKON_VTABLE_CAknAppUi = 3820,
       EIKCORE_VTABLE_CEikAppUi = 387 };

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
#define CAT_UI  {'G','5','U','I',' '}

static const u16 kAvkon[] = { 'a', 'v', 'k', 'o', 'n', '.', 'd', 'l', 'l' };
static const u16 kEikcore[] = { 'e', 'i', 'k', 'c', 'o', 'r', 'e', '.', 'd', 'l', 'l' };

// The two slots a concrete application has to supply. They are ordinary
// functions taking `this` in r0, which is all a vtable slot ever is.
extern "C" u32 gate5_app_dll_uid(void *)
{
    return 0xE0001005;          // TUid is one word, returned in r0
}

// Rather than count virtual declarations in headers -- whose overrides often
// omit the `virtual` keyword, so a count is a guess -- give the document a
// vtable whose every slot is a 16-byte trampoline carrying its own index:
//
//   ldr r0, [pc, #0]    @ the slot index, replacing `this`, which we do not need
//   ldr pc, [pc, #0]    @ the reporter
//   .word index
//   .word gate5_doc_slot
//
// The framework then names the slot it calls first, which is the same trick
// gate 4 uses on imports.
enum { DOC_SLOTS = 34, UI_SLOTS = 48 };
enum { SLOT_CREATE_APP_UI = 21 };   // measured, not counted

extern "C" void gate5_ui_slot(int index)
{
    PANIC(CAT_UI, index);
}

extern "C" void gate5_doc_slot(int index)
{
    PANIC(CAT_DOC, index);
}

// Fetch an exported symbol by ordinal. A vtable is a data export, so it has to
// be resolved this way rather than through an import stub -- a stub's address
// is the stub, not what it points at.
static const u32 *dll_export(const u16 *dll, int dllLen, int ordinal)
{
    u32 lib = 0;
    Ptrc16 name, nopath;
    name.lengthAndType = ((u32)EPtrC << KTypeShift) | (u32)dllLen;
    name.text = dll;
    nopath.lengthAndType = (u32)EPtrC << KTypeShift;
    nopath.text = 0;
    if (rlibrary_load(&lib, &name, &nopath)) PANIC(CAT_LIB, -10);
    const u32 *vt = (const u32 *)rlibrary_lookup(&lib, ordinal);
    if (!vt) PANIC(CAT_LIB, -ordinal);
    return vt;
}

#define AVKON_EXPORT(o)   dll_export(kAvkon, sizeof kAvkon / sizeof kAvkon[0], (o))
#define EIKCORE_EXPORT(o) dll_export(kEikcore, sizeof kEikcore / sizeof kEikcore[0], (o))

// A vtable whose every slot reports its own index. Same trick as gate 4's
// import stubs, and the reason the document's CreateAppUiL slot is known to be
// 21 rather than the 19 a count of header declarations suggested.
static u32 *trampoline_vtable(int slots, void *reporter)
{
    u32 *tramp = (u32 *)user_allocz(slots * 16);
    u32 *vt = (u32 *)user_allocz((VT_HEADER + slots) * 4);
    if (!tramp || !vt) PANIC(CAT_LIB, -11);
    for (int i = 0; i < slots; i++) {
        u32 *t = tramp + 4 * i;
        t[0] = 0xE59F0000;                  // ldr r0, [pc, #0]  -- the index
        t[1] = 0xE59FF000;                  // ldr pc, [pc, #0]  -- the reporter
        t[2] = (u32)i;
        t[3] = (u32)reporter;
        vt[VT_HEADER + i] = (u32)t;
    }
    return vt;
}

// The document asks for this once the framework wants a UI.
extern "C" void *gate5_create_app_ui(void *)
{
    u32 *ui = (u32 *)user_allocz(2048);
    if (!ui) PANIC(CAT_LIB, -12);
    eikappui_ctor(ui);

    // Constructor and vtable have to come from the same class. CAknAppUi's
    // constructor is not exported, so building with CEikAppUi's and then
    // installing CAknAppUi's vtable would describe an object that was never
    // laid out that way -- and CCoeAppUi and CEikAppUi bring mixins, so the
    // secondary vptrs the constructor sets would no longer match the primary.
    // CEikAppUi is a perfectly good app UI on its own.
    ui[0] = (u32)(EIKCORE_EXPORT(EIKCORE_VTABLE_CEikAppUi) + VT_HEADER);
    return ui;
}

extern "C" void *gate5_create_document(void *app)
{
    // Still a reporting vtable. Patching the real one at slot 21 and returning
    // an app UI does not work: the framework carries on past that call and
    // fails later, and it fails identically when the slot returns null -- so
    // slot 21 is only the first virtual called, not necessarily CreateAppUiL.
    // Telling them apart needs wrappers that record the call and then chain to
    // the real implementation, which is the next piece of work.
    u32 *vt = trampoline_vtable(DOC_SLOTS, (void *)&gate5_doc_slot);

    u32 *doc = (u32 *)user_allocz(2048);
    if (!doc) PANIC(CAT_LIB, -14);
    akndocument_ctor(doc, app);         // CAknDocument(CEikApplication&)
    doc[0] = (u32)(vt + VT_HEADER);
    return doc;
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
