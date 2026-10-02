// Step 1: be a real S60v3 GUI application.
//
// A GUI app hands a factory to EikStart::RunApplication, which brings up
// CEikonEnv and then asks for an application, a document and an app UI in
// turn. Each is built the way the game builds its own: zeroed memory, the
// exported base constructor, then a vtable.
//
// The vtable is read back out of the object the constructor just built rather
// than looked up by ordinal. Ordinals for these libraries come from the
// Symbian source release and differ between releases; a vtable fetched by a
// wrong ordinal is a real symbol that is not a vtable, which is an access
// violation with nothing to say about which lookup was wrong. A constructor
// ordinal cannot fail that way -- it lives in the import section, which the
// loader resolves when the image starts, so a wrong one stops the app rather
// than corrupting it. It also keeps constructor and vtable in step by
// construction, which pairing them by hand did not.

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern "C" {
void user_panic(const void *category, int reason);
void *user_allocz(int size);
void eikstart_runapplication(u32 type, u32 data, u32 cached, u32 spare);
void eikapplication_ctor(void *self);                 // CEikApplication::CEikApplication()
void akndocument_ctor(void *self, void *app);         // CAknDocument::CAknDocument(CEikApplication&)
void coeappui_ctor(void *self);                       // CCoeAppUi::CCoeAppUi()
void eikappui_ctor(void *self);                       // CEikAppUi::CEikAppUi()
void eikappui_baseconstructl(void *self, int flags);  // CEikAppUi::BaseConstructL(TInt)
}

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

#define CAT_MEM {'G','5','M','E','M'}
#define CAT_UI  {'G','5','U','I',' '}
#define CAT_DOC {'G','5','D','O','C'}
#define CAT_RET {'G','5','R','E','T'}

// An Itanium vtable has two words before the first slot -- offset-to-top and
// typeinfo -- and the vptr points past them at slot 0.
enum { VT_HEADER = 2 };

// Application, from the declaration order in apaapp.h, EIKAPP.H and aknApp.h:
//   0,1 ~CBase          6 OpenIniFileLC       12 GetDefaultDocumentFileName
//   2  Extension_       7 AppFullName         13 BitmapStoreName
//   3  PreDocConstructL 8 Capability          14 ResourceFileName
//   4  CreateDocumentL(CApaProcess*)          15,16 CEikApplication_Reserved1,2
//   5  AppDllUid        9 NewAppServerL       17 CreateDocumentL()
//                      10,11 CApaApplication_Reserved1,2
enum { APP_SLOTS = 18, SLOT_APP_DLL_UID = 5, SLOT_CREATE_DOCUMENT = 17 };

// Measured, not counted: chaining wrappers reported the document calling slot
// 19, and chaining that slot to its real implementation raises the unhandled
// exception a pure virtual does.
enum { DOC_SLOTS = 34, SLOT_CREATE_APP_UI = 19 };

// Found exactly, by looking up the exported CEikAppUi::ConstructL and finding
// its address in the table -- better than watching which slot is called first,
// which is what misidentified the document's.
enum { UI_SLOTS = 48, SLOT_UI_CONSTRUCT = 16 };

// EIKAPPUI.H. Without ENoScreenFurniture, BaseConstructL goes on to
// CreateResourceIndependentFurnitureL and faults: a status pane needs more of
// an environment than an app this bare has.
enum { ENoAppResourceFile = 0x01, ENoScreenFurniture = 0x04 };

static const u32 *vtable_of(const u32 *object)
{
    return (const u32 *)object[0] - VT_HEADER;
}

static u32 *copy_vtable(const u32 *real, int slots)
{
    u32 *vt = (u32 *)user_allocz((VT_HEADER + slots) * 4);
    if (!vt) PANIC(CAT_MEM, -1);
    for (int i = 0; i < VT_HEADER + slots; i++)
        vt[i] = real[i];
    return vt;
}

// ---- the app UI ------------------------------------------------------------

extern "C" void gate5_ui_construct(void *self)
{
    eikappui_baseconstructl(self, ENoAppResourceFile | ENoScreenFurniture);

    // Reaching here is the milestone: application, document and app UI are all
    // built and the framework accepted them. Letting ConstructL return instead
    // faults shortly after -- an app UI with no control and no view has nothing
    // for the framework to run -- and that belongs with the next step.
    PANIC(CAT_UI, 0);
}

extern "C" void *gate5_create_app_ui(void *)
{
    u32 *ui = (u32 *)user_allocz(2048);
    if (!ui) PANIC(CAT_MEM, -2);

    // CCoeAppUi's constructor is what assigns iCoeEnv, which BaseConstructL
    // dereferences on its first line; a derived class's constructor would call
    // it, so call it here. CEikAppUi's own constructor is empty.
    coeappui_ctor(ui);
    eikappui_ctor(ui);

    u32 *vt = copy_vtable(vtable_of(ui), UI_SLOTS);
    vt[VT_HEADER + SLOT_UI_CONSTRUCT] = (u32)&gate5_ui_construct;
    ui[0] = (u32)(vt + VT_HEADER);
    return ui;
}

// ---- the document ----------------------------------------------------------

extern "C" void *gate5_create_document(void *app)
{
    u32 *doc = (u32 *)user_allocz(2048);
    if (!doc) PANIC(CAT_MEM, -3);
    akndocument_ctor(doc, app);

    u32 *vt = copy_vtable(vtable_of(doc), DOC_SLOTS);
    vt[VT_HEADER + SLOT_CREATE_APP_UI] = (u32)&gate5_create_app_ui;
    doc[0] = (u32)(vt + VT_HEADER);
    return doc;
}

// ---- the application -------------------------------------------------------

extern "C" u32 gate5_app_dll_uid(void *)
{
    return 0xE0001005;          // TUid is one word, returned in r0
}

extern "C" void *gate5_new_application()
{
    u32 *app = (u32 *)user_allocz(2048);
    if (!app) PANIC(CAT_MEM, -4);
    eikapplication_ctor(app);

    u32 *vt = copy_vtable(vtable_of(app), APP_SLOTS);
    vt[VT_HEADER + SLOT_APP_DLL_UID] = (u32)&gate5_app_dll_uid;
    vt[VT_HEADER + SLOT_CREATE_DOCUMENT] = (u32)&gate5_create_document;
    app[0] = (u32)(vt + VT_HEADER);
    return app;
}

// TApaApplicationFactory is four trivially copyable words -- a type tag, the
// payload, a cached pointer and a spare -- so AAPCS passes it in r0..r3. Type 0
// means the payload is a function pointer.
extern "C" u32 gate5_main()
{
    eikstart_runapplication(0, (u32)&gate5_new_application, 0, 0);
    PANIC(CAT_RET, 0);          // RunApplication is not supposed to come back
    return 0;
}
