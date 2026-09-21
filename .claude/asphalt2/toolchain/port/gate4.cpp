// Gate 4: load the N-Gage binary on S60v3 and run it.
//
// Read 6RBC.APP, copy its code into an executable chunk, rebase it with its own
// relocation table, point all 462 imports at stubs, and enter it.  The game is
// an EKA1 polymorphic DLL, so "enter it" means its entry point and then export
// ordinal 1, CApaApplication's factory.
//
// The result is reported as a panic, because that is the only channel a phone
// gives us.  The category says how far it got and the reason says which import
// its code reached first -- and reaching an import at all is the whole point:
// it means the game's own instructions ran, on hardware they were never built
// for, and called out through our table.

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int i32;

extern "C" {
void user_panic(const void *category, int reason);
void *user_alloc(int size);
i32 chunk_createlocalcode(void *chunk, int size, int maxSize, int owner);
u8 *chunk_base(const void *chunk);
i32 fs_connect(void *fs, int slots);
i32 file_open(void *file, void *fs, const void *name, u32 mode);
i32 file_size(const void *file, int *size);
i32 file_read(const void *file, void *des);
}

// TDesC16 and TDes8: a length word whose top four bits are the descriptor type,
// then (for a modifiable one) a maximum length, then the data pointer.
enum { EPtrC = 1, EPtr = 2, KTypeShift = 28 };

struct Ptrc16 { u32 lengthAndType; const u16 *text; };
struct Ptr8 { u32 lengthAndType; int maxLength; u8 *ptr; };

static void panic(const u16 *cat, int catLen, int reason)
{
    Ptrc16 d;
    d.lengthAndType = ((u32)EPtrC << KTypeShift) | (u32)catLen;
    d.text = cat;
    user_panic(&d, reason);
}

#define PANIC(name, reason) do { \
    static const u16 s[] = name; \
    panic(s, (int)(sizeof s / sizeof s[0]), (reason)); } while (0)

#define CAT_FS  {'G','4','F','S'}
#define CAT_MEM {'G','4','M','E','M'}
#define CAT_HDR {'G','4','H','D','R'}
#define CAT_IMP {'G','4','I','M','P'}
#define CAT_RET {'G','4','R','E','T'}

// The EKA1 header, at the offsets EKA2L1's loader reads.
struct E32 {
    u32 uid1, uid2, uid3, check, sig, cpu;
    u32 pad, compression, petran, timeLo, timeHi;
    u32 flags, codeSize, dataSize, heapMin, heapMax, stackSize, bssSize;
    u32 entryPoint, codeBase, dataBase, dllRefCount;
    u32 exportDirOffset, exportDirCount, textSize;
    u32 codeOffset, dataOffset, importOffset, codeRelocOffset, dataRelocOffset;
};

static const u16 kPath0[] = {'E',':','\\','6','r','b','c','.','a','p','p'};
static const u16 kPath1[] = {'C',':','\\','6','r','b','c','.','a','p','p'};
static const u16 kPath2[] = {'E',':','\\','s','y','s','t','e','m','\\','a','p','p','s','\\',
                             '6','r','b','c','\\','6','r','b','c','.','a','p','p'};

// Every import points here, through a 16-byte stub carrying its own report
// code. That code is `import count * 1000 + index`, because a panic gives us
// one integer and which binary was loaded is half the answer: this game ships
// both as a 1.6 MB image with 462 imports and as a 4 KB loader stub with 41,
// and an index alone cannot tell them apart. Keeping it in the stub also keeps
// this program free of writable globals, which the image has no room for --
// it declares no .bss.
extern "C" void gate4_report(int code)
{
    PANIC(CAT_IMP, code);
}

extern "C" u32 gate4_main()
{
    u32 fs[2] = { 0, 0 };
    u32 file[4] = { 0, 0, 0, 0 };
    i32 err = fs_connect(fs, -1);
    if (err) PANIC(CAT_FS, err);

    const u16 *paths[3] = { kPath0, kPath1, kPath2 };
    const int lens[3] = { sizeof kPath0 / 2, sizeof kPath1 / 2, sizeof kPath2 / 2 };
    err = -1;
    for (int i = 0; i < 3 && err; i++) {
        Ptrc16 name;
        name.lengthAndType = ((u32)EPtrC << KTypeShift) | (u32)lens[i];
        name.text = paths[i];
        err = file_open(file, fs, &name, 1);       // EFileRead | EFileShareReadersOnly
    }
    if (err) PANIC(CAT_FS, err);

    int size = 0;
    err = file_size(file, &size);
    if (err) PANIC(CAT_FS, err);

    u8 *raw = (u8 *)user_alloc(size);
    if (!raw) PANIC(CAT_MEM, size);

    // RFile::Read stops at the descriptor's maximum, so loop until it is full.
    int got = 0;
    while (got < size) {
        Ptr8 des;
        des.lengthAndType = (u32)EPtr << KTypeShift;
        des.maxLength = size - got;
        des.ptr = raw + got;
        err = file_read(file, &des);
        if (err) PANIC(CAT_FS, err);
        int n = (int)(des.lengthAndType & 0x0FFFFFFF);
        if (n <= 0) break;
        got += n;
    }
    if (got != size) PANIC(CAT_FS, got);

    const E32 *h = (const E32 *)raw;
    if (h->sig != 0x434F5045) PANIC(CAT_HDR, 1);            // 'EPOC'
    if (h->uid1 != 0x10000079) PANIC(CAT_HDR, 2);           // a polymorphic DLL
    if (h->cpu != 0x2000 && h->cpu != 0x1000) PANIC(CAT_HDR, 3);  // must be EKA1
    if (h->compression) PANIC(CAT_HDR, 4);
    if (h->textSize > h->codeSize) PANIC(CAT_HDR, 5);

    const u32 nImports = (h->codeSize - h->textSize) / 4;
    const u32 stubBytes = nImports * 16;
    const u32 chunkSize = h->codeSize + stubBytes;

    u32 chunk[2] = { 0, 0 };
    err = chunk_createlocalcode(chunk, (int)chunkSize, (int)chunkSize, 0);
    if (err) PANIC(CAT_MEM, err);
    u8 *base = chunk_base(chunk);
    if (!base) PANIC(CAT_MEM, 0x0BADBA5E);

    for (u32 i = 0; i < h->codeSize; i++)
        base[i] = raw[h->codeOffset + i];

    // Rebase. Type 3 is KInferredRelocType and this image has no data section,
    // so every relocation lands in the code we just copied.
    const u32 delta = (u32)base - h->codeBase;
    u32 relocated = 0;
    if (h->codeRelocOffset) {
        const u8 *sec = raw + h->codeRelocOffset;
        const u32 secSize = *(const u32 *)sec;
        u32 o = 8;
        while (o < secSize + 8) {
            const u32 pageBase = *(const u32 *)(sec + o);
            const u32 blockSize = *(const u32 *)(sec + o + 4);
            if (blockSize < 8 || (blockSize & 1)) break;
            for (u32 k = 0; k < (blockSize - 8) / 2; k++) {
                const u16 e = *(const u16 *)(sec + o + 8 + 2 * k);
                if (!e) continue;
                const u32 at = pageBase + (e & 0x0FFF);
                if (at + 4 > h->codeSize) PANIC(CAT_HDR, 6);
                *(u32 *)(base + at) += delta;
                relocated++;
            }
            o += blockSize;
        }
    }
    if (!relocated) PANIC(CAT_HDR, 7);

    // Build one stub per import and write its address into the import address
    // table, which on EKA1 sits at the end of the code section.
    //   ldr r0, [pc, #0]   -> the report code
    //   ldr pc, [pc, #0]   -> gate4_report
    u32 *iat = (u32 *)(base + h->textSize);
    u8 *stub = base + h->codeSize;
    for (u32 i = 0; i < nImports; i++) {
        u32 *s = (u32 *)(stub + 16 * i);
        s[0] = 0xE59F0000;
        s[1] = 0xE59FF000;
        s[2] = nImports * 1000 + i;
        s[3] = (u32)&gate4_report;
        iat[i] = (u32)s;
    }

    // Enter it. EKA1 calls a DLL's entry point with EDllProcessAttach first,
    // then apparc asks ordinal 1 for the application object.
    typedef int (*EntryFn)(int);
    typedef void *(*Ordinal1Fn)(void);
    ((EntryFn)(base + h->entryPoint))(0);
    const u32 ord1 = *(const u32 *)(raw + h->exportDirOffset);
    void *app = ((Ordinal1Fn)(base + ord1))();

    PANIC(CAT_RET, (int)(u32)app);
    return 0;
}
