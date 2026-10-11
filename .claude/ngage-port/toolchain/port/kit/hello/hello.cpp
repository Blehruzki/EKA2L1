// No SDK headers: the two types this needs are written out by hand.
//
// A Symbian descriptor is a length word whose top four bits are its type, then
// its data. TPtrC (type 1, EPtrC) points at the text instead of holding it.
struct TPtrC16 {
    unsigned iLengthAndType;
    const char16_t *iPtr;
};
#define LIT(name, text) \
    static const TPtrC16 name = { (1u << 28) | (sizeof(text) / 2 - 1), text }

// The import stubs in start.s.
extern "C" void rdebug_rawprint(const TPtrC16 &aText);
extern "C" void user_panic(const TPtrC16 &aCategory, int aReason);

LIT(KHello, u"Hello from clang");
LIT(KCategory, u"HELLO");

// The image has no data section, so nothing here may be a writable static:
// build.py refuses one. Constants are fine.
extern "C" void hello_main()
{
    rdebug_rawprint(KHello);     // the emulator's log shows it
    user_panic(KCategory, 42);   // "HELLO 42": proof the imports resolved
}
