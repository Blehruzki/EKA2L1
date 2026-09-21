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
void eikstart_runapplication(u32 type, u32 data, u32 cached, u32 spare);
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

#define CAT_NEW {'G','5','N','E','W'}
#define CAT_RET {'G','5','R','E','T'}

// The framework calls this once it is up. Reaching it is the whole milestone.
extern "C" void *gate5_new_application()
{
    PANIC(CAT_NEW, 1);
    return 0;
}

extern "C" u32 gate5_main()
{
    eikstart_runapplication(0, (u32)&gate5_new_application, 0, 0);
    PANIC(CAT_RET, 0);          // RunApplication is not supposed to come back
    return 0;
}
